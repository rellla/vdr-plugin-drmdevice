// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file framesnapshot.h
 * Frame Snapshot Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_FRAMESNAPSHOT_H
#define __DRMDEVICE_FRAMESNAPSHOT_H

#include <cstddef>
#include <cstdint>

#include <vdr/osd.h>

#include "mappedframe.h"

class cDrmBuffer;
struct AVFrame;

/**
 * Snapshot of a displayed drm buffer
 *
 * Taking a snapshot only duplicates the DMA-BUF file descriptors of the
 * buffer and copies its layout. This is cheap (a few syscalls) and can be
 * done in the display thread. The snapshot is independent of the drm buffer:
 * the duplicated descriptors keep the memory alive, even if the buffer is
 * destroyed or the decoder releases the frame meanwhile.
 *
 * The expensive part, mapping and reading the memory, is done later by the
 * user of the snapshot in its own thread.
 *
 * The duplicated descriptors keep only the memory, not the content: the
 * decoder may write a new frame into the same DMA-BUF, as soon as the frame
 * is released. To keep the content, the snapshot can hold a reference to the
 * AVFrame of the buffer (keepFrame): the decoder gets the DMA-BUF back only
 * after Release(). This works for hardware decoded frames. The dumb buffer of
 * a software decoded frame is reused by the renderer independently of its
 * AVFrame, its content can't be kept.
 *
 * @ingroup misc
 */
class cFrameSnapshot {
public:
	cFrameSnapshot(void) = default;
	~cFrameSnapshot(void) { Release(); }
	cFrameSnapshot(const cFrameSnapshot &) = delete;
	cFrameSnapshot &operator=(const cFrameSnapshot &) = delete;

	static bool CanTake(cDrmBuffer *);
	bool Take(cDrmBuffer *, bool);
	void Release(void);
	void Swap(cFrameSnapshot &);
	bool IsValid(void) const { return m_numObjects > 0; }       ///< true, if the snapshot holds a buffer

	bool Map(sMappedFrame *);
	void Unmap(void);
	bool CopyObjects(uint8_t *[4], size_t[4]);

	// layout
	uint32_t Width(void) const { return m_width; }              ///< buffer width
	uint32_t Height(void) const { return m_height; }            ///< buffer height
	uint32_t PixFmt(void) const { return m_pixFmt; }            ///< drm fourcc
	uint64_t Modifier(void) const { return m_modifier; }        ///< drm format modifier
	int NumPlanes(void) const { return m_numPlanes; }           ///< number of planes
	int NumObjects(void) const { return m_numObjects; }         ///< number of DMA-BUF objects
	int ObjectIndex(int plane) const { return m_objIdx[plane]; }///< object of a plane
	uint32_t Offset(int plane) const { return m_offset[plane]; }///< offset of a plane in its object
	uint32_t Pitch(int plane) const { return m_pitch[plane]; }  ///< pitch of a plane

	// presentation
	int FbId(void) const { return m_fbId; }                     ///< drm framebuffer id at the time of the snapshot
	int64_t Pts(void) const { return m_pts; }                   ///< pts of the frame (AV_NOPTS_VALUE, if unknown)
	const cRect &ScreenRect(void) const { return m_screenRect; }///< position of the buffer on the screen
	const cRect &CropRect(void) const { return m_cropRect; }    ///< shown part of the buffer (empty: all)
	AVColorSpace ColorSpace(void) const { return m_colorspace; }///< color matrix of the frame
	AVColorRange ColorRange(void) const { return m_colorRange; }///< color range of the frame

private:
	uint32_t m_width = 0;               ///< buffer width
	uint32_t m_height = 0;              ///< buffer height
	uint32_t m_pixFmt = 0;              ///< drm fourcc
	uint64_t m_modifier = 0;            ///< drm format modifier
	int m_numPlanes = 0;                ///< number of planes
	int m_objIdx[4] = {};               ///< object index of the planes
	uint32_t m_offset[4] = {};          ///< offset of the planes
	uint32_t m_pitch[4] = {};           ///< pitch of the planes

	int m_numObjects = 0;               ///< number of objects
	int m_fd[4] = {-1, -1, -1, -1};     ///< duplicated DMA-BUF descriptors of the objects
	size_t m_objSize[4] = {};           ///< sizes of the objects
	void *m_map[4] = {};                ///< mapped objects (while mapped)
	AVFrame *m_pFrame = nullptr;        ///< reference to the decoded frame (keepFrame), keeps the decoder from reusing the DMA-BUF

	int m_fbId = 0;                     ///< framebuffer id
	int64_t m_pts = 0;                  ///< pts of the frame
	cRect m_screenRect;                 ///< position on the screen
	cRect m_cropRect;                   ///< shown part of the buffer
	AVColorSpace m_colorspace = AVCOL_SPC_UNSPECIFIED; ///< color matrix
	AVColorRange m_colorRange = AVCOL_RANGE_UNSPECIFIED; ///< color range
};

#endif
