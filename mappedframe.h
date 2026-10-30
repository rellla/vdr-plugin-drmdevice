// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mappedframe.h
 * Mapped Frame Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_MAPPEDFRAME_H
#define __DRMDEVICE_MAPPEDFRAME_H

#include <cstdint>

extern "C" {
#include <libavutil/pixfmt.h>
}

/**
 * A mapped (CPU readable) video frame
 *
 * Describes the memory layout of a frame as the drm buffer describes it:
 * pixel format (fourcc), format modifier and the planes. The plane pointers
 * already include the plane offsets, planeEnd points behind the last
 * readable byte of the object, the plane belongs to.
 *
 * @ingroup misc
 */
struct sMappedFrame {
	uint32_t width = 0;                 ///< frame width
	uint32_t height = 0;                ///< frame height
	uint32_t pixFmt = 0;                ///< drm fourcc
	uint64_t modifier = 0;              ///< drm format modifier
	int numPlanes = 0;                  ///< number of planes
	const uint8_t *plane[4] = {};       ///< start of the plane data
	const uint8_t *planeEnd[4] = {};    ///< end of the mapped object of the plane
	uint32_t pitch[4] = {};             ///< pitch of the plane (SAND: column height in lines)
	AVColorSpace colorspace = AVCOL_SPC_UNSPECIFIED; ///< color matrix of the frame
	AVColorRange colorRange = AVCOL_RANGE_UNSPECIFIED; ///< color range of the frame
	AVColorTransferCharacteristic colorTrc = AVCOL_TRC_UNSPECIFIED; ///< transfer function of the frame (PQ, HLG: HDR)
	AVColorPrimaries colorPrimaries = AVCOL_PRI_UNSPECIFIED; ///< color primaries of the frame
};

#endif
