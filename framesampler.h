// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file framesampler.h
 * Frame Sampler Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_FRAMESAMPLER_H
#define __DRMDEVICE_FRAMESAMPLER_H

#include <cstddef>
#include <cstdint>

#include "mappedframe.h"

/**
 * Sample a frame into a small RGB24 image
 *
 * The frame is not converted completely. Only a few pixels per destination
 * pixel are read and averaged, so that only a fraction of the frame has to be
 * touched. This is important, because the frames are usually in uncached
 * memory (dmabuf), where every read is expensive.
 *
 * @ingroup misc
 */
class cFrameSampler {
public:
	static bool IsSupported(uint32_t pixFmt, uint64_t modifier);
	static const char *FormatName(uint32_t pixFmt, uint64_t modifier);
	static const char *ToneMapName(AVColorTransferCharacteristic, AVColorPrimaries);

	bool Sample(const sMappedFrame &frame,
	            int srcX, int srcY, int srcW, int srcH,
	            uint8_t *dst, int dstStride, int dstW, int dstH);

	/** number of samples per destination pixel in each direction */
	void SetSamplesPerPixel(int n) { m_samplesPerPixel = n < 1 ? 1 : (n > 4 ? 4 : n); }

private:
	int m_samplesPerPixel = 2;          ///< samples per destination pixel in each direction
};

#endif
