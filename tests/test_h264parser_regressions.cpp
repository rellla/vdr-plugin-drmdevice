/**
 * @file test_h264parser_regressions.cpp
 * Regression tests for fixed bugs of cH264Parser
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "../h264parser.h"
#include "h264streamwriter.h"

using namespace h264stream;
TEST_CASE("cH264Parser - SPS: 1080p with cropping", "[h264]") {
	// Regression test: the vertical crop unit was always doubled, 1080p was parsed as 1072 lines
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SPS, CreateSps(120, 68, true, 4, 0)); // 1920x1088 with 8 lines cropped (crop unit 2)

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, -4, -1, -1);

	REQUIRE(parser.GetWidth() == 1920);
	REQUIRE(parser.GetHeight() == 1080);

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - Truncated slice header", "[h264]") {
	// Regression test: a truncated ref_pic_list_modification() resulted in an endless loop
	// (ReadExponentialGolombCode() returns 0 at the end of the data, which is a valid idc)
	cBitWriter w;
	w.Ue(0);       // first_mb_in_slice
	w.Ue(5);       // slice_type P
	w.Ue(0);       // pic_parameter_set_id
	w.Bits(0, 4);  // frame_num
	w.Bit(0);      // num_ref_idx_active_override_flag
	w.Bit(1);      // ref_pic_list_modification_flag_l0, but no data follows
	auto rbsp = w.Data();

	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SLICE_REF, rbsp);

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, 0, 0, 0);

	REQUIRE(parser.IsPSlice());
	REQUIRE(parser.HasParseError());
	REQUIRE(parser.GetRefMods().size() <= 33);

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - Long-term modifications are skipped", "[h264]") {
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SLICE_REF, CreateSliceHeader(0, false, 3, 4, { { 2, 5 }, { 0, 1 } }));

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, 0, 0, 0);

	auto &mods = parser.GetRefMods();
	REQUIRE(mods.size() == 1);
	REQUIRE(mods[0].idc == 0);
	REQUIRE(mods[0].abs_diff_pic_num_minus1 == 1);
	REQUIRE_FALSE(parser.HasParseError());

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - NAL units are separated", "[h264]") {
	// the slice header is parsed from the slice NAL unit only, even if more NAL units follow
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SLICE_REF, CreateSliceHeader(0, false, 9, 4));
	AppendNal(stream, NAL_SLICE_REF, CreateSliceHeader(0, false, 9, 4, {}, 100));

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, 0, 0, 0);

	REQUIRE(parser.GetFrameNum() == 9);
	REQUIRE_FALSE(parser.HasParseError());

	av_packet_free(&pkt);
}
