// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h264parser.h
 * H.264 Parser Header File
 *
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_H264PARSER_H
#define __DRMDEVICE_H264PARSER_H

#include <set>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

/**
 * H.264 Nal unit types
 *
 * @ingroup misc
 */
typedef enum {
	NALU_TYPE_NON_IDR = (1 << 0),  ///< coded slice of a non-IDR picture (type 1)
	NALU_TYPE_PART_A  = (1 << 1),  ///< coded slice data partition A (type 2)
	NALU_TYPE_PART_B  = (1 << 2),  ///< coded slice data partition B (type 3)
	NALU_TYPE_PART_C  = (1 << 3),  ///< coded slice data partition C (type 4)
	NALU_TYPE_IDR     = (1 << 4),  ///< coded slice of an IDR picture (type 5)
	NALU_TYPE_SEI     = (1 << 5),  ///< supplemental enhancement information (type 6)
	NALU_TYPE_SPS     = (1 << 6),  ///< sequence parameter set (type 7)
	NALU_TYPE_PPS     = (1 << 7),  ///< picture parameter set (type 8)
	NALU_TYPE_AUD     = (1 << 8)   ///< access unit delimiter (type 9)
} NalUnitTypes;

/**
 * H.264 Parser
 *
 * Parses the NAL units of an H.264 packet: the size from the SPS, the
 * default number of reference indices from the PPS and the slice header
 * (slice type, frame_num, reference picture list modifications). It is used
 * to log the stream start and to detect P-frames, which reference frames,
 * which were never received (e.g. after a channel switch).
 *
 * @ingroup misc
 */
class cH264Parser {
private:
	/** One reference picture list modification of a slice header */
	struct RefPicMod {
		int list;                      ///< reference picture list (0 or 1)
		int idc;                       ///< modification_of_pic_nums_idc
		int abs_diff_pic_num_minus1;   ///< abs_diff_pic_num_minus1 (idc 0 and 1)
		int long_term_pic_num;         ///< long_term_pic_num (idc 2)
	};

public:
	cH264Parser(AVPacket *, int, int, int);
	void BuildInvalidReferenceString(int);
	void BuildValidReferenceString(void);
	void AddFrameNumber(int);
	void AddInvalidReference(int, int);
	void AddValidReference(int);
	void PrintNalUnits(void);
	void PrintStreamData(void);

	std::string GetNalUnitString(void) { return m_naluString; }                        ///< get the NAL units of the packet as string (for logging)
	int GetWidth(void) { return m_width; }                                             ///< width from the SPS
	int GetHeight(void) { return m_height; }                                           ///< height from the SPS
	bool IsMbaff(void) { return m_mbaff; }                                             ///< true, if the SPS has mb_adaptive_frame_field_flag set
	bool HasSPS(void) { return m_hasSPS; }                                             ///< true, if the packet contains a SPS
	bool HasPPS(void) { return m_hasPPS; }                                             ///< true, if the packet contains a PPS
	bool IsPSlice() const { return m_sliceType == 0; }                                 ///< true, if the slice is a P-slice
	bool IsBSlice() const { return m_sliceType == 1; }                                 ///< true, if the slice is a B-slice
	bool IsISlice() const { return m_sliceType == 2; }                                 ///< true, if the slice is an I-slice
	bool IsIDR() const { return m_isIDR; }                                             ///< true, if the slice belongs to an IDR picture
	bool IsReference() const { return m_isReference; }                                 ///< true, if the slice is used as reference (nal_ref_idc != 0)
	int GetFrameNum() const { return m_frameNum; }                                     ///< frame_num of the slice
	const std::vector<RefPicMod>& GetRefMods() const { return m_refMods; }             ///< reference picture list modifications of the slice
	int GetLog2MaxFrameNumMinus4() const { return m_log2MaxFrameNumMinus4; }           ///< log2_max_frame_num_minus4 of the SPS
	/** num_ref_idx_l0_default_active_minus1 of the PPS */
	int GetPpsNumRefIdxL0DefaultActiveMinus1(void) { return m_ppsNumRefIdxL0DefaultActiveMinus1; }
	/** num_ref_idx_l1_default_active_minus1 of the PPS */
	int GetPpsNumRefIdxL1DefaultActiveMinus1(void) { return m_ppsNumRefIdxL1DefaultActiveMinus1; }
	int GetNumRefIdxL0Active(void) { return m_numRefIdxL0Active; }                     ///< number of active references in list 0
	int GetNumRefIdxL1Active(void) { return m_numRefIdxL1Active; }                     ///< number of active references in list 1
	bool HasInvalidReferences(void) { return m_hasInvalidReferences; }                 ///< true, if the slice references a missing frame
	bool HasInvalidBackwardReferences(void) { return m_hasInvalidBackwardReferences; } ///< true, if the slice references a missing older frame (frame_num smaller than the current one)
	bool HasParseError(void) const { return m_parseError; }                            ///< true, if the parsing failed (e.g. corrupted data)

private:
	AVPacket *m_pAvpkt;            ///< the parsed packet
	const unsigned char *m_pStart; ///< start of the RBSP data, which is read
	std::vector<uint8_t> m_rbsp;   ///< RBSP data of the current NAL unit (without emulation prevention bytes)
	int m_nLength;                 ///< length of the RBSP data in bytes
	int m_nCurrentBit;             ///< current bit position in the RBSP data

	int m_nalutype = 0;                                 ///< NAL unit types of the packet (bit mask of ::NalUnitTypes)
	int m_width = 0;                                    ///< width from the SPS
	int m_height = 0;                                   ///< height from the SPS
	bool m_hasSPS = false;                              ///< packet contains a SPS
	bool m_hasPPS = false;                              ///< packet contains a PPS
	bool m_isIDR = false;                               ///< slice belongs to an IDR picture
	bool m_isReference = false;                         ///< slice is used as reference
	bool m_mbaff = false;                               ///< mb_adaptive_frame_field_flag of the SPS
	bool m_parseError = false;                          ///< set, if the parsing failed

	std::string m_naluString;                           ///< NAL units of the packet as string (for logging)

	int m_sliceType = -1;                               ///< normalized slice type: 0=P, 1=B, 2=I, 3=SP, 4=SI (-1 = no slice)
	int m_frameNum = -1;                                ///< frame_num of the slice
	int m_nalRefIdc = 0;                                ///< nal_ref_idc of the slice
	std::set<int> m_invalidReferences;                  ///< referenced frame numbers, which are missing
	std::set<int> m_validReferences;                    ///< referenced frame numbers, which are available
	std::vector<RefPicMod> m_refMods;                   ///< reference picture list modifications of the slice
	bool m_hasInvalidReferences = false;                ///< slice references a missing frame
	bool m_hasInvalidBackwardReferences = false;        ///< slice references a missing older frame
	bool m_hasValidReferences = false;                  ///< slice references an available frame

	int m_log2MaxFrameNumMinus4 = -4;             ///< saved from SPS
	int m_ppsNumRefIdxL0DefaultActiveMinus1 = -1; ///< saved from PPS
	int m_ppsNumRefIdxL1DefaultActiveMinus1 = -1; ///< saved from PPS
	int m_numRefIdxL0Active = 0;                  ///< number of active references in list 0
	int m_numRefIdxL1Active = 0;                  ///< number of active references in list 1

	unsigned int ReadBit(void);
	unsigned int ReadBits(int);
	unsigned int ReadExponentialGolombCode(void);
	unsigned int ReadSE(void);
	int GetSPSOffset(void);
	int GetPPSOffset(void);
	int GetSliceOffset(void);
	void ConvertEBSPtoRBSP(const uint8_t *, int);
	void ConvertNalUnitToRBSP(int);
	void ReadRefPicListModifications(int);
};

#endif
