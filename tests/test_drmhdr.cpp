// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file test_drmhdr.cpp
 * Unit tests for cHdrMetadata
 */

#include <catch2/catch_test_macros.hpp>

#include "drmhdr.h"

namespace {

// EOTF of the HDR output metadata (CTA-861-G)
constexpr int EOTF_SDR = 0;
constexpr int EOTF_PQ = 2;
constexpr int EOTF_HLG = 3;

// a chromaticity coordinate in units of 0.00002
constexpr uint16_t Xy(double value) { return static_cast<uint16_t>(value * 50000 + 0.5); }

/** Side data of a frame with mastering display metadata (DCI-P3 primaries, D65) */
struct sMasteringData {
	AVMasteringDisplayMetadata metadata{};
	AVFrameSideData sideData{};

	sMasteringData(int maxLuminance, AVRational minLuminance)
	{
		metadata.display_primaries[0][0] = {34000, 50000};
		metadata.display_primaries[0][1] = {16000, 50000};
		metadata.display_primaries[1][0] = {13250, 50000};
		metadata.display_primaries[1][1] = {34500, 50000};
		metadata.display_primaries[2][0] = {7500, 50000};
		metadata.display_primaries[2][1] = {3000, 50000};
		metadata.white_point[0] = {15635, 50000};
		metadata.white_point[1] = {16450, 50000};
		metadata.max_luminance = {maxLuminance, 1};
		metadata.min_luminance = minLuminance;
		metadata.has_primaries = 1;
		metadata.has_luminance = 1;
		sideData.type = AV_FRAME_DATA_MASTERING_DISPLAY_METADATA;
		sideData.data = reinterpret_cast<uint8_t *>(&metadata);
		sideData.size = sizeof(metadata);
	}
};

/** Side data of a frame with content light metadata */
struct sLightData {
	AVContentLightMetadata metadata{};
	AVFrameSideData sideData{};

	sLightData(unsigned maxCLL, unsigned maxFALL)
	{
		metadata.MaxCLL = maxCLL;
		metadata.MaxFALL = maxFALL;
		sideData.type = AV_FRAME_DATA_CONTENT_LIGHT_LEVEL;
		sideData.data = reinterpret_cast<uint8_t *>(&metadata);
		sideData.size = sizeof(metadata);
	}
};

} // namespace

TEST_CASE("HDR metadata - EOTF of the transfer characteristics", "[drmhdr]") {
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_BT709) == EOTF_SDR);
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_UNSPECIFIED) == EOTF_SDR);
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_SMPTE170M) == EOTF_SDR);
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_SMPTE2084) == EOTF_PQ);
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_ARIB_STD_B67) == EOTF_HLG);
	// HLG streams often signal BT.2020 and carry HLG in the alternative transfer characteristics
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_BT2020_10) == EOTF_HLG);
	REQUIRE(cHdrMetadata::Eotf(AVCOL_TRC_BT2020_12) == EOTF_HLG);
}

TEST_CASE("HDR metadata - without side data", "[drmhdr]") {
	cHdrMetadata hdr;
	struct hdr_output_metadata data{};

	SECTION("nothing was built yet") {
		REQUIRE_FALSE(hdr.IsHdr());
		REQUIRE(hdr.GetColorPrimaries() == -1);
		REQUIRE(hdr.GetColorTrc() == -1);
	}

	SECTION("SDR BT.709") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT709, AVCOL_TRC_BT709, nullptr, nullptr) == 0);
		const auto &m = data.hdmi_metadata_type1;
		REQUIRE(data.metadata_type == 0);
		REQUIRE(m.metadata_type == 0);
		REQUIRE(m.eotf == EOTF_SDR);
		REQUIRE(m.display_primaries[0].x == Xy(0.640));
		REQUIRE(m.display_primaries[0].y == Xy(0.330));
		REQUIRE(m.display_primaries[1].x == Xy(0.300));
		REQUIRE(m.display_primaries[1].y == Xy(0.600));
		REQUIRE(m.display_primaries[2].x == Xy(0.150));
		REQUIRE(m.display_primaries[2].y == Xy(0.060));
		REQUIRE(m.white_point.x == Xy(0.3127));
		REQUIRE(m.white_point.y == Xy(0.3290));
		REQUIRE_FALSE(hdr.IsHdr());
		REQUIRE(hdr.GetColorPrimaries() == AVCOL_PRI_BT709);
	}

	SECTION("HDR10 uses the BT.2020 primaries and default luminances") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == 0);
		const auto &m = data.hdmi_metadata_type1;
		REQUIRE(m.eotf == EOTF_PQ);
		REQUIRE(m.display_primaries[0].x == Xy(0.708));
		REQUIRE(m.display_primaries[0].y == Xy(0.292));
		REQUIRE(m.display_primaries[1].x == Xy(0.170));
		REQUIRE(m.display_primaries[1].y == Xy(0.797));
		REQUIRE(m.display_primaries[2].x == Xy(0.131));
		REQUIRE(m.display_primaries[2].y == Xy(0.046));
		REQUIRE(m.max_display_mastering_luminance == 4000);
		REQUIRE(m.min_display_mastering_luminance == 50);   // 0.005 cd/m²
		REQUIRE(m.max_cll == 1500);
		REQUIRE(m.max_fall == 400);
		REQUIRE(hdr.IsHdr());
		REQUIRE(hdr.GetColorPrimaries() == AVCOL_PRI_BT2020);
		REQUIRE(hdr.GetColorTrc() == AVCOL_TRC_SMPTE2084);
	}

	SECTION("BT.2020 10 bit is handled as HLG") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_BT2020_10, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.eotf == EOTF_HLG);
		REQUIRE(hdr.GetColorTrc() == AVCOL_TRC_ARIB_STD_B67);
		REQUIRE(hdr.IsHdr());
		// so the next frame with the same format doesn't change anything
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_BT2020_10, nullptr, nullptr) == -1);
	}

	SECTION("BT.470 B/G and unknown primaries") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT470BG, AVCOL_TRC_BT709, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.display_primaries[1].x == Xy(0.290));

		REQUIRE(hdr.Build(&data, AVCOL_PRI_SMPTE432, AVCOL_TRC_BT709, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.display_primaries[1].x == Xy(0.300));   // BT.709
	}
}

TEST_CASE("HDR metadata - changes", "[drmhdr]") {
	cHdrMetadata hdr;
	struct hdr_output_metadata data{};

	REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == 0);

	SECTION("the same format again changes nothing") {
		data = {};
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == -1);
		REQUIRE(data.hdmi_metadata_type1.eotf == 0);   // untouched
		REQUIRE(hdr.IsHdr());
	}

	SECTION("other transfer characteristics") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_ARIB_STD_B67, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.eotf == EOTF_HLG);
	}

	SECTION("other primaries") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT709, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.display_primaries[0].x == Xy(0.640));
	}

	SECTION("back to SDR") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT709, AVCOL_TRC_BT709, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.eotf == EOTF_SDR);
		REQUIRE_FALSE(hdr.IsHdr());
	}

	SECTION("after a reset the same format is built again") {
		hdr.Reset();
		REQUIRE_FALSE(hdr.IsHdr());
		REQUIRE(hdr.GetColorPrimaries() == -1);
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == 0);
		REQUIRE(hdr.IsHdr());
	}
}

TEST_CASE("HDR metadata - side data of the frames", "[drmhdr]") {
	cHdrMetadata hdr;
	struct hdr_output_metadata data{};
	sMasteringData mastering(1000, {50, 10000});
	sLightData light(900, 300);

	SECTION("mastering display metadata") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, nullptr) == 0);
		const auto &m = data.hdmi_metadata_type1;
		REQUIRE(m.display_primaries[0].x == 34000);
		REQUIRE(m.display_primaries[0].y == 16000);
		REQUIRE(m.display_primaries[1].x == 13250);
		REQUIRE(m.display_primaries[1].y == 34500);
		REQUIRE(m.display_primaries[2].x == 7500);
		REQUIRE(m.display_primaries[2].y == 3000);
		REQUIRE(m.white_point.x == 15635);
		REQUIRE(m.white_point.y == 16450);
		REQUIRE(m.max_display_mastering_luminance == 1000);   // cd/m²
		REQUIRE(m.min_display_mastering_luminance == 50);     // 0.0001 cd/m²
		REQUIRE(m.max_cll == 1500);                           // default
	}

	SECTION("mastering display metadata without primaries or luminance") {
		mastering.metadata.has_primaries = 0;
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.display_primaries[0].x == Xy(0.708));
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 1000);

		mastering.metadata.has_primaries = 1;
		mastering.metadata.has_luminance = 0;
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.display_primaries[0].x == 34000);
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 4000);
		REQUIRE(data.hdmi_metadata_type1.min_display_mastering_luminance == 50);
	}

	SECTION("content light metadata") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, &light.sideData) == 0);
		REQUIRE(data.hdmi_metadata_type1.max_cll == 900);
		REQUIRE(data.hdmi_metadata_type1.max_fall == 300);
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 4000);   // default
	}

	SECTION("the side data is only attached to some frames") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, &light.sideData) == 0);
		// the frames between keep the metadata
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == -1);
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, &light.sideData) == -1);
	}

	SECTION("side data, which arrives later, updates the metadata") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == 0);
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 1000);
		// the content light metadata is added to the stored mastering metadata
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, &light.sideData) == 0);
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 1000);
		REQUIRE(data.hdmi_metadata_type1.max_cll == 900);
	}

	SECTION("changed side data") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, &light.sideData) == 0);

		sMasteringData brighter(4000, {1, 10000});
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &brighter.sideData, &light.sideData) == 0);
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 4000);
		REQUIRE(data.hdmi_metadata_type1.min_display_mastering_luminance == 1);

		sLightData darker(400, 100);
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &brighter.sideData, &darker.sideData) == 0);
		REQUIRE(data.hdmi_metadata_type1.max_cll == 400);
		REQUIRE(data.hdmi_metadata_type1.max_fall == 100);
	}

	SECTION("the metadata of the previous video isn't used for another format") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, &light.sideData) == 0);
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_ARIB_STD_B67, nullptr, nullptr) == 0);
		const auto &m = data.hdmi_metadata_type1;
		REQUIRE(m.display_primaries[0].x == Xy(0.708));
		REQUIRE(m.max_display_mastering_luminance == 4000);
		REQUIRE(m.max_cll == 1500);
	}

	SECTION("the metadata is forgotten with a reset") {
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, &mastering.sideData, &light.sideData) == 0);
		hdr.Reset();
		REQUIRE(hdr.Build(&data, AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084, nullptr, nullptr) == 0);
		REQUIRE(data.hdmi_metadata_type1.max_display_mastering_luminance == 4000);
		REQUIRE(data.hdmi_metadata_type1.max_cll == 1500);
	}
}
