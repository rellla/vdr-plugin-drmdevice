/**
 * @file h264streamwriter.h
 * Helpers to compose H.264 NAL units for the unit tests
 *
 * The NAL units are composed with a small bit writer, so every field
 * of the parsed headers is known.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_TESTS_H264STREAMWRITER_H
#define __DRMDEVICE_TESTS_H264STREAMWRITER_H

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace h264stream {

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
inline void AppendNal(std::vector<uint8_t> &stream, uint8_t nalHeader, const std::vector<uint8_t> &rbsp)
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
inline std::vector<uint8_t> CreateSps(int widthMbs, int heightMapUnits, bool frameMbsOnly, int cropBottom, int log2MaxFrameNumMinus4)
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
inline std::vector<uint8_t> CreatePps(int numRefIdxL0, int numRefIdxL1)
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
inline std::vector<uint8_t> CreateSliceHeader(int sliceType, bool idr, int frameNum, int frameNumBits, const std::vector<sRefMod> &mods = {}, uint32_t firstMb = 0)
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

inline AVPacket *CreatePacket(const std::vector<uint8_t> &data)
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

} // namespace h264stream

#endif
