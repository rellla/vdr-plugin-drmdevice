// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file test_edid.cpp
 * Unit tests for the EDID parser
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "edid.h"

namespace {

/**
 * Build an EDID with one CTA-861 extension block
 *
 * @param dataBlocks    data blocks of the extension block
 */
std::vector<uint8_t> MakeEdid(const std::vector<uint8_t> &dataBlocks)
{
	std::vector<uint8_t> edid(256, 0);
	const uint8_t header[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	std::copy(std::begin(header), std::end(header), edid.begin());
	edid[126] = 1;                         // one extension block

	uint8_t *cta = &edid[128];
	cta[0] = 0x02;                         // CTA-861 extension
	cta[1] = 0x03;                         // revision
	cta[2] = 4 + dataBlocks.size();        // offset of the detailed timing descriptors
	std::copy(dataBlocks.begin(), dataBlocks.end(), cta + 4);

	return edid;
}

}

TEST_CASE("EdidHdrEotfs", "[edid]") {
	// video data block (tag 2, length 2) and audio data block (tag 1, length 3) before the HDR block
	const std::vector<uint8_t> otherBlocks = { 0x42, 0x10, 0x1f, 0x23, 0x09, 0x07, 0x07 };

	SECTION("SDR, PQ and HLG") {
		std::vector<uint8_t> blocks = otherBlocks;
		blocks.insert(blocks.end(), { 0xe3, 0x06, 0x0d, 0x01 });   // extended tag 6, EOTFs 0x0d
		auto edid = MakeEdid(blocks);
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == (EDID_EOTF_SDR | EDID_EOTF_PQ | EDID_EOTF_HLG));
	}

	SECTION("only PQ") {
		auto edid = MakeEdid({ 0xe3, 0x06, 0x05, 0x01 });
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == (EDID_EOTF_SDR | EDID_EOTF_PQ));
	}

	SECTION("the reserved bits are ignored") {
		auto edid = MakeEdid({ 0xe3, 0x06, 0xf1, 0x01 });
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == EDID_EOTF_SDR);
	}

	SECTION("no HDR static metadata data block") {
		auto edid = MakeEdid(otherBlocks);
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == 0);
	}

	SECTION("another extended data block") {
		auto edid = MakeEdid({ 0xe2, 0x05, 0x03 });              // colorimetry data block
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == 0);
	}

	SECTION("no extension block") {
		std::vector<uint8_t> edid(128, 0);
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == 0);
	}

	SECTION("a missing extension block isn't read") {
		auto edid = MakeEdid({ 0xe3, 0x06, 0x0d, 0x01 });
		edid.resize(128);
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == 0);
	}

	SECTION("a data block beyond the data block collection isn't read") {
		auto edid = MakeEdid({ 0xe3, 0x06, 0x0d, 0x01 });
		edid[128 + 2] = 6;                                      // the collection ends within the HDR block
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == 0);
	}

	SECTION("too short or no EDID") {
		std::vector<uint8_t> edid(100, 0);
		REQUIRE(EdidHdrEotfs(edid.data(), edid.size()) == 0);
		REQUIRE(EdidHdrEotfs(nullptr, 256) == 0);
	}
}
