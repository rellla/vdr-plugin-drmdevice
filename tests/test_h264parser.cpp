/**
 * @file test_h264parser.cpp
 * Unit tests for cH264Parser
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "../h264parser.h"
#include "h264streamwriter.h"

using namespace h264stream;

TEST_CASE("cH264Parser - SPS: 720p", "[h264]") {
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_AUD, { 0x10 });
	AppendNal(stream, NAL_SPS, CreateSps(80, 45, true, 0, 0));
	AppendNal(stream, NAL_PPS, CreatePps(1, 1));
	AppendNal(stream, NAL_IDR, CreateSliceHeader(2, true, 0, 4));

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, -4, -1, -1);

	REQUIRE(parser.HasSPS());
	REQUIRE(parser.HasPPS());
	REQUIRE(parser.GetWidth() == 1280);
	REQUIRE(parser.GetHeight() == 720);
	REQUIRE_FALSE(parser.IsMbaff());
	REQUIRE(parser.GetLog2MaxFrameNumMinus4() == 0);
	REQUIRE(parser.IsIDR());
	REQUIRE(parser.IsISlice());
	REQUIRE(parser.IsReference());
	REQUIRE(parser.GetFrameNum() == 0);
	REQUIRE(parser.GetNalUnitString().find("AUD") != std::string::npos);
	REQUIRE(parser.GetNalUnitString().find("SPS") != std::string::npos);

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - SPS: 1080i (field coding)", "[h264]") {
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SPS, CreateSps(120, 34, false, 2, 5)); // 1920x1088 with 8 lines cropped (crop unit 4)

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, -4, -1, -1);

	REQUIRE(parser.HasSPS());
	REQUIRE(parser.GetWidth() == 1920);
	REQUIRE(parser.GetHeight() == 1080);
	REQUIRE(parser.IsMbaff());
	REQUIRE(parser.GetLog2MaxFrameNumMinus4() == 5);

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - PPS", "[h264]") {
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_PPS, CreatePps(3, 2));

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, 0, -1, -1);

	REQUIRE_FALSE(parser.HasSPS());
	REQUIRE(parser.HasPPS());
	REQUIRE(parser.GetPpsNumRefIdxL0DefaultActiveMinus1() == 2);
	REQUIRE(parser.GetPpsNumRefIdxL1DefaultActiveMinus1() == 1);

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - Slices with previously parsed SPS/PPS values", "[h264]") {
	const int log2MaxFrameNumMinus4 = 2; // frame_num has 6 bits

	SECTION("Reference P-slice with modifications") {
		std::vector<uint8_t> stream;
		AppendNal(stream, NAL_SLICE_REF, CreateSliceHeader(0, false, 42, 6, { { 0, 1 }, { 1, 4 } }));

		AVPacket *pkt = CreatePacket(stream);
		cH264Parser parser(pkt, log2MaxFrameNumMinus4, 0, 0);

		REQUIRE(parser.IsPSlice());
		REQUIRE_FALSE(parser.IsIDR());
		REQUIRE(parser.IsReference());
		REQUIRE(parser.GetFrameNum() == 42);
		REQUIRE(parser.GetNumRefIdxL0Active() == 1);

		auto &mods = parser.GetRefMods();
		REQUIRE(mods.size() == 2);
		REQUIRE(mods[0].list == 0);
		REQUIRE(mods[0].idc == 0);
		REQUIRE(mods[0].abs_diff_pic_num_minus1 == 1);
		REQUIRE(mods[1].idc == 1);
		REQUIRE(mods[1].abs_diff_pic_num_minus1 == 4);

		av_packet_free(&pkt);
	}

	SECTION("Non-reference B-slice") {
		std::vector<uint8_t> stream;
		AppendNal(stream, NAL_SLICE_NOREF, CreateSliceHeader(1, false, 7, 6));

		AVPacket *pkt = CreatePacket(stream);
		cH264Parser parser(pkt, log2MaxFrameNumMinus4, 1, 0);

		REQUIRE(parser.IsBSlice());
		REQUIRE_FALSE(parser.IsReference());
		REQUIRE(parser.GetFrameNum() == 7);
		REQUIRE(parser.GetNumRefIdxL0Active() == 2);
		REQUIRE(parser.GetNumRefIdxL1Active() == 1);
		REQUIRE(parser.GetRefMods().empty());

		av_packet_free(&pkt);
	}
}

TEST_CASE("cH264Parser - Invalid references", "[h264]") {
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SLICE_REF, CreateSliceHeader(0, false, 10, 4));

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, 0, 0, 0);

	REQUIRE_FALSE(parser.HasInvalidReferences());

	parser.AddValidReference(9);
	REQUIRE_FALSE(parser.HasInvalidReferences());

	parser.AddInvalidReference(12, 10); // forward reference
	REQUIRE(parser.HasInvalidReferences());
	REQUIRE_FALSE(parser.HasInvalidBackwardReferences());

	parser.AddInvalidReference(8, 10);  // backward reference
	REQUIRE(parser.HasInvalidBackwardReferences());

	parser.BuildInvalidReferenceString(10);
	REQUIRE(parser.GetNalUnitString().find("!!! 8") != std::string::npos);

	av_packet_free(&pkt);
}

TEST_CASE("cH264Parser - Emulation prevention bytes are removed", "[h264]") {
	// first_mb_in_slice 2^23 - 1 is coded with 23 leading zero bits, which creates a 0x000001 sequence in the RBSP
	std::vector<uint8_t> stream;
	AppendNal(stream, NAL_SLICE_REF, CreateSliceHeader(0, false, 0x0001, 16, {}, (1 << 23) - 1));

	// the stream must contain an emulation prevention byte
	bool hasEmulationPrevention = false;
	for (size_t i = 6; i + 2 < stream.size(); i++) {
		if (stream[i] == 0 && stream[i + 1] == 0 && stream[i + 2] == 3)
			hasEmulationPrevention = true;
	}
	REQUIRE(hasEmulationPrevention);

	AVPacket *pkt = CreatePacket(stream);
	cH264Parser parser(pkt, 12, 0, 0);

	REQUIRE(parser.IsPSlice());
	REQUIRE(parser.GetFrameNum() == 1);

	av_packet_free(&pkt);
}
