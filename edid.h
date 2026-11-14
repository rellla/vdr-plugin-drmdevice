// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edid.h
 * EDID Parser Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_EDID_H
#define __DRMDEVICE_EDID_H

#include <cstddef>
#include <cstdint>

/**
 * @addtogroup drm
 * @{
 */

/**
 * EOTFs of the HDR static metadata data block (CTA-861-G)
 *
 * The bit n corresponds to the EOTF value n of the HDR output metadata.
 */
enum EdidEotf {
	EDID_EOTF_SDR = 1 << 0,        ///< traditional gamma, SDR luminance range
	EDID_EOTF_HDR = 1 << 1,        ///< traditional gamma, HDR luminance range
	EDID_EOTF_PQ  = 1 << 2,        ///< SMPTE ST 2084 (PQ, HDR10)
	EDID_EOTF_HLG = 1 << 3,        ///< hybrid log-gamma
};

int EdidHdrEotfs(const uint8_t *, size_t);

/** @} */

#endif
