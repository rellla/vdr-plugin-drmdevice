// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file drmbuffer.cpp
 * DRM Buffer
 *
 * This files defines cDrmBuffer, which is a class used to describe
 * a DRM buffer, keeping framebuffer and prime handles to be used
 * by the kernel display interface.
 *
 * @copyright 2018 by zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
}

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drmbuffer.h"
#include "logger.h"
#include "pool.h"

/*****************************************************************************
 * Drm Buffer
 ****************************************************************************/

/**
 * Create a new drm buffer
 *
 * as an empty buffer
 */
cDrmBuffer::cDrmBuffer(void)
{
}

#ifdef USE_GLES
/**
 * Create a new drm buffer
 *
 * from a gbm buffer object
 *
 * @param fdDrm          drm file descriptor
 * @param width          buffer width
 * @param height         buffer height
 * @param pixFmt         buffer pixel format
 * @param bo             pointer to gbm buffer object
 */
cDrmBuffer::cDrmBuffer(int fdDrm, uint32_t width, uint32_t height, uint32_t pixFmt, struct gbm_bo *bo)
	: m_width(width),
	  m_height(height),
	  m_pixFmt(pixFmt),
	  m_drmDeviceFd(fdDrm),
	  m_pBo(bo)
{
	m_numPlanes = 0;
	m_modifier = DRM_FORMAT_MOD_INVALID;

	for (int i = 0; i < 4; i++) {
		m_pPlane[i] = nullptr;
		m_planePrimeHandle[i] = 0;
		m_offset[i] = 0;
		m_pitch[i] = 0;
		m_size[i] = 0;
	}
	m_dirty = false;
}
#endif

/**
 * Clear and destroy the buffer object and its parameters
 */
void cDrmBuffer::Destroy(void)
{
	struct drm_mode_destroy_dumb dreq;
	LOGDEBUG2(L_DRM, "drmbuffer: %s: destroy FB %d DMA-BUF handle %d", __FUNCTION__, m_fbId, m_dmaBufHandle[0]);

	for (int i = 0; i < m_numPlanes; i++) {
		if (m_pPlane[i] && m_size[i]) {
			if (munmap(m_pPlane[i], m_size[i]))
				LOGERROR("drmbuffer: %s: failed unmap FB (%d): %m", __FUNCTION__, errno);
		}
	}

	if (drmModeRmFB(m_drmDeviceFd, m_fbId) < 0)
		LOGERROR("drmbuffer: %s: cannot rm FB (%d): %m", __FUNCTION__, errno);

	if (m_closeHandleOnDestroy && m_dmaBufHandle[0] && fcntl(m_dmaBufHandle[0], F_GETFD) != -1) { // the handle can be invalid in reverse trickspeed, because the decoder is rapidly reopened
		if (close(m_dmaBufHandle[0]))
			LOGERROR("drmbuffer: %s: error closing DMA-BUF handle %d (%d): %m", __FUNCTION__, m_dmaBufHandle[0], errno);
	}

	for (int i = 0; i < m_numPlanes; i++) {
		if (m_pPlane[i] && m_size[i]) {
			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = m_planePrimeHandle[i];

			if (drmIoctl(m_drmDeviceFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq) < 0)
				LOGERROR("drmbuffer: %s: cannot destroy dumb buffer (%d): %m", __FUNCTION__, errno);
			m_planePrimeHandle[i] = 0;

		}

		m_pPlane[i] = 0;
		m_size[i] = 0;
		m_pitch[i] = 0;
		m_offset[i] = 0;
		m_objIdx[i] = 0;
	}

	for (int i = 0; i < m_numObjects; i++) {
		if (m_objectPrimeHandle[i] && (drmIoctl(m_drmDeviceFd, DRM_IOCTL_GEM_CLOSE, &m_objectPrimeHandle[i]) < 0))
			LOGERROR("drmbuffer: %s: cannot close handle %d FB %d GEM (%d): %m", __FUNCTION__, m_objectPrimeHandle[i], m_fbId, errno);
		m_objectPrimeHandle[i] = 0;
		m_size[i] = 0;
	}
	m_numObjects = 0;
	m_dmaBufHandle[0] = 0;
	m_dmaBufInode = 0;

	m_width = 0;
	m_height = 0;
	m_fbId = 0;
	m_dirty = false;
	m_numPlanes = 0;
	m_destroyAfterUse = false;
	m_modifier = DRM_FORMAT_MOD_INVALID;
}

/**
 * Setup the buffer
 *
 * @param drmDeviceFd    drm device file descriptor
 * @param width          buffer width
 * @param height         buffer height
 * @param pixFmt         buffer pixel format
 * @param primedata      AVDRMFrameDescriptor or NULL (if this is a software buffer)
 * @param closeHandleOnDestroy  close the DMA-BUF handle in Destroy()
 *
 * @note If this is a software buffer (primedata == nullptr), pixFmt has to be either
 * DRM_FORMAT_NV12 or DRM_FORMAT_ARGB8888. A NV12 buffer is created with one single
 * buffer object for two planes with offsets.
 */
void cDrmBuffer::Setup(int drmDeviceFd, uint32_t width, uint32_t height, uint32_t pixFmt, AVDRMFrameDescriptor *primedata, bool closeHandleOnDestroy)
{
	uint64_t modifier[4] = { 0, 0, 0, 0 };
	uint32_t mod_flags = 0;
	m_planePrimeHandle[0] = m_planePrimeHandle[1] = m_planePrimeHandle[2] = m_planePrimeHandle[3] = 0;
	m_pitch[0] = m_pitch[1] = m_pitch[2] = m_pitch[3] = 0;
	m_offset[0] = m_offset[1] = m_offset[2] = m_offset[3] = 0;
	m_numObjects = m_numPlanes = 0;
	m_size[0] = m_size[1] = m_size[2] = m_size[3] = 0;

	m_width = width;
	m_height = height;
	m_pixFmt = pixFmt;
	m_drmDeviceFd = drmDeviceFd;
	m_closeHandleOnDestroy = closeHandleOnDestroy;

	if (primedata) {
		// we have no DRM objects yet, so return
		if (!primedata->nb_objects)
			LOGFATAL("drmbuffer: %s: No primedata objects available!", __FUNCTION__);

		AVDRMLayerDescriptor *layer = &primedata->layers[0];

		m_pixFmt = layer->format;
		m_numPlanes = layer->nb_planes;
		m_numObjects = primedata->nb_objects;

		//LOGDEBUG2(L_DRM, "drmbuffer: %s: PRIMEDATA %d x %d, pix_fmt %4.4s nb_planes %d nb_objects %d", __FUNCTION__,
		//	m_width, m_height, (char *)&m_pixFmt, m_numPlanes, m_numObjects);

		// create handles for PrimeFDs
		for (int object = 0; object < primedata->nb_objects; object++) {
			if (drmPrimeFDToHandle(drmDeviceFd, primedata->objects[object].fd, &m_objectPrimeHandle[object])) {
				LOGFATAL("drmbuffer: %s: PRIMEDATA Failed to retrieve the Prime Handle %i size %zu (%d): %m", __FUNCTION__,
					primedata->objects[object].fd,
					primedata->objects[object].size, errno);
			}

			m_dmaBufHandle[object] = primedata->objects[object].fd;
			m_size[object] = primedata->objects[object].size;
			if (object == 0)
				m_dmaBufInode = FdInode(primedata->objects[object].fd);
			//LOGDEBUG2(L_DRM, "drmbuffer: %s: PRIMEDATA create handle for PrimeFD (%d|%i): PrimeFD %i ToHandle %i size %zu modifier %" PRIx64 "",
			//	__FUNCTION__, object, primedata->nb_objects, primedata->objects[object].fd, m_primehandle[object],
			//	primedata->objects[object].size, primedata->objects[object].format_modifier);
		}

		// fill the planes
		for (int plane = 0; plane < layer->nb_planes; plane++) {
			int object = layer->planes[plane].object_index;
			uint32_t handle = m_objectPrimeHandle[object];
			if (handle) {
				m_planePrimeHandle[plane] = handle;
				m_pitch[plane] = layer->planes[plane].pitch;
				m_offset[plane] = layer->planes[plane].offset;
				m_objIdx[plane] = object;
				if (primedata->objects[object].format_modifier)
					modifier[plane] = primedata->objects[object].format_modifier;

				// LOGDEBUG2(L_DRM, "drmbuffer: %s: PRIMEDATA fill plane %d: handle %d object_index %i pitch %d offset %d size %d modifier %" PRIx64 " (plane not mapped!)",
				// 	__FUNCTION__, plane, m_planePrimeHandle[plane], m_objIdx[plane], m_pitch[plane], m_offset[plane], m_size[plane], modifier[plane]);
			}
		}
		if (modifier[0] && modifier[0] != DRM_FORMAT_MOD_INVALID)
			mod_flags = DRM_MODE_FB_MODIFIERS;

		m_modifier = modifier[0];
	} else {
		if (m_pixFmt != DRM_FORMAT_NV12 && m_pixFmt != DRM_FORMAT_ARGB8888)
			LOGFATAL("drmbuffer: %s: pixFmt is neither NV12 nor ARGB8888!", __FUNCTION__);

		//LOGDEBUG2(L_DRM, "drmbuffer: %s: %d x %d, pix_fmt %4.4s", __FUNCTION__, m_width, m_height, (char *)&m_pixFmt);

		struct drm_mode_create_dumb creq;
		if (m_pixFmt == DRM_FORMAT_NV12) {
			creq.height = m_height * 3 / 2;
			creq.bpp = 8;
		} else { // DRM_FORMAT_ARGB8888
			creq.height = m_height;
			creq.bpp = 32;
		}

		creq.width = m_width;
		creq.flags = 0;
		creq.handle = 0;
		creq.pitch = 0;
		creq.size = 0;

		if (drmIoctl(drmDeviceFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
			LOGFATAL("drmbuffer: %s: cannot create dumb buffer %dx%d@%d (%d): %m", __FUNCTION__, creq.width, creq.height, creq.bpp, errno);

		struct drm_mode_map_dumb mreq;
		memset(&mreq, 0, sizeof(struct drm_mode_map_dumb));
		mreq.handle = creq.handle;

		if (drmIoctl(drmDeviceFd, DRM_IOCTL_MODE_MAP_DUMB, &mreq))
			LOGFATAL("drmbuffer: %s: cannot prepare dumb buffer for mapping (%d): %m", __FUNCTION__, errno);

		uint8_t *map = (uint8_t *)mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drmDeviceFd, mreq.offset);
		if (map == MAP_FAILED)
			LOGFATAL("drmbuffer: %s: cannot map dumb buffer (%d): %m", __FUNCTION__, errno);

		m_planePrimeHandle[0] = creq.handle;
		m_pitch[0] = creq.pitch;
		m_offset[0] = 0;
		m_size[0] = creq.size;
		m_pPlane[0] = map;
		m_numPlanes = 1;

		//LOGDEBUG2(L_DRM, "drmbuffer: %s: fill plane 0: prime handle %d pitch %d offset %d size %d address %p", __FUNCTION__,
		//	m_planePrimeHandle[0], m_pitch[0], m_offset[0], m_size[0], m_pPlane[0]);

		// DRM_FORMAT_NV12 is always using a single buffer object for both planes
		if (m_pixFmt == DRM_FORMAT_NV12) {
			m_planePrimeHandle[1] = creq.handle;
			m_pitch[1] = creq.pitch;
			m_offset[1] = creq.pitch * m_height;
			m_size[1] = 0;   // shares the buffer object of plane 0
			m_pPlane[1] = map + m_offset[1];
			m_numPlanes++;
			//LOGDEBUG2(L_DRM, "drmbuffer: %s: fill plane 1: prime handle %d pitch %d offset %d size %d address %p", __FUNCTION__,
			//	m_planePrimeHandle[1], m_pitch[1], m_offset[1], m_size[1], m_pPlane[1]);
		}

		memset(m_pPlane[0], 0, m_size[0]);
	}

	int ret = -1;
	ret = drmModeAddFB2WithModifiers(drmDeviceFd, m_width, m_height, m_pixFmt, m_planePrimeHandle, m_pitch, m_offset, modifier, &m_fbId, mod_flags);

	if (ret) {
		if (mod_flags)
			LOGERROR("drmbuffer: %s: cannot create modifiers framebuffer (%d): %m", __FUNCTION__, errno);

		ret = drmModeAddFB2(drmDeviceFd, m_width, m_height, m_pixFmt,
			m_planePrimeHandle, m_pitch, m_offset, &m_fbId, 0);
	}

	if (ret)
		LOGFATAL("drmbuffer: %s: cannot create framebuffer (%d): %m", __FUNCTION__, errno);

	LOGDEBUG2(L_DRM, "drmbuffer: %s: Added %sFB fb_id %d width %d height %d pix_fmt %4.4s DMA-BUF handle %d", __FUNCTION__,
		primedata ? "primedata " : "", m_fbId, m_width, m_height, (char *)&m_pixFmt, m_dmaBufHandle[0]);

	m_dirty = true;
}

/**
 * Get the inode of a file descriptor
 *
 * Every DMA-BUF has its own inode, so it identifies the DMA-BUF, while the
 * number of its file descriptor may be reused for another one after it was
 * closed (e.g. when the decoder is reopened).
 *
 * @param fd    file descriptor
 *
 * @return      the inode (0: invalid file descriptor)
 */
ino_t cDrmBuffer::FdInode(int fd)
{
	struct stat st;

	if (fd < 0 || fstat(fd, &st))
		return 0;

	return st.st_ino;
}

/**
 * Color the buffer black
 */
void cDrmBuffer::FillBlack(void)
{
	// NV12: Y plane with height lines, interleaved UV plane with height / 2 lines,
	// the lines may be padded (pitch > width)
	for (uint32_t i = 0; i < m_height; ++i)
		memset(m_pPlane[0] + i * m_pitch[0], 0x10, m_width);

	for (uint32_t i = 0; i < m_height / 2; ++i)
		memset(m_pPlane[1] + i * m_pitch[1], 0x80, m_width);
}

/**
 * The presentation of this buffer has finished
 *
 * Destroys the buffer, if it is not needed anymore (m_destroyAfterUse == true)
 */
void cDrmBuffer::PresentationFinished(void)
{
	av_frame_free(&frame);
	m_presentationPending = false;

	if (m_destroyAfterUse)
		Destroy();
}

/*****************************************************************************
 * Drm Buffer Pool
 ****************************************************************************/

/**
 * Find a drm buffer from the buffer pool by a given prime handle
 *
 * The buffer must have imported the same DMA-BUF: after the decoder was reopened,
 * a new DMA-BUF may get the file descriptor number of an old one. The buffer of
 * the old one still holds its framebuffer, which would show the old frame. Then
 * the buffers of the closed DMA-BUFs are destroyed (DestroyStaleImports()).
 *
 * @param primeFd   DMA-BUF file descriptor of the first object of the frame
 *
 * @return          the buffer (nullptr, if there is none)
 */
cDrmBuffer *cDrmBufferPool::FindByDmaBufHandle(int primeFd)
{
	for (const auto &buf : buffer) {
		if (buf->IsDirty() && buf->DmaBufHandle() == primeFd) {
			if (!buf->DmaBufInode() || buf->DmaBufInode() == cDrmBuffer::FdInode(primeFd))
				return buf.get();

			int destroyed = DestroyStaleImports();
			LOGDEBUG2(L_DRM, "drmbuffer: %s: DMA-BUF handle %d was reused, destroyed %d stale buffers", __FUNCTION__, primeFd, destroyed);
			return nullptr;
		}
	}

	return nullptr;
}

/**
 * Destroy the buffers, whose imported DMA-BUF was closed
 *
 * The DMA-BUF of such a buffer was released by the decoder (e.g. it was reopened),
 * its file descriptor is closed or belongs to another DMA-BUF now. A buffer, which
 * is queued or displayed (presentation pending), keeps its DMA-BUF open, so it is
 * never destroyed here.
 *
 * @return          number of the destroyed buffers
 */
int cDrmBufferPool::DestroyStaleImports(void)
{
	int destroyed = 0;

	for (const auto &buf : buffer) {
		if (!buf->IsDirty() || !buf->DmaBufInode() || buf->IsPresentationPending())
			continue;

		if (buf->DmaBufInode() != cDrmBuffer::FdInode(buf->DmaBufHandle())) {
			av_frame_free(&buf->frame);
			buf->Destroy();
			destroyed++;
		}
	}

	return destroyed;
}

/**
 * Find a clean drm buffer from the buffer pool
 */
cDrmBuffer *cDrmBufferPool::FindUninitilized()
{
	for (const auto &buf : buffer) {
		if (!buf->IsDirty())
			return buf.get();
	}

	return nullptr;
}

/**
 * Find a dirty drm buffer from the buffer pool which presentation has finished
 */
cDrmBuffer *cDrmBufferPool::FindNoPresentationPending()
{
	for (const auto &buf : buffer) {
		if (buf->IsDirty() && !buf->IsPresentationPending())
			return buf.get();
	}

	return nullptr;
}

/**
 * Destroy all drm buffers except the given one
 *
 * @param exceptBuf      the buffer, which should be kept alive
 *                       if nullptr is given, all buffers are destroyed
 */
void cDrmBufferPool::DestroyAllExcept(cDrmBuffer *exceptBuf)
{
	for (const auto &buf : buffer) {
		if (buf.get() != exceptBuf && buf->IsDirty()) {
			av_frame_free(&buf->frame);
			buf->Destroy();
		}
	}
}
