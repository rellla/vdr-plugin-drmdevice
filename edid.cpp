// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edid.cpp
 * EDID Parser
 *
 * Reads the capabilities of the sink from its EDID.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include "edid.h"

namespace {

constexpr size_t EDID_BLOCK_SIZE = 128;                ///< size of an EDID block
constexpr int EDID_EXTENSION_COUNT = 126;              ///< offset of the number of extension blocks in the base block
constexpr uint8_t CTA_EXTENSION_TAG = 0x02;            ///< tag of a CTA-861 extension block
constexpr int CTA_DATA_BLOCK_EXTENDED = 7;             ///< tag code of an extended data block
constexpr uint8_t CTA_EXT_HDR_STATIC_METADATA = 0x06;  ///< extended tag code of the HDR static metadata data block

}

/**
 * Get the EOTFs, which the sink supports
 *
 * Searches the CTA-861 extension blocks for the HDR static metadata data block.
 *
 * @param edid      EDID of the sink
 * @param size      size of the EDID
 *
 * @return bit mask of the supported EOTFs (::EdidEotf), 0 if the sink doesn't report any
 */
int EdidHdrEotfs(const uint8_t *edid, size_t size)
{
	if (!edid || size < EDID_BLOCK_SIZE)
		return 0;

	size_t extensions = edid[EDID_EXTENSION_COUNT];

	for (size_t n = 1; n <= extensions && (n + 1) * EDID_BLOCK_SIZE <= size; n++) {
		const uint8_t *block = edid + n * EDID_BLOCK_SIZE;

		if (block[0] != CTA_EXTENSION_TAG)
			continue;

		// the data blocks are between byte 4 and the offset of the detailed timing descriptors
		size_t end = block[2];
		if (end < 4 || end > EDID_BLOCK_SIZE)
			end = EDID_BLOCK_SIZE;

		size_t pos = 4;
		while (pos < end) {
			int tag = block[pos] >> 5;
			size_t length = block[pos] & 0x1f;

			if (pos + 1 + length > end)
				break;

			// payload: extended tag code, supported EOTFs, supported static metadata descriptors
			if (tag == CTA_DATA_BLOCK_EXTENDED && length >= 2 && block[pos + 1] == CTA_EXT_HDR_STATIC_METADATA)
				return block[pos + 2] & (EDID_EOTF_SDR | EDID_EOTF_HDR | EDID_EOTF_PQ | EDID_EOTF_HLG);

			pos += 1 + length;
		}
	}

	return 0;
}
