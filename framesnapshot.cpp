// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file framesnapshot.cpp
 * Frame Snapshot
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/dma-buf.h>

extern "C" {
#include <libavutil/frame.h>
}

#include "drmbuffer.h"
#include "framesnapshot.h"
#include "logger.h"

/**
 * @addtogroup misc
 * @{
 */

/**
 * Check, if a snapshot can be taken from the buffer
 *
 * The buffer needs DMA-BUF descriptors. Buffers, which are only mapped
 * (e.g. the software osd), can't be shared.
 */
bool cFrameSnapshot::CanTake(cDrmBuffer *buf)
{
	return buf && buf->IsDirty() && buf->DmaBufHandle(0) > 0 && buf->NumPlanes() > 0;
}

/**
 * Take a snapshot of a drm buffer
 *
 * Must be called, while the buffer is valid (e.g. in the display thread).
 * It doesn't block: the descriptors are duplicated, nothing is mapped or copied.
 *
 * @param buf       drm buffer
 * @param keepFrame hold a reference to the frame of the buffer, so the decoder
 *                  can't write a new frame into it until Release(). It must be
 *                  released soon then, the decoder has only a few buffers.
 * @return          true on success
 */
bool cFrameSnapshot::Take(cDrmBuffer *buf, bool keepFrame)
{
	Release();

	if (!CanTake(buf))
		return false;

	// buffers created from a software decoded frame (dumb buffer) have no object list,
	// but one DMA-BUF descriptor for all planes
	int numObjects = buf->NumObjects() > 0 ? buf->NumObjects() : 1;
	if (numObjects > 4)
		return false;

	for (int i = 0; i < numObjects; i++) {
		int fd = buf->DmaBufHandle(i);
		size_t size = buf->Size(i);

		if (fd <= 0 || size == 0) {
			Release();
			return false;
		}

		m_fd[i] = fcntl(fd, F_DUPFD_CLOEXEC, 0);
		if (m_fd[i] < 0) {
			LOGERROR("framesnapshot: %s: cannot duplicate DMA-BUF handle %d (%d): %m", __FUNCTION__, fd, errno);
			Release();
			return false;
		}
		m_objSize[i] = size;
		m_numObjects = i + 1;

		// The decoder may have closed the imported DMA-BUF already (e.g. the frame of
		// the buffer was released after a failed pageflip) and its descriptor number
		// may belong to another DMA-BUF now. The duplicated descriptor refers to the
		// same DMA-BUF as at the time of the dup, so its inode is checked.
		if (i == 0 && buf->DmaBufInode() && cDrmBuffer::FdInode(m_fd[0]) != buf->DmaBufInode()) {
			LOGDEBUG2(L_GRAB, "framesnapshot: %s: DMA-BUF handle %d was reused, skip the snapshot", __FUNCTION__, fd);
			Release();
			return false;
		}
	}

	m_width = buf->Width();
	m_height = buf->Height();
	m_pixFmt = buf->PixFmt();
	m_modifier = buf->Modifier();
	m_numPlanes = buf->NumPlanes() > 4 ? 4 : buf->NumPlanes();

	for (int i = 0; i < m_numPlanes; i++) {
		m_objIdx[i] = buf->NumObjects() > 0 ? buf->ObjectIndex(i) : 0;
		m_offset[i] = buf->Offset(i);
		m_pitch[i] = buf->Pitch(i);

		if (m_objIdx[i] < 0 || m_objIdx[i] >= m_numObjects) {
			Release();
			return false;
		}
	}

	m_fbId = buf->Id();
	m_screenRect = buf->GetScreenRect();
	m_cropRect = buf->GetCropRect();

	m_pts = AV_NOPTS_VALUE;
	m_colorspace = AVCOL_SPC_UNSPECIFIED;
	m_colorRange = AVCOL_RANGE_UNSPECIFIED;
	if (buf->frame) {
		m_pts = buf->frame->pts;
		m_colorspace = buf->frame->colorspace;
		m_colorRange = buf->frame->color_range;

		// a new reference to the buffers of the frame (no copy), the decoder
		// gets them back, when the last reference is released
		if (keepFrame) {
			m_pFrame = av_frame_alloc();
			if (m_pFrame && av_frame_ref(m_pFrame, buf->frame) < 0) {
				LOGDEBUG2(L_GRAB, "framesnapshot: %s: cannot reference the frame, its content may change", __FUNCTION__);
				av_frame_free(&m_pFrame);
			}
		}
	}

	return true;
}

/**
 * Release the snapshot (unmaps it, releases the frame and closes the duplicated descriptors)
 */
void cFrameSnapshot::Release(void)
{
	Unmap();
	av_frame_free(&m_pFrame);   // the decoder may reuse the DMA-BUF now

	for (int i = 0; i < 4; i++) {
		if (m_fd[i] >= 0)
			close(m_fd[i]);
		m_fd[i] = -1;
		m_objSize[i] = 0;
	}
	m_numObjects = 0;
	m_numPlanes = 0;
}

/**
 * Exchange the content of two snapshots
 *
 * This is used to hand over a snapshot between threads without copying.
 */
void cFrameSnapshot::Swap(cFrameSnapshot &o)
{
	std::swap(m_width, o.m_width);
	std::swap(m_height, o.m_height);
	std::swap(m_pixFmt, o.m_pixFmt);
	std::swap(m_modifier, o.m_modifier);
	std::swap(m_numPlanes, o.m_numPlanes);
	std::swap(m_objIdx, o.m_objIdx);
	std::swap(m_offset, o.m_offset);
	std::swap(m_pitch, o.m_pitch);
	std::swap(m_numObjects, o.m_numObjects);
	std::swap(m_fd, o.m_fd);
	std::swap(m_objSize, o.m_objSize);
	std::swap(m_map, o.m_map);
	std::swap(m_pFrame, o.m_pFrame);
	std::swap(m_fbId, o.m_fbId);
	std::swap(m_pts, o.m_pts);
	std::swap(m_screenRect, o.m_screenRect);
	std::swap(m_cropRect, o.m_cropRect);
	std::swap(m_colorspace, o.m_colorspace);
	std::swap(m_colorRange, o.m_colorRange);
}

/**
 * Map the snapshot for reading
 *
 * The objects are mapped read-only and a CPU access is started (cache
 * coherency for cached buffers). Unmap() ends the access.
 *
 * @param[out] frame    the mapped frame
 * @return              true on success
 */
bool cFrameSnapshot::Map(sMappedFrame *frame)
{
	if (!IsValid())
		return false;

	for (int i = 0; i < m_numObjects; i++) {
		if (m_map[i])
			continue;

		void *map = mmap(NULL, m_objSize[i], PROT_READ, MAP_SHARED, m_fd[i], 0);
		if (map == MAP_FAILED) {
			LOGERROR("framesnapshot: %s: cannot map object %d size %zu (%d): %m", __FUNCTION__, i, m_objSize[i], errno);
			Unmap();
			return false;
		}
		m_map[i] = map;

		struct dma_buf_sync sync = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
		ioctl(m_fd[i], DMA_BUF_IOCTL_SYNC, &sync); // not supported by every exporter, ignore errors
	}

	frame->width = m_width;
	frame->height = m_height;
	frame->pixFmt = m_pixFmt;
	frame->modifier = m_modifier;
	frame->numPlanes = m_numPlanes;
	frame->colorspace = m_colorspace;
	frame->colorRange = m_colorRange;

	for (int i = 0; i < 4; i++) {
		frame->plane[i] = nullptr;
		frame->planeEnd[i] = nullptr;
		frame->pitch[i] = 0;
	}

	for (int i = 0; i < m_numPlanes; i++) {
		const uint8_t *base = (const uint8_t *)m_map[m_objIdx[i]];
		size_t size = m_objSize[m_objIdx[i]];

		if (m_offset[i] >= size) {
			Unmap();
			return false;
		}

		frame->plane[i] = base + m_offset[i];
		frame->planeEnd[i] = base + size;
		frame->pitch[i] = m_pitch[i];
	}

	return true;
}

/**
 * End the CPU access and unmap the objects
 */
void cFrameSnapshot::Unmap(void)
{
	for (int i = 0; i < 4; i++) {
		if (!m_map[i])
			continue;

		struct dma_buf_sync sync = { DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
		ioctl(m_fd[i], DMA_BUF_IOCTL_SYNC, &sync);

		munmap(m_map[i], m_objSize[i]);
		m_map[i] = nullptr;
	}
}

/**
 * Copy the objects of the snapshot into allocated memory
 *
 * The caller has to free() the returned objects.
 *
 * @param[out] objects  the copied objects (malloc'ed)
 * @param[out] sizes    sizes of the copied objects
 * @return              true on success (nothing is allocated on failure)
 */
bool cFrameSnapshot::CopyObjects(uint8_t *objects[4], size_t sizes[4])
{
	for (int i = 0; i < 4; i++) {
		objects[i] = nullptr;
		sizes[i] = 0;
	}

	sMappedFrame frame;
	if (!Map(&frame))
		return false;

	for (int i = 0; i < m_numObjects; i++) {
		objects[i] = (uint8_t *)malloc(m_objSize[i]);
		if (!objects[i]) {
			LOGERROR("framesnapshot: %s: cannot allocate %zu bytes", __FUNCTION__, m_objSize[i]);
			for (int j = 0; j < i; j++) {
				free(objects[j]);
				objects[j] = nullptr;
				sizes[j] = 0;
			}
			Unmap();
			return false;
		}
		memcpy(objects[i], m_map[i], m_objSize[i]);
		sizes[i] = m_objSize[i];
	}

	Unmap();
	return true;
}

/** @} */
