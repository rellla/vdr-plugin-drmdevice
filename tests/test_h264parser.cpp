/**
 * @file test_h264parser.cpp
 * Unit tests for cH264Parser
 *
 * The NAL units are composed with a small bit writer, so every field
 * of the parsed headers is known.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "../h264parser.h"

namespace {

/**
 * Bit writer for H.264 RBSP data (including exp-golomb codes)
 */
class cBitWriter {
public:
	void Bit(int b) {
		if (m_bitPos == 0)
			m_data.push_back(0);
		if (b)
			m_data.back() |= 0x80 >> m_bitPos;
		m_bitPos = (m_bitPos + 1) % 8;
	}
	void Bits(uint32_t value, int n) {
		for (int i = n - 1; i >= 0; i--)
			Bit((value >> i) & 1);
	}
	void Ue(uint32_t value) {
		uint32_t v = value + 1;
		int len = 0;
		for (uint32_t t = v; t > 1; t >>= 1)
			len++;
		Bits(0, len);
		Bits(v, len + 1);
	}
	void Se(int32_t value) {
		Ue(value > 0 ? 2 * value - 1 : -2 * value);
	}
	void Trailing(void) {
		Bit(1);
		while (m_bitPos)
			Bit(0);
	}
	const std::vector<uint8_t> &Data(void) const { return m_data; }
private:
	std::vector<uint8_t> m_data;
	int m_bitPos = 0;
};

/**
 * Append a NAL unit with start code and emulation prevention to a stream
 */
void AppendNal(std::vector<uint8_t> &stream, uint8_t nalHeader, const std::vector<uint8_t> &rbsp)
{
	stream.insert(stream.end(), { 0x00, 0x00, 0x00, 0x01, nalHeader });

	int zeros = 0;
	for (uint8_t byte : rbsp) {
		if (zeros == 2 && byte <= 0x03) {
			stream.push_back(0x03);
			zeros = 0;
		}
		stream.push_back(byte);
		zeros = byte == 0 ? zeros + 1 : 0;
	}
}

/**
 * Write a SPS (High profile, 4:2:0)
 */
std::vector<uint8_t> CreateSps(int widthMbs, int heightMapUnits, bool frameMbsOnly, int cropBottom, int log2MaxFrameNumMinus4)
{
	cBitWriter w;
	w.Bits(100, 8);            // profile_idc (High)
	w.Bits(0, 8);              // constraint flags
	w.Bits(40, 8);             // level_idc
	w.Ue(0);                   // seq_parameter_set_id
	w.Ue(1);                   // chroma_format_idc (4:2:0)
	w.Ue(0);                   // bit_depth_luma_minus8
	w.Ue(0);                   // bit_depth_chroma_minus8
	w.Bit(0);                  // qpprime_y_zero_transform_bypass_flag
	w.Bit(0);                  // seq_scaling_matrix_present_flag
	w.Ue(log2MaxFrameNumMinus4);
	w.Ue(0);                   // pic_order_cnt_type
	w.Ue(4);                   // log2_max_pic_order_cnt_lsb_minus4
	w.Ue(4);                   // max_num_ref_frames
	w.Bit(0);                  // gaps_in_frame_num_value_allowed_flag
	w.Ue(widthMbs - 1);        // pic_width_in_mbs_minus1
	w.Ue(heightMapUnits - 1);  // pic_height_in_map_units_minus1
	w.Bit(frameMbsOnly);       // frame_mbs_only_flag
	if (!frameMbsOnly)
		w.Bit(1);              // mb_adaptive_frame_field_flag
	w.Bit(1);                  // direct_8x8_inference_flag
	w.Bit(cropBottom ? 1 : 0); // frame_cropping_flag
	if (cropBottom) {
		w.Ue(0);               // left
		w.Ue(0);               // right
		w.Ue(0);               // top
		w.Ue(cropBottom);      // bottom
	}
	w.Bit(0);                  // vui_parameters_present_flag
	w.Trailing();
	return w.Data();
}

/**
 * Write a PPS
 */
std::vector<uint8_t> CreatePps(int numRefIdxL0, int numRefIdxL1)
{
	cBitWriter w;
	w.Ue(0);                   // pic_parameter_set_id
	w.Ue(0);                   // seq_parameter_set_id
	w.Bit(1);                  // entropy_coding_mode_flag
	w.Bit(0);                  // bottom_field_pic_order_in_frame_present_flag
	w.Ue(0);                   // num_slice_groups_minus1
	w.Ue(numRefIdxL0 - 1);     // num_ref_idx_l0_default_active_minus1
	w.Ue(numRefIdxL1 - 1);     // num_ref_idx_l1_default_active_minus1
	w.Bit(0);                  // weighted_pred_flag
	w.Bits(0, 2);              // weighted_bipred_idc
	w.Se(0);                   // pic_init_qp_minus26
	w.Se(0);                   // pic_init_qs_minus26
	w.Se(0);                   // chroma_qp_index_offset
	w.Bit(1);                  // deblocking_filter_control_present_flag
	w.Bit(0);                  // constrained_intra_pred_flag
	w.Bit(0);                  // redundant_pic_cnt_present_flag
	w.Trailing();
	return w.Data();
}

struct sRefMod {
	int idc;
	int absDiffPicNumMinus1;
};

/**
 * Write the beginning of a slice header (up to the ref pic list modification)
 */
std::vector<uint8_t> CreateSliceHeader(int sliceType, bool idr, int frameNum, int frameNumBits, const std::vector<sRefMod> &mods = {}, uint32_t firstMb = 0)
{
	cBitWriter w;
	w.Ue(firstMb);             // first_mb_in_slice
	w.Ue(sliceType + 5);       // slice_type (all slices of the picture have the same type)
	w.Ue(0);                   // pic_parameter_set_id
	w.Bits(frameNum, frameNumBits);
	if (idr)
		w.Ue(0);               // idr_pic_id
	if (sliceType == 0 || sliceType == 1) {
		w.Bit(0);              // num_ref_idx_active_override_flag
		w.Bit(!mods.empty());  // ref_pic_list_modification_flag_l0
		if (!mods.empty()) {
			for (auto &m : mods) {
				w.Ue(m.idc);
				w.Ue(m.absDiffPicNumMinus1);
			}
			w.Ue(3);           // end of the list
		}
		if (sliceType == 1)
			w.Bit(0);          // ref_pic_list_modification_flag_l1
	}
	w.Trailing();
	return w.Data();
}

AVPacket *CreatePacket(const std::vector<uint8_t> &data)
{
	AVPacket *pkt = av_packet_alloc();
	REQUIRE(pkt);
	REQUIRE(av_new_packet(pkt, data.size()) == 0);
	memcpy(pkt->data, data.data(), data.size());
	return pkt;
}

constexpr uint8_t NAL_SLICE_REF = 0x41;   // nal_ref_idc 2, non-IDR slice
constexpr uint8_t NAL_SLICE_NOREF = 0x01; // nal_ref_idc 0, non-IDR slice
constexpr uint8_t NAL_IDR = 0x65;
constexpr uint8_t NAL_SPS = 0x67;
constexpr uint8_t NAL_PPS = 0x68;
constexpr uint8_t NAL_AUD = 0x09;

} // namespace

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
