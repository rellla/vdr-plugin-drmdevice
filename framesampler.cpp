// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file framesampler.cpp
 * Frame Sampler
 *
 * Reads a small RGB image from a mapped video frame. It is used for ambient
 * lighting (Hyperion), where only a tiny image (e.g. 80x45) is needed.
 *
 * Instead of converting the whole frame (detiling, color conversion and
 * scaling of several megabytes), only a few pixels per destination pixel are
 * read directly from the frame in its native layout. The frames live in
 * dmabufs, which are usually mapped uncached, so touching as little memory
 * as possible is what makes this cheap.
 *
 * Supported layouts:
 * - linear: NV12, NV21, YUV420, YVU420, P010, NV15, (A|X)RGB8888, (A|X)BGR8888
 * - Broadcom SAND128 (Raspberry Pi HEVC decoder): NV12, P030
 *
 * HDR frames (PQ, HLG) are tone mapped to SDR BT.709.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include <drm_fourcc.h>

#include "framesampler.h"

/**
 * @addtogroup misc
 * @{
 */

namespace {

/** Memory layouts, the sampler can read */
enum eLayout {
	LAYOUT_NONE,
	LAYOUT_NV12,        ///< 8 bit Y plane, interleaved 8 bit UV plane (linear)
	LAYOUT_NV21,        ///< 8 bit Y plane, interleaved 8 bit VU plane (linear)
	LAYOUT_YUV420,      ///< three 8 bit planes Y, U, V (linear)
	LAYOUT_YVU420,      ///< three 8 bit planes Y, V, U (linear)
	LAYOUT_P010,        ///< 16 bit (10 bit MSB aligned) Y plane, interleaved UV plane (linear)
	LAYOUT_NV15,        ///< 10 bit packed Y plane, interleaved 10 bit packed UV plane (linear, Rockchip)
	LAYOUT_NV12_SAND,   ///< NV12 in 128 byte columns (Broadcom SAND128)
	LAYOUT_P030_SAND,   ///< 3x10 bit in 32 bit words, 128 byte columns (Broadcom SAND128)
	LAYOUT_BGRX,        ///< ARGB8888/XRGB8888: bytes B, G, R, X (linear)
	LAYOUT_RGBX,        ///< ABGR8888/XBGR8888: bytes R, G, B, X (linear)
};

/** true, if the modifier describes a linear buffer */
bool IsLinear(uint64_t modifier)
{
	return modifier == DRM_FORMAT_MOD_LINEAR || modifier == DRM_FORMAT_MOD_INVALID;
}

/** true, if the modifier is Broadcom SAND128 (with or without the column height parameter) */
bool IsSand128(uint64_t modifier)
{
	return fourcc_mod_broadcom_mod(modifier) == DRM_FORMAT_MOD_BROADCOM_SAND128;
}

/** Get the memory layout of a format/modifier combination */
eLayout GetLayout(uint32_t pixFmt, uint64_t modifier)
{
	if (IsSand128(modifier)) {
		switch (pixFmt) {
			case DRM_FORMAT_NV12: return LAYOUT_NV12_SAND;
			case DRM_FORMAT_P030: return LAYOUT_P030_SAND;
			default:              return LAYOUT_NONE;
		}
	}

	if (!IsLinear(modifier))
		return LAYOUT_NONE;

	switch (pixFmt) {
		case DRM_FORMAT_NV12:     return LAYOUT_NV12;
		case DRM_FORMAT_NV21:     return LAYOUT_NV21;
		case DRM_FORMAT_YUV420:   return LAYOUT_YUV420;
		case DRM_FORMAT_YVU420:   return LAYOUT_YVU420;
		case DRM_FORMAT_P010:     return LAYOUT_P010;
		case DRM_FORMAT_NV15:     return LAYOUT_NV15;
		case DRM_FORMAT_ARGB8888:
		case DRM_FORMAT_XRGB8888: return LAYOUT_BGRX;
		case DRM_FORMAT_ABGR8888:
		case DRM_FORMAT_XBGR8888: return LAYOUT_RGBX;
		default:                  return LAYOUT_NONE;
	}
}

/** Number of planes, the layout needs */
int NeededPlanes(eLayout layout)
{
	switch (layout) {
		case LAYOUT_YUV420:
		case LAYOUT_YVU420:
			return 3;
		case LAYOUT_BGRX:
		case LAYOUT_RGBX:
			return 1;
		case LAYOUT_NONE:
			return 0;
		default:
			return 2;
	}
}

inline uint16_t Load16(const uint8_t *p)
{
	uint16_t v;
	memcpy(&v, p, sizeof(v));   // the buffers are little endian, as the supported platforms
	return v;
}

inline uint32_t Load32(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

/**
 * Plane access of a frame
 *
 * Calculates the address of a sample and checks, that the whole needed area
 * of a plane is inside its mapping before anything is read.
 */
class cPlaneReader {
public:
	cPlaneReader(const sMappedFrame &f, eLayout layout) : m_f(f), m_layout(layout)
	{
		// The column height of the SAND formats is either the modifier parameter
		// or (rpi-ffmpeg convention, parameter 0) the pitch of the plane.
		int param = fourcc_mod_broadcom_param(f.modifier);
		for (int i = 0; i < 2; i++)
			m_colStride[i] = (size_t)(param ? param : f.pitch[i]) * 128;
	}

	/** Address of the last byte, which is read in a plane for the given dimension */
	bool Fits(int plane, size_t lastByte) const
	{
		const uint8_t *p = m_f.plane[plane];
		return p && m_f.planeEnd[plane] && lastByte < (size_t)(m_f.planeEnd[plane] - p);
	}

	bool Validate(void) const
	{
		const uint32_t w = m_f.width, h = m_f.height;
		const uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
		if (!w || !h)
			return false;

		switch (m_layout) {
			case LAYOUT_NV12:
			case LAYOUT_NV21:
				return Fits(0, (size_t)(h - 1) * m_f.pitch[0] + w - 1) &&
				       Fits(1, (size_t)(ch - 1) * m_f.pitch[1] + 2 * cw - 1);
			case LAYOUT_YUV420:
			case LAYOUT_YVU420:
				return Fits(0, (size_t)(h - 1) * m_f.pitch[0] + w - 1) &&
				       Fits(1, (size_t)(ch - 1) * m_f.pitch[1] + cw - 1) &&
				       Fits(2, (size_t)(ch - 1) * m_f.pitch[2] + cw - 1);
			case LAYOUT_P010:
				return Fits(0, (size_t)(h - 1) * m_f.pitch[0] + 2 * w - 1) &&
				       Fits(1, (size_t)(ch - 1) * m_f.pitch[1] + 4 * cw - 1);
			case LAYOUT_NV15:
				// the last sample is read with a 16 bit load at the byte of its first bit
				return Fits(0, (size_t)(h - 1) * m_f.pitch[0] + (10 * ((size_t)w - 1)) / 8 + 1) &&
				       Fits(1, (size_t)(ch - 1) * m_f.pitch[1] + (10 * (2 * (size_t)cw - 1)) / 8 + 1);
			case LAYOUT_NV12_SAND:
				return m_colStride[0] && m_colStride[1] &&
				       Fits(0, ((w - 1) / 128) * m_colStride[0] + (size_t)(h - 1) * 128 + 127) &&
				       Fits(1, ((2 * cw - 1) / 128) * m_colStride[1] + (size_t)(ch - 1) * 128 + 127);
			case LAYOUT_P030_SAND:
				return m_colStride[0] && m_colStride[1] &&
				       Fits(0, ((w - 1) / 96) * m_colStride[0] + (size_t)(h - 1) * 128 + 127) &&
				       Fits(1, ((2 * cw - 1) / 96) * m_colStride[1] + (size_t)(ch - 1) * 128 + 127);
			case LAYOUT_BGRX:
			case LAYOUT_RGBX:
				return Fits(0, (size_t)(h - 1) * m_f.pitch[0] + 4 * (size_t)w - 1);
			default:
				return false;
		}
	}

	/**
	 * Read one pixel
	 *
	 * @param x, y      position in the frame
	 * @param[out] c    Y, U, V (10 bit) or R, G, B (8 bit) for the rgb layouts
	 * @return          true, if c is RGB already
	 */
	bool Read(int x, int y, int c[3]) const
	{
		const int cx = x / 2, cy = y / 2;
		const uint8_t *const *p = m_f.plane;
		const uint32_t *pitch = m_f.pitch;

		switch (m_layout) {
			case LAYOUT_NV12:
			case LAYOUT_NV21: {
				const uint8_t *uv = p[1] + (size_t)cy * pitch[1] + 2 * cx;
				c[0] = p[0][(size_t)y * pitch[0] + x] << 2;
				c[1] = uv[m_layout == LAYOUT_NV21 ? 1 : 0] << 2;
				c[2] = uv[m_layout == LAYOUT_NV21 ? 0 : 1] << 2;
				return false;
			}
			case LAYOUT_YUV420:
			case LAYOUT_YVU420: {
				int u = m_layout == LAYOUT_YUV420 ? 1 : 2;
				int v = m_layout == LAYOUT_YUV420 ? 2 : 1;
				c[0] = p[0][(size_t)y * pitch[0] + x] << 2;
				c[1] = p[u][(size_t)cy * pitch[u] + cx] << 2;
				c[2] = p[v][(size_t)cy * pitch[v] + cx] << 2;
				return false;
			}
			case LAYOUT_P010: {
				const uint8_t *uv = p[1] + (size_t)cy * pitch[1] + 4 * cx;
				c[0] = Load16(p[0] + (size_t)y * pitch[0] + 2 * x) >> 6;
				c[1] = Load16(uv) >> 6;
				c[2] = Load16(uv + 2) >> 6;
				return false;
			}
			case LAYOUT_NV15:
				c[0] = Packed10(p[0] + (size_t)y * pitch[0], x);
				c[1] = Packed10(p[1] + (size_t)cy * pitch[1], 2 * cx);
				c[2] = Packed10(p[1] + (size_t)cy * pitch[1], 2 * cx + 1);
				return false;
			case LAYOUT_NV12_SAND: {
				const uint8_t *uv = p[1] + (size_t)(cx / 64) * m_colStride[1] + (size_t)cy * 128 + (2 * cx) % 128;
				c[0] = p[0][(size_t)(x / 128) * m_colStride[0] + (size_t)y * 128 + x % 128] << 2;
				c[1] = uv[0] << 2;
				c[2] = uv[1] << 2;
				return false;
			}
			case LAYOUT_P030_SAND:
				c[0] = Sand30(p[0], m_colStride[0], x, y);
				c[1] = Sand30(p[1], m_colStride[1], 2 * cx, cy);
				c[2] = Sand30(p[1], m_colStride[1], 2 * cx + 1, cy);
				return false;
			case LAYOUT_BGRX: {
				const uint8_t *px = p[0] + (size_t)y * pitch[0] + 4 * x;
				c[0] = px[2];
				c[1] = px[1];
				c[2] = px[0];
				return true;
			}
			case LAYOUT_RGBX: {
				const uint8_t *px = p[0] + (size_t)y * pitch[0] + 4 * x;
				c[0] = px[0];
				c[1] = px[1];
				c[2] = px[2];
				return true;
			}
			default:
				c[0] = c[1] = c[2] = 0;
				return true;
		}
	}

private:
	const sMappedFrame &m_f;
	eLayout m_layout;
	size_t m_colStride[2] = {};         ///< SAND: bytes between two columns (Y, UV)

	/** 10 bit sample i of a line packed without gaps (NV15) */
	static int Packed10(const uint8_t *line, int i)
	{
		size_t bit = (size_t)i * 10;
		return (Load16(line + bit / 8) >> (bit % 8)) & 0x3ff;
	}

	/** 10 bit sample i of a line in a SAND128 column layout with 3 samples per 32 bit word (P030) */
	static int Sand30(const uint8_t *plane, size_t colStride, int i, int y)
	{
		int word = i / 3;
		const uint8_t *w = plane + (size_t)(word / 32) * colStride + (size_t)y * 128 + (word % 32) * 4;
		return (Load32(w) >> (10 * (i % 3))) & 0x3ff;
	}
};

/** Fixed point (16 bit fraction) YCbCr to RGB coefficients (10 bit) */
struct sYuvMatrix {
	int y;      ///< luma factor
	int rv;     ///< V to R
	int gu;     ///< U to G (negative)
	int gv;     ///< V to G (negative)
	int bu;     ///< U to B
	int yOffset;///< black level of Y
};

constexpr int FIX(double v) { return (int)(v * 65536.0 + (v < 0 ? -0.5 : 0.5)); }

/**
 * Get the conversion matrix of a frame
 *
 * Without colorspace information, HD frames are BT.709 and SD frames BT.601.
 */
sYuvMatrix GetMatrix(AVColorSpace cs, AVColorRange range, uint32_t height)
{
	// Kr, Kb of the standards
	double kr, kb;
	switch (cs) {
		case AVCOL_SPC_BT470BG:
		case AVCOL_SPC_SMPTE170M:
			kr = 0.299;  kb = 0.114;  break;
		case AVCOL_SPC_BT2020_NCL:
		case AVCOL_SPC_BT2020_CL:
			kr = 0.2627; kb = 0.0593; break;
		case AVCOL_SPC_BT709:
			kr = 0.2126; kb = 0.0722; break;
		default:
			if (height >= 720) {
				kr = 0.2126; kb = 0.0722;
			} else {
				kr = 0.299;  kb = 0.114;
			}
			break;
	}
	double kg = 1.0 - kr - kb;

	// 10 bit: limited range Y 64 - 940, C 64 - 960
	bool full = range == AVCOL_RANGE_JPEG;
	double ys = full ? 1.0 : 1023.0 / 876.0;
	double cs_ = full ? 1.0 : 1023.0 / 896.0;

	sYuvMatrix m;
	m.y = FIX(ys);
	m.rv = FIX(2.0 * (1.0 - kr) * cs_);
	m.bu = FIX(2.0 * (1.0 - kb) * cs_);
	m.gu = FIX(2.0 * (1.0 - kb) * kb / kg * cs_);
	m.gv = FIX(2.0 * (1.0 - kr) * kr / kg * cs_);
	m.yOffset = full ? 0 : 64;
	return m;
}

inline int Clamp10(int v)
{
	return v < 0 ? 0 : (v > 1023 ? 1023 : v);
}

/** 10 bit (0 - 1023) to 8 bit (0 - 255) */
inline int To8(int v)
{
	return (v * 255 + 511) / 1023;
}

/*
 * HDR
 *
 * The leds expect an SDR (BT.709) image. A HDR frame (PQ or HLG, BT.2020
 * primaries) is converted to linear light, its colors are converted to BT.709,
 * the highlights are compressed (tone mapping) and it is encoded with the
 * sRGB curve again. A SDR frame with BT.2020 primaries only gets the color
 * conversion. The conversion is done per sample, so it costs little: an
 * image of 80x45 pixels has about 15000 samples.
 */

/** How a frame is converted to the BT.709 SDR image */
enum eToneMap {
	TONEMAP_NONE,           ///< SDR BT.709 (or unknown): R'G'B' is used as it is
	TONEMAP_PQ,             ///< HDR10 (SMPTE ST 2084)
	TONEMAP_HLG,            ///< HLG (ARIB STD-B67)
	TONEMAP_SDR_BT2020,     ///< SDR with BT.2020 primaries (wide color gamut)
};

constexpr double HDR_REFERENCE_WHITE = 203.0;   ///< nits of the HDR reference white (BT.2408), shown as SDR white
constexpr double HLG_PEAK = 1000.0;             ///< nits of the HLG reference display
constexpr float TONEMAP_KNEE = 0.75f;           ///< below, the brightness is kept, above it is compressed towards SDR white

eToneMap GetToneMap(AVColorTransferCharacteristic trc, AVColorPrimaries primaries)
{
	if (trc == AVCOL_TRC_SMPTE2084)
		return TONEMAP_PQ;
	if (trc == AVCOL_TRC_ARIB_STD_B67)
		return TONEMAP_HLG;
	if (primaries == AVCOL_PRI_BT2020)
		return TONEMAP_SDR_BT2020;
	return TONEMAP_NONE;
}

/** Lookup tables of the conversions (created once) */
struct sHdrTables {
	float pq[1024];         ///< PQ code value (10 bit) -> linear, 1.0 = reference white
	float hlg[1024];        ///< HLG code value (10 bit) -> scene linear 0 - 1
	float sdr[1024];        ///< SDR code value (10 bit) -> linear (BT.1886, gamma 2.4)
	uint8_t encode[4096];   ///< linear 0 - 1 -> 8 bit sRGB

	sHdrTables(void)
	{
		// SMPTE ST 2084
		const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
		const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
		// ARIB STD-B67
		const double a = 0.17883277, b = 1.0 - 4.0 * 0.17883277, c = 0.5 - 0.17883277 * std::log(4.0 * 0.17883277);

		for (int i = 0; i < 1024; i++) {
			double e = i / 1023.0;
			double p = std::pow(e, 1.0 / m2);
			double nits = 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
			pq[i] = (float)(nits / HDR_REFERENCE_WHITE);
			hlg[i] = (float)(e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0);
			sdr[i] = (float)std::pow(e, 2.4);
		}
		for (int i = 0; i < 4096; i++) {
			double l = i / 4095.0;
			double v = l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
			encode[i] = (uint8_t)std::lround(255.0 * v);
		}
	}
};

const sHdrTables &HdrTables(void)
{
	static const sHdrTables tables;     // thread safe initialization
	return tables;
}

/**
 * Convert a non-linear BT.2020 R'G'B' sample (10 bit) to 8 bit BT.709 RGB
 */
void ToneMap(eToneMap mode, const int rgb10[3], int out[3])
{
	const sHdrTables &t = HdrTables();
	float r, g, b;

	switch (mode) {
		case TONEMAP_PQ:
			r = t.pq[rgb10[0]];
			g = t.pq[rgb10[1]];
			b = t.pq[rgb10[2]];
			break;
		case TONEMAP_HLG: {
			// scene light -> display light (OOTF, system gamma 1.2 at 1000 nits)
			float rs = t.hlg[rgb10[0]], gs = t.hlg[rgb10[1]], bs = t.hlg[rgb10[2]];
			float ys = 0.2627f * rs + 0.6780f * gs + 0.0593f * bs;
			float gain = ys > 0.0f ? (float)(std::pow(ys, 0.2) * HLG_PEAK / HDR_REFERENCE_WHITE) : 0.0f;
			r = rs * gain;
			g = gs * gain;
			b = bs * gain;
			break;
		}
		default:
			r = t.sdr[rgb10[0]];
			g = t.sdr[rgb10[1]];
			b = t.sdr[rgb10[2]];
			break;
	}

	// BT.2020 -> BT.709 primaries (colors outside of BT.709 are clipped)
	float r7 = std::max(0.0f,  1.6605f * r - 0.5876f * g - 0.0728f * b);
	float g7 = std::max(0.0f, -0.1246f * r + 1.1329f * g - 0.0083f * b);
	float b7 = std::max(0.0f, -0.0182f * r - 0.1006f * g + 1.1187f * b);

	// compress the highlights, the ratio of the colors (hue) is kept
	float m = std::max(r7, std::max(g7, b7));
	if (m > TONEMAP_KNEE) {
		float mapped = TONEMAP_KNEE + (1.0f - TONEMAP_KNEE) * (1.0f - std::exp(-(m - TONEMAP_KNEE) / (1.0f - TONEMAP_KNEE)));
		float scale = mapped / m;
		r7 *= scale;
		g7 *= scale;
		b7 *= scale;
	}

	out[0] = t.encode[std::min(4095, (int)(r7 * 4095.0f + 0.5f))];
	out[1] = t.encode[std::min(4095, (int)(g7 * 4095.0f + 0.5f))];
	out[2] = t.encode[std::min(4095, (int)(b7 * 4095.0f + 0.5f))];
}

} // namespace

/**
 * Check, if a frame of the given format and modifier can be sampled
 */
bool cFrameSampler::IsSupported(uint32_t pixFmt, uint64_t modifier)
{
	return GetLayout(pixFmt, modifier) != LAYOUT_NONE;
}

/**
 * Get a printable name of the format (for logging)
 */
const char *cFrameSampler::FormatName(uint32_t pixFmt, uint64_t modifier)
{
	switch (GetLayout(pixFmt, modifier)) {
		case LAYOUT_NV12:      return "NV12";
		case LAYOUT_NV21:      return "NV21";
		case LAYOUT_YUV420:    return "YUV420";
		case LAYOUT_YVU420:    return "YVU420";
		case LAYOUT_P010:      return "P010";
		case LAYOUT_NV15:      return "NV15";
		case LAYOUT_NV12_SAND: return "NV12/SAND128";
		case LAYOUT_P030_SAND: return "P030/SAND128";
		case LAYOUT_BGRX:      return "XRGB8888";
		case LAYOUT_RGBX:      return "XBGR8888";
		default:               return "unsupported";
	}
}

/**
 * Get a printable name of the conversion of a frame (for the statistics)
 *
 * @return  nullptr, if the frame is SDR BT.709
 */
const char *cFrameSampler::ToneMapName(AVColorTransferCharacteristic trc, AVColorPrimaries primaries)
{
	switch (GetToneMap(trc, primaries)) {
		case TONEMAP_PQ:         return "HDR10 (PQ) tone mapped";
		case TONEMAP_HLG:        return "HLG tone mapped";
		case TONEMAP_SDR_BT2020: return "BT.2020 converted";
		default:                 return nullptr;
	}
}

/**
 * Sample a part of a frame into a RGB24 image
 *
 * The source rectangle is divided into dstW x dstH cells. Of every cell,
 * samplesPerPixel x samplesPerPixel pixels are read, converted to RGB and
 * averaged.
 *
 * @param frame          mapped frame
 * @param srcX, srcY     top left corner of the shown part of the frame
 * @param srcW, srcH     size of the shown part of the frame
 * @param dst            destination RGB24 image (top left pixel of the area to fill)
 * @param dstStride      bytes per line of the destination image
 * @param dstW, dstH     size of the area to fill
 *
 * @return               false, if the frame can't be read (nothing is written)
 */
bool cFrameSampler::Sample(const sMappedFrame &frame,
                           int srcX, int srcY, int srcW, int srcH,
                           uint8_t *dst, int dstStride, int dstW, int dstH)
{
	eLayout layout = GetLayout(frame.pixFmt, frame.modifier);
	if (layout == LAYOUT_NONE || frame.numPlanes < NeededPlanes(layout))
		return false;

	cPlaneReader reader(frame, layout);
	if (!reader.Validate())
		return false;

	if (dstW <= 0 || dstH <= 0 || !dst)
		return false;

	// keep the source rectangle inside of the frame
	if (srcX < 0 || srcY < 0 || srcW <= 0 || srcH <= 0 ||
	    (uint32_t)(srcX + srcW) > frame.width || (uint32_t)(srcY + srcH) > frame.height) {
		srcX = 0;
		srcY = 0;
		srcW = frame.width;
		srcH = frame.height;
	}

	const sYuvMatrix m = GetMatrix(frame.colorspace, frame.colorRange, frame.height);
	const eToneMap toneMap = GetToneMap(frame.colorTrc, frame.colorPrimaries);
	const int n = m_samplesPerPixel;
	const int count = n * n;

	for (int dy = 0; dy < dstH; dy++) {
		uint8_t *out = dst + (size_t)dy * dstStride;

		for (int dx = 0; dx < dstW; dx++) {
			int sum[3] = {0, 0, 0};

			for (int sy = 0; sy < n; sy++) {
				// sample positions are spread evenly over the cell (centered)
				int y = srcY + (int)(((int64_t)(2 * dy * n + 2 * sy + 1) * srcH) / (2 * dstH * n));

				for (int sx = 0; sx < n; sx++) {
					int x = srcX + (int)(((int64_t)(2 * dx * n + 2 * sx + 1) * srcW) / (2 * dstW * n));
					int c[3];

					if (reader.Read(x, y, c)) {
						sum[0] += c[0];
						sum[1] += c[1];
						sum[2] += c[2];
						continue;
					}

					int yy = (c[0] - m.yOffset) * m.y;
					int u = c[1] - 512;
					int v = c[2] - 512;
					int rgb[3] = {
						Clamp10((yy + m.rv * v + 32768) >> 16),
						Clamp10((yy - m.gu * u - m.gv * v + 32768) >> 16),
						Clamp10((yy + m.bu * u + 32768) >> 16),
					};

					if (toneMap != TONEMAP_NONE) {
						int out[3];
						ToneMap(toneMap, rgb, out);
						sum[0] += out[0];
						sum[1] += out[1];
						sum[2] += out[2];
					} else {
						sum[0] += To8(rgb[0]);
						sum[1] += To8(rgb[1]);
						sum[2] += To8(rgb[2]);
					}
				}
			}

			*out++ = (uint8_t)((sum[0] + count / 2) / count);
			*out++ = (uint8_t)((sum[1] + count / 2) / count);
			*out++ = (uint8_t)((sum[2] + count / 2) / count);
		}
	}

	return true;
}

/** @} */
