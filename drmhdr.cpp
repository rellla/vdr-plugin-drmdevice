// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file drmhdr.cpp
 * HDR (High Dynamic Range) Interface
 *
 * This code is mostly taken from https://github.com/jojo61/vdr-plugin-softhdcuvid
 * and it seems this was at least inspired by libweston (https://gitlab.freedesktop.org/wayland/weston
 * which is published under the MIT license.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mastering_display_metadata.h>
}

#include <drm_mode.h>

#include "drmhdr.h"
#include "logger.h"
#include "videorender.h"

/**
 * Electro-optical transfer function of the HDR output metadata (CTA-861-G)
 *
 * @ingroup drm
 */
enum hdrMetadataEotf {
	EOTF_TRADITIONAL_GAMMA_SDR,    ///< traditional gamma, SDR luminance range
	EOTF_TRADITIONAL_GAMMA_HDR,    ///< traditional gamma, HDR luminance range
	EOTF_ST2084,                   ///< SMPTE ST 2084 (PQ, HDR10)
	EOTF_HLG,                      ///< hybrid log-gamma
};

/**
 * Static metadata descriptor id of the HDR output metadata
 *
 * @ingroup drm
 */
enum metadataId {
	METADATA_TYPE1,                ///< static metadata type 1
};

/**
 * Encode a chromaticity coordinate in units of 0.00002 (CTA-861-G)
 *
 * @ingroup drm
 */
static inline uint16_t EncodeXYY(float xyy)
{
	return static_cast<uint16_t>(xyy * 50000.0f + 0.5f);
}

/**
 * Get the EOTF of the HDR output metadata for the transfer characteristics
 *
 * @param colorTrc      transfer characteristics (AVColorTransferCharacteristic)
 *
 * @return the EOTF (::hdrMetadataEotf), its bit in the EDID is 1 << EOTF (::EdidEotf)
 */
int cHdrMetadata::Eotf(int colorTrc)
{
	switch (colorTrc) {
		case AVCOL_TRC_BT2020_10:    // 14
		case AVCOL_TRC_BT2020_12:
		case AVCOL_TRC_ARIB_STD_B67: // 18 HLG
			return EOTF_HLG;
		case AVCOL_TRC_SMPTE2084:    // 16
			return EOTF_ST2084;
		case AVCOL_TRC_BT709:        // 1
		case AVCOL_TRC_UNSPECIFIED:  // 2
		default:
			return EOTF_TRADITIONAL_GAMMA_SDR;
	}
}

/**
 * Build an HDR static metadata blob
 *
 * @param[out] data             blob data
 * @param[in]  colorPrimaries   color primaries
 * @param[in]  colorTrc         transfer characteristics
 * @param[in]  sd1              mastering display metadata from ffmpeg frame's sidedata
 * @param[in]  sd2              content light metadata from ffmpeg frame's sidedata
 *
 * @retval 0    on success
 * @retval -1   on error or if nothing changed
 */
int cHdrMetadata::Build(struct hdr_output_metadata *data, int colorPrimaries, int colorTrc, AVFrameSideData *sd1, AVFrameSideData *sd2)
{
	if (!m_pRender->CanHandleHdr())
		return -1;

	// clean up FFmpeg stuff
	if (colorTrc == AVCOL_TRC_BT2020_10)
		colorTrc = AVCOL_TRC_ARIB_STD_B67;

	// The side data is only attached to some frames, the frames between keep the
	// metadata. The stored metadata belongs to the previous video, if the format changed.
	bool changed = m_colorPrimaries != colorPrimaries || m_colorTrc != colorTrc;
	if (changed) {
		m_hasMdMetadata = false;
		m_hasClMetadata = false;
	}

	if (sd1) {
		const AVMasteringDisplayMetadata *md = reinterpret_cast<AVMasteringDisplayMetadata *>(sd1->data);
		if (!m_hasMdMetadata || memcmp(md, &m_mdMetadata, sizeof(m_mdMetadata))) {
			memcpy(&m_mdMetadata, md, sizeof(m_mdMetadata));
			m_hasMdMetadata = true;
			changed = true;
		}
	}

	if (sd2) {
		const AVContentLightMetadata *cl = reinterpret_cast<AVContentLightMetadata *>(sd2->data);
		if (!m_hasClMetadata || memcmp(cl, &m_clMetadata, sizeof(m_clMetadata))) {
			memcpy(&m_clMetadata, cl, sizeof(m_clMetadata));
			m_hasClMetadata = true;
			changed = true;
		}
	}

	if (!changed)
		return -1;

	const AVMasteringDisplayMetadata *mdMetadata = m_hasMdMetadata ? &m_mdMetadata : nullptr;
	const AVContentLightMetadata *clMetadata = m_hasClMetadata ? &m_clMetadata : nullptr;

	LOGDEBUG2(L_DRM, "HDR %s: Update HDR to TRC %d color %d", __FUNCTION__, colorTrc, colorPrimaries);

	m_colorPrimaries = colorPrimaries;
	m_colorTrc = colorTrc;

	int eotf = Eotf(colorTrc);

	struct colorspace *cs;
	switch (colorPrimaries) {
		case AVCOL_PRI_BT2020:       // 9
			cs = colorspace_lookup("BT.2020");
			break;
		case AVCOL_PRI_BT470BG:      // 5
			cs = colorspace_lookup("BT.470 B/G"); // BT.601
			break;
		case AVCOL_PRI_BT709:        // 1
		case AVCOL_PRI_UNSPECIFIED:  // 2
		default:
			cs = colorspace_lookup("BT.709");
			break;
	}

	int maxLum = 4000;
	int minLum = 50;                       // 0.005 cd/m², in units of 0.0001 cd/m²
	if (mdMetadata) { // we got Metadata
		if (mdMetadata->has_primaries) {
			LOGDEBUG2(L_DRM, "HDR %s: Mastering Display Metadata:", __FUNCTION__);
			LOGDEBUG2(L_DRM, "HDR %s: has_primaries: %d has_luminance: %d", __FUNCTION__,
			          mdMetadata->has_primaries, mdMetadata->has_luminance);
			LOGDEBUG2(L_DRM, "HDR %s: r(%5.4f,%5.4f) g(%5.4f,%5.4f) b(%5.4f %5.4f) wp(%5.4f, %5.4f)", __FUNCTION__,
			          av_q2d(mdMetadata->display_primaries[0][0]), av_q2d(mdMetadata->display_primaries[0][1]), av_q2d(mdMetadata->display_primaries[1][0]),
			          av_q2d(mdMetadata->display_primaries[1][1]), av_q2d(mdMetadata->display_primaries[2][0]), av_q2d(mdMetadata->display_primaries[2][1]),
			          av_q2d(mdMetadata->white_point[0]), av_q2d(mdMetadata->white_point[1]));
			LOGDEBUG2(L_DRM, "HDR %s: min_luminance= %f, max_luminance= %f", __FUNCTION__,
			          av_q2d(mdMetadata->min_luminance), av_q2d(mdMetadata->max_luminance));

			cs = &m_hdr10;
			cs->r.f[0] = (float)mdMetadata->display_primaries[0][0].num / (float)mdMetadata->display_primaries[0][0].den;
			cs->r.f[1] = (float)mdMetadata->display_primaries[0][1].num / (float)mdMetadata->display_primaries[0][1].den;
			cs->g.f[0] = (float)mdMetadata->display_primaries[1][0].num / (float)mdMetadata->display_primaries[1][0].den;
			cs->g.f[1] = (float)mdMetadata->display_primaries[1][1].num / (float)mdMetadata->display_primaries[1][1].den;
			cs->b.f[0] = (float)mdMetadata->display_primaries[2][0].num / (float)mdMetadata->display_primaries[2][0].den;
			cs->b.f[1] = (float)mdMetadata->display_primaries[2][1].num / (float)mdMetadata->display_primaries[2][1].den;
			cs->whitepoint.f[0] = (float)mdMetadata->white_point[0].num / (float)mdMetadata->white_point[0].den;
			cs->whitepoint.f[1] = (float)mdMetadata->white_point[1].num / (float)mdMetadata->white_point[1].den;
		}
		if (mdMetadata->has_luminance) {
			maxLum = static_cast<uint16_t>(av_q2d(mdMetadata->max_luminance) + 0.5);
			minLum = static_cast<uint16_t>(av_q2d(mdMetadata->min_luminance) * 10000 + 0.5);
			LOGDEBUG2(L_DRM, "HDR %s: maxLum %d minLum %d", __FUNCTION__, maxLum, minLum);
		}
	}

	int maxCLL = 1500;
	int maxFALL = 400;
	if (clMetadata) {
		maxCLL = clMetadata->MaxCLL;
		maxFALL = clMetadata->MaxFALL;
		LOGDEBUG2(L_DRM, "HDR %s: Has maxCLL %d maxFALL %d", __FUNCTION__, maxCLL, maxFALL);
	}

	data->metadata_type = METADATA_TYPE1;
	data->hdmi_metadata_type1.eotf = eotf;
	data->hdmi_metadata_type1.metadata_type = METADATA_TYPE1;
	data->hdmi_metadata_type1.display_primaries[0].x = EncodeXYY(cs->r.f[0]);
	data->hdmi_metadata_type1.display_primaries[0].y = EncodeXYY(cs->r.f[1]);
	data->hdmi_metadata_type1.display_primaries[1].x = EncodeXYY(cs->g.f[0]);
	data->hdmi_metadata_type1.display_primaries[1].y = EncodeXYY(cs->g.f[1]);
	data->hdmi_metadata_type1.display_primaries[2].x = EncodeXYY(cs->b.f[0]);
	data->hdmi_metadata_type1.display_primaries[2].y = EncodeXYY(cs->b.f[1]);
	data->hdmi_metadata_type1.white_point.x = EncodeXYY(cs->whitepoint.f[0]);
	data->hdmi_metadata_type1.white_point.y = EncodeXYY(cs->whitepoint.f[1]);
	data->hdmi_metadata_type1.max_display_mastering_luminance = maxLum;
	data->hdmi_metadata_type1.min_display_mastering_luminance = minLum;
	data->hdmi_metadata_type1.max_cll = maxCLL;
	data->hdmi_metadata_type1.max_fall = maxFALL;

	LOGDEBUG2(L_DRM, "HDR %s: blob metadata set at %p", __FUNCTION__, data);

	return 0;
}
