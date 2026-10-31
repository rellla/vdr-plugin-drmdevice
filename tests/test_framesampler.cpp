/**
 * @file test_framesampler.cpp
 * Unit tests for cFrameSampler
 *
 * Synthetic frames with four colored quadrants are written in every supported
 * memory layout and sampled into a small RGB image.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <drm_fourcc.h>

#include "framesampler.h"

namespace {

constexpr int W = 384;      // 3 SAND128 columns for 8 bit, 4 columns for P030
constexpr int H = 64;

struct sRgb { int r, g, b; };

/** quadrants: red | green / blue | white */
sRgb SourceColor(int x, int y)
{
	bool right = x >= W / 2, bottom = y >= H / 2;
	if (!bottom && !right) return {255, 0, 0};
	if (!bottom && right)  return {0, 255, 0};
	if (bottom && !right)  return {0, 0, 255};
	return {255, 255, 255};
}

/** RGB to YCbCr (8 bit) */
void ToYuv(sRgb c, double kr, double kb, bool full, int *y, int *u, int *v)
{
	double kg = 1.0 - kr - kb;
	double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
	double yy = kr * r + kg * g + kb * b;
	double cb = (b - yy) / (2.0 * (1.0 - kb));
	double cr = (r - yy) / (2.0 * (1.0 - kr));
	if (full) {
		*y = std::lround(255.0 * yy);
		*u = std::lround(128.0 + 255.0 * cb);
		*v = std::lround(128.0 + 255.0 * cr);
	} else {
		*y = std::lround(16.0 + 219.0 * yy);
		*u = std::lround(128.0 + 224.0 * cb);
		*v = std::lround(128.0 + 224.0 * cr);
	}
	*u = std::min(255, std::max(0, *u));
	*v = std::min(255, std::max(0, *v));
}

const double KR709 = 0.2126, KB709 = 0.0722;

/** Frame memory plus the description for the sampler */
struct sTestFrame {
	std::vector<uint8_t> mem;
	sMappedFrame f;

	void SetPlane(int i, size_t offset, uint32_t pitch)
	{
		f.plane[i] = mem.data() + offset;
		f.planeEnd[i] = mem.data() + mem.size();
		f.pitch[i] = pitch;
	}
};

void Init(sTestFrame &t, uint32_t fmt, uint64_t mod, int planes, size_t size)
{
	t.mem.assign(size, 0);
	t.f.width = W;
	t.f.height = H;
	t.f.pixFmt = fmt;
	t.f.modifier = mod;
	t.f.numPlanes = planes;
	t.f.colorspace = AVCOL_SPC_BT709;   // the test frames are encoded with BT.709 (they are SD sized)
}

sTestFrame MakeNv12(bool swapUv)
{
	sTestFrame t;
	const uint32_t pitch = W + 32;
	Init(t, swapUv ? DRM_FORMAT_NV21 : DRM_FORMAT_NV12, DRM_FORMAT_MOD_LINEAR, 2, pitch * H * 3 / 2);
	uint8_t *uv = t.mem.data() + pitch * H;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int yy, u, v;
			ToYuv(SourceColor(x, y), KR709, KB709, false, &yy, &u, &v);
			t.mem[y * pitch + x] = yy;
			if (!(x & 1) && !(y & 1)) {
				uv[(y / 2) * pitch + x] = swapUv ? v : u;
				uv[(y / 2) * pitch + x + 1] = swapUv ? u : v;
			}
		}
	t.SetPlane(0, 0, pitch);
	t.SetPlane(1, pitch * H, pitch);
	return t;
}

sTestFrame MakeYuv420(bool swapUv)
{
	sTestFrame t;
	const uint32_t pitch = W + 16, cpitch = W / 2 + 16;
	Init(t, swapUv ? DRM_FORMAT_YVU420 : DRM_FORMAT_YUV420, DRM_FORMAT_MOD_LINEAR, 3, pitch * H + 2 * cpitch * H / 2);
	size_t off1 = pitch * H, off2 = off1 + cpitch * H / 2;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int yy, u, v;
			ToYuv(SourceColor(x, y), KR709, KB709, false, &yy, &u, &v);
			t.mem[y * pitch + x] = yy;
			if (!(x & 1) && !(y & 1)) {
				t.mem[off1 + (y / 2) * cpitch + x / 2] = swapUv ? v : u;
				t.mem[off2 + (y / 2) * cpitch + x / 2] = swapUv ? u : v;
			}
		}
	t.SetPlane(0, 0, pitch);
	t.SetPlane(1, off1, cpitch);
	t.SetPlane(2, off2, cpitch);
	return t;
}

void Put16(uint8_t *p, int v) { p[0] = v; p[1] = v >> 8; }

sTestFrame MakeP010(void)
{
	sTestFrame t;
	const uint32_t pitch = 2 * W + 64;
	Init(t, DRM_FORMAT_P010, DRM_FORMAT_MOD_LINEAR, 2, pitch * H * 3 / 2);
	uint8_t *uv = t.mem.data() + pitch * H;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int yy, u, v;
			ToYuv(SourceColor(x, y), KR709, KB709, false, &yy, &u, &v);
			Put16(&t.mem[y * pitch + 2 * x], (yy << 2) << 6);
			if (!(x & 1) && !(y & 1)) {
				Put16(&uv[(y / 2) * pitch + 2 * x], (u << 2) << 6);
				Put16(&uv[(y / 2) * pitch + 2 * x + 2], (v << 2) << 6);
			}
		}
	t.SetPlane(0, 0, pitch);
	t.SetPlane(1, pitch * H, pitch);
	return t;
}

void PutPacked10(uint8_t *line, int i, int v)
{
	size_t bit = (size_t)i * 10;
	uint32_t word = line[bit / 8] | (line[bit / 8 + 1] << 8) | (line[bit / 8 + 2] << 16);
	word &= ~(0x3ffu << (bit % 8));
	word |= (uint32_t)v << (bit % 8);
	line[bit / 8] = word;
	line[bit / 8 + 1] = word >> 8;
	line[bit / 8 + 2] = word >> 16;
}

sTestFrame MakeNv15(void)
{
	sTestFrame t;
	const uint32_t pitch = W * 10 / 8 + 16;
	Init(t, DRM_FORMAT_NV15, DRM_FORMAT_MOD_LINEAR, 2, pitch * H * 3 / 2 + 4);
	uint8_t *uv = t.mem.data() + pitch * H;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int yy, u, v;
			ToYuv(SourceColor(x, y), KR709, KB709, false, &yy, &u, &v);
			PutPacked10(&t.mem[y * pitch], x, yy << 2);
			if (!(x & 1) && !(y & 1)) {
				PutPacked10(&uv[(y / 2) * pitch], x, u << 2);
				PutPacked10(&uv[(y / 2) * pitch], x + 1, v << 2);
			}
		}
	t.SetPlane(0, 0, pitch);
	t.SetPlane(1, pitch * H, pitch);
	return t;
}

/**
 * NV12 in SAND128 columns as the Raspberry Pi decoder writes it: each column
 * holds the luma lines followed by the chroma lines
 *
 * @param colHeightInModifier   true: column height in the modifier parameter (pitch = width),
 *                              false: column height in the pitch (rpi-ffmpeg)
 */
sTestFrame MakeNv12Sand(bool colHeightInModifier)
{
	sTestFrame t;
	const int colH = H * 3 / 2 + 8;
	const int cols = (W + 127) / 128;
	uint64_t mod = colHeightInModifier ? DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(colH) : DRM_FORMAT_MOD_BROADCOM_SAND128;
	Init(t, DRM_FORMAT_NV12, mod, 2, (size_t)cols * 128 * colH);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int yy, u, v;
			ToYuv(SourceColor(x, y), KR709, KB709, false, &yy, &u, &v);
			t.mem[(x / 128) * 128 * colH + y * 128 + x % 128] = yy;
			if (!(x & 1) && !(y & 1)) {
				uint8_t *c = &t.mem[128 * H + (x / 128) * 128 * colH + (y / 2) * 128 + x % 128];
				c[0] = u;
				c[1] = v;
			}
		}
	uint32_t pitch = colHeightInModifier ? W : colH;
	t.SetPlane(0, 0, pitch);
	t.SetPlane(1, 128 * H, pitch);
	return t;
}

void PutSand30(uint8_t *plane, int colH, int i, int y, int v)
{
	int word = i / 3;
	uint8_t *p = plane + (word / 32) * 128 * colH + y * 128 + (word % 32) * 4;
	uint32_t w;
	memcpy(&w, p, 4);
	w &= ~(0x3ffu << (10 * (i % 3)));
	w |= (uint32_t)v << (10 * (i % 3));
	memcpy(p, &w, 4);
}

sTestFrame MakeP030Sand(void)
{
	sTestFrame t;
	const int colH = H * 3 / 2;
	const int cols = (W + 95) / 96;
	Init(t, DRM_FORMAT_P030, DRM_FORMAT_MOD_BROADCOM_SAND128, 2, (size_t)cols * 128 * colH);
	uint8_t *chroma = t.mem.data() + 128 * H;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int yy, u, v;
			ToYuv(SourceColor(x, y), KR709, KB709, false, &yy, &u, &v);
			PutSand30(t.mem.data(), colH, x, y, yy << 2);
			if (!(x & 1) && !(y & 1)) {
				PutSand30(chroma, colH, x, y / 2, u << 2);
				PutSand30(chroma, colH, x + 1, y / 2, v << 2);
			}
		}
	t.SetPlane(0, 0, colH);
	t.SetPlane(1, 128 * H, colH);
	return t;
}

sTestFrame MakeRgb32(uint32_t fmt)
{
	sTestFrame t;
	const uint32_t pitch = 4 * W;
	Init(t, fmt, DRM_FORMAT_MOD_LINEAR, 1, pitch * H);
	bool bgr = fmt == DRM_FORMAT_XRGB8888 || fmt == DRM_FORMAT_ARGB8888;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			sRgb c = SourceColor(x, y);
			uint8_t *p = &t.mem[y * pitch + 4 * x];
			p[0] = bgr ? c.b : c.r;
			p[1] = c.g;
			p[2] = bgr ? c.r : c.b;
			p[3] = 0xff;
		}
	t.SetPlane(0, 0, pitch);
	return t;
}

/** Sample into 8x4 and compare every pixel with its quadrant color */
void CheckQuadrants(sTestFrame &t, int tolerance = 3)
{
	const int dw = 8, dh = 4;
	std::vector<uint8_t> img(dw * dh * 3, 0x55);
	cFrameSampler sampler;

	REQUIRE(cFrameSampler::IsSupported(t.f.pixFmt, t.f.modifier));
	REQUIRE(sampler.Sample(t.f, 0, 0, W, H, img.data(), dw * 3, dw, dh));

	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++) {
			sRgb want = SourceColor(x * W / dw, y * H / dh);
			const uint8_t *p = &img[(y * dw + x) * 3];
			INFO("format " << cFrameSampler::FormatName(t.f.pixFmt, t.f.modifier) << " pixel " << x << "," << y
			     << " got " << (int)p[0] << " " << (int)p[1] << " " << (int)p[2]);
			CHECK(std::abs(p[0] - want.r) <= tolerance);
			CHECK(std::abs(p[1] - want.g) <= tolerance);
			CHECK(std::abs(p[2] - want.b) <= tolerance);
		}
}

} // namespace

TEST_CASE("FrameSampler: linear YUV layouts", "[framesampler]")
{
	SECTION("NV12") { auto t = MakeNv12(false); CheckQuadrants(t); }
	SECTION("NV21") { auto t = MakeNv12(true); CheckQuadrants(t); }
	SECTION("YUV420") { auto t = MakeYuv420(false); CheckQuadrants(t); }
	SECTION("YVU420") { auto t = MakeYuv420(true); CheckQuadrants(t); }
	SECTION("P010") { auto t = MakeP010(); CheckQuadrants(t); }
	SECTION("NV15") { auto t = MakeNv15(); CheckQuadrants(t); }
}

TEST_CASE("FrameSampler: Broadcom SAND128 layouts", "[framesampler]")
{
	SECTION("NV12, column height in the pitch") { auto t = MakeNv12Sand(false); CheckQuadrants(t); }
	SECTION("NV12, column height in the modifier") { auto t = MakeNv12Sand(true); CheckQuadrants(t); }
	SECTION("P030") { auto t = MakeP030Sand(); CheckQuadrants(t); }
}

TEST_CASE("FrameSampler: RGB layouts", "[framesampler]")
{
	SECTION("XRGB8888") { auto t = MakeRgb32(DRM_FORMAT_XRGB8888); CheckQuadrants(t, 0); }
	SECTION("ARGB8888") { auto t = MakeRgb32(DRM_FORMAT_ARGB8888); CheckQuadrants(t, 0); }
	SECTION("XBGR8888") { auto t = MakeRgb32(DRM_FORMAT_XBGR8888); CheckQuadrants(t, 0); }
}

TEST_CASE("FrameSampler: crop and destination area", "[framesampler]")
{
	auto t = MakeNv12(false);
	cFrameSampler sampler;

	// only the right half of the frame (green / white) into a 4x2 area of a larger image
	const int iw = 6, ih = 4;
	std::vector<uint8_t> img(iw * ih * 3, 0);
	REQUIRE(sampler.Sample(t.f, W / 2, 0, W / 2, H, img.data() + (1 * iw + 1) * 3, iw * 3, 4, 2));

	auto px = [&](int x, int y) { return &img[(y * iw + x) * 3]; };
	// outside of the area untouched
	CHECK(px(0, 0)[0] == 0);
	CHECK(px(5, 3)[1] == 0);
	// top row green, bottom row white
	CHECK(px(1, 1)[1] >= 252);
	CHECK(px(1, 1)[0] <= 3);
	CHECK(px(4, 2)[0] >= 252);
	CHECK(px(4, 2)[2] >= 252);

	// an invalid source rectangle falls back to the whole frame
	std::vector<uint8_t> all(2 * 2 * 3, 0);
	REQUIRE(sampler.Sample(t.f, W - 10, 0, 100, H, all.data(), 2 * 3, 2, 2));
	CHECK(all[0] >= 252);   // top left: red
	CHECK(all[4] >= 252);   // top right: green
	CHECK(all[8] >= 252);   // bottom left: blue
}

TEST_CASE("FrameSampler: color matrix and range", "[framesampler]")
{
	cFrameSampler sampler;
	uint8_t px[3];

	auto fill = [](sTestFrame &t, int yy, int u, int v) {
		uint32_t pitch = t.f.pitch[0];
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				t.mem[y * pitch + x] = yy;
				if (!(x & 1) && !(y & 1)) {
					t.mem[pitch * H + (y / 2) * pitch + x] = u;
					t.mem[pitch * H + (y / 2) * pitch + x + 1] = v;
				}
			}
	};

	SECTION("full range BT.709") {
		auto t = MakeNv12(false);
		int yy, u, v;
		ToYuv({200, 100, 50}, KR709, KB709, true, &yy, &u, &v);
		fill(t, yy, u, v);
		t.f.colorRange = AVCOL_RANGE_JPEG;
		t.f.colorspace = AVCOL_SPC_BT709;
		REQUIRE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
		CHECK(std::abs(px[0] - 200) <= 2);
		CHECK(std::abs(px[1] - 100) <= 2);
		CHECK(std::abs(px[2] - 50) <= 2);
	}

	SECTION("BT.601 signaled") {
		auto t = MakeNv12(false);
		int yy, u, v;
		ToYuv({200, 100, 50}, 0.299, 0.114, false, &yy, &u, &v);
		fill(t, yy, u, v);
		t.f.colorspace = AVCOL_SPC_SMPTE170M;
		REQUIRE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
		CHECK(std::abs(px[0] - 200) <= 2);
		CHECK(std::abs(px[1] - 100) <= 2);
		CHECK(std::abs(px[2] - 50) <= 2);
	}

	SECTION("unspecified: SD frames are BT.601") {
		auto t = MakeNv12(false);   // 384x64 is SD
		int yy, u, v;
		ToYuv({200, 100, 50}, 0.299, 0.114, false, &yy, &u, &v);
		fill(t, yy, u, v);
		t.f.colorspace = AVCOL_SPC_UNSPECIFIED;
		REQUIRE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
		CHECK(std::abs(px[1] - 100) <= 2);
	}
}

TEST_CASE("FrameSampler: rejected frames", "[framesampler]")
{
	cFrameSampler sampler;
	uint8_t px[3];

	SECTION("unsupported modifier") {
		CHECK_FALSE(cFrameSampler::IsSupported(DRM_FORMAT_NV12, DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED));
		CHECK_FALSE(cFrameSampler::IsSupported(DRM_FORMAT_YUV420, DRM_FORMAT_MOD_BROADCOM_SAND128));
		CHECK_FALSE(cFrameSampler::IsSupported(DRM_FORMAT_RGB565, DRM_FORMAT_MOD_LINEAR));
		auto t = MakeNv12(false);
		t.f.modifier = DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED;
		CHECK_FALSE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
	}

	SECTION("plane outside of the mapping") {
		auto t = MakeNv12(false);
		t.f.planeEnd[1] = t.f.plane[1] + 100;
		CHECK_FALSE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
	}

	SECTION("SAND column height too large for the mapping") {
		auto t = MakeNv12Sand(false);
		t.f.pitch[0] = t.f.pitch[1] = 4096;
		CHECK_FALSE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
	}

	SECTION("missing planes") {
		auto t = MakeYuv420(false);
		t.f.numPlanes = 2;
		CHECK_FALSE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
	}
}

namespace {

/** PQ encoding (SMPTE ST 2084) of a luminance in nits */
double PqEncode(double nits)
{
	const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
	const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
	double y = std::pow(nits / 10000.0, m1);
	return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

/** HLG encoding (ARIB STD-B67) of scene light 0 - 1 */
double HlgEncode(double e)
{
	const double a = 0.17883277, b = 1.0 - 4.0 * a, c = 0.5 - a * std::log(4.0 * a);
	return e <= 1.0 / 12.0 ? std::sqrt(3.0 * e) : a * std::log(12.0 * e - b) + c;
}

/** linear 0 - 1 -> 8 bit sRGB */
int SrgbEncode(double l)
{
	l = std::min(1.0, std::max(0.0, l));
	return (int)std::lround(255.0 * (l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055));
}

/** linear BT.709 -> linear BT.2020 */
void Bt709To2020(const double in[3], double out[3])
{
	out[0] = 0.6274 * in[0] + 0.3293 * in[1] + 0.0433 * in[2];
	out[1] = 0.0691 * in[0] + 0.9195 * in[1] + 0.0114 * in[2];
	out[2] = 0.0164 * in[0] + 0.0880 * in[1] + 0.8956 * in[2];
}

/** non-linear BT.2020 R'G'B' (0 - 1) -> 10 bit limited range YCbCr (BT.2020 matrix) */
void ToYuv10(const double rgb[3], int *y, int *u, int *v)
{
	const double kr = 0.2627, kb = 0.0593, kg = 1.0 - kr - kb;
	double yy = kr * rgb[0] + kg * rgb[1] + kb * rgb[2];
	*y = (int)std::lround(64.0 + 876.0 * yy);
	*u = (int)std::lround(512.0 + 896.0 * (rgb[2] - yy) / (2.0 * (1.0 - kb)));
	*v = (int)std::lround(512.0 + 896.0 * (rgb[0] - yy) / (2.0 * (1.0 - kr)));
}

/** A single colored P010 frame */
sTestFrame MakeP010Color(int y, int u, int v, AVColorTransferCharacteristic trc, AVColorPrimaries primaries)
{
	auto t = MakeP010();
	uint32_t pitch = t.f.pitch[0];
	for (int yy = 0; yy < H; yy++)
		for (int x = 0; x < W; x++) {
			Put16(&t.mem[yy * pitch + 2 * x], y << 6);
			if (!(x & 1) && !(yy & 1)) {
				Put16(&t.mem[pitch * H + (yy / 2) * pitch + 2 * x], u << 6);
				Put16(&t.mem[pitch * H + (yy / 2) * pitch + 2 * x + 2], v << 6);
			}
		}
	t.f.colorspace = AVCOL_SPC_BT2020_NCL;
	t.f.colorTrc = trc;
	t.f.colorPrimaries = primaries;
	return t;
}

/** Expected 8 bit output of the tone mapping (independent of the implementation) */
void ExpectToneMapped(const double linear709[3], int out[3])
{
	double m = std::max(linear709[0], std::max(linear709[1], linear709[2]));
	double scale = 1.0;
	if (m > 0.75)
		scale = (0.75 + 0.25 * (1.0 - std::exp(-(m - 0.75) / 0.25))) / m;
	for (int i = 0; i < 3; i++)
		out[i] = SrgbEncode(linear709[i] * scale);
}

void CheckColor(sTestFrame &t, const int want[3], int tolerance)
{
	cFrameSampler sampler;
	uint8_t px[3];
	REQUIRE(sampler.Sample(t.f, 0, 0, W, H, px, 3, 1, 1));
	INFO("got " << (int)px[0] << " " << (int)px[1] << " " << (int)px[2] << ", want " << want[0] << " " << want[1] << " " << want[2]);
	CHECK(std::abs(px[0] - want[0]) <= tolerance);
	CHECK(std::abs(px[1] - want[1]) <= tolerance);
	CHECK(std::abs(px[2] - want[2]) <= tolerance);
}

/** encode a linear BT.709 color (1.0 = reference white of 203 nits) as PQ and check the result */
void CheckPq(const double linear709[3], int tolerance = 3)
{
	double lin2020[3], enc[3];
	Bt709To2020(linear709, lin2020);
	for (int i = 0; i < 3; i++)
		enc[i] = PqEncode(lin2020[i] * 203.0);
	int y, u, v;
	ToYuv10(enc, &y, &u, &v);
	auto t = MakeP010Color(y, u, v, AVCOL_TRC_SMPTE2084, AVCOL_PRI_BT2020);
	int want[3];
	ExpectToneMapped(linear709, want);
	CheckColor(t, want, tolerance);
}

} // namespace

TEST_CASE("FrameSampler: HDR10 (PQ) tone mapping", "[framesampler]")
{
	SECTION("mid gray (18% of the reference white) keeps its brightness") {
		const double c[3] = {0.18, 0.18, 0.18};
		CheckPq(c);
	}
	SECTION("a saturated BT.709 red stays red") {
		const double c[3] = {0.5, 0.0, 0.0};
		CheckPq(c);
	}
	SECTION("a dark blue") {
		const double c[3] = {0.02, 0.05, 0.3};
		CheckPq(c);
	}
	SECTION("the reference white is compressed a bit") {
		const double c[3] = {1.0, 1.0, 1.0};
		CheckPq(c);
	}
	SECTION("a 1000 nits highlight is nearly white, not clipped earlier") {
		const double c[3] = {1000.0 / 203.0, 1000.0 / 203.0, 1000.0 / 203.0};
		CheckPq(c);
	}
	SECTION("the hue of a bright color is kept") {
		const double c[3] = {3.0, 1.5, 0.3};
		CheckPq(c);
	}
	SECTION("black") {
		const double c[3] = {0.0, 0.0, 0.0};
		CheckPq(c, 1);
	}
}

TEST_CASE("FrameSampler: HLG tone mapping", "[framesampler]")
{
	// HLG 75% is the reference white (BT.2408)
	SECTION("75% white") {
		double enc[3] = {0.75, 0.75, 0.75};
		int y, u, v;
		ToYuv10(enc, &y, &u, &v);
		auto t = MakeP010Color(y, u, v, AVCOL_TRC_ARIB_STD_B67, AVCOL_PRI_BT2020);
		const double lin[3] = {1.0, 1.0, 1.0};
		int want[3];
		ExpectToneMapped(lin, want);
		CheckColor(t, want, 4);
	}
	SECTION("a dark scene stays dark") {
		double scene = 0.01;    // 1% scene light
		double enc[3] = {HlgEncode(scene), HlgEncode(scene), HlgEncode(scene)};
		int y, u, v;
		ToYuv10(enc, &y, &u, &v);
		auto t = MakeP010Color(y, u, v, AVCOL_TRC_ARIB_STD_B67, AVCOL_PRI_BT2020);
		double display = std::pow(scene, 1.2) * 1000.0 / 203.0;
		const double lin[3] = {display, display, display};
		int want[3];
		ExpectToneMapped(lin, want);
		CheckColor(t, want, 3);
	}
}

TEST_CASE("FrameSampler: SDR with BT.2020 primaries", "[framesampler]")
{
	// a BT.709 red, coded in BT.2020 with a gamma 2.4 transfer
	const double lin709[3] = {0.6, 0.0, 0.0};
	double lin2020[3], enc[3];
	Bt709To2020(lin709, lin2020);
	for (int i = 0; i < 3; i++)
		enc[i] = std::pow(lin2020[i], 1.0 / 2.4);
	int y, u, v;
	ToYuv10(enc, &y, &u, &v);
	auto t = MakeP010Color(y, u, v, AVCOL_TRC_BT2020_10, AVCOL_PRI_BT2020);
	const int want[3] = {SrgbEncode(0.6), 0, 0};
	CheckColor(t, want, 4);
}

TEST_CASE("FrameSampler: HDR on the Raspberry Pi (P030/SAND128)", "[framesampler]")
{
	const double lin709[3] = {0.4, 0.1, 0.02};
	double lin2020[3], enc[3];
	Bt709To2020(lin709, lin2020);
	for (int i = 0; i < 3; i++)
		enc[i] = PqEncode(lin2020[i] * 203.0);
	int yy, u, v;
	ToYuv10(enc, &yy, &u, &v);

	auto t = MakeP030Sand();
	const int colH = H * 3 / 2;
	uint8_t *chroma = t.mem.data() + 128 * H;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			PutSand30(t.mem.data(), colH, x, y, yy);
			if (!(x & 1) && !(y & 1)) {
				PutSand30(chroma, colH, x, y / 2, u);
				PutSand30(chroma, colH, x + 1, y / 2, v);
			}
		}
	t.f.colorspace = AVCOL_SPC_BT2020_NCL;
	t.f.colorTrc = AVCOL_TRC_SMPTE2084;
	t.f.colorPrimaries = AVCOL_PRI_BT2020;

	int want[3];
	ExpectToneMapped(lin709, want);
	CheckColor(t, want, 3);
	CHECK(std::string(cFrameSampler::ToneMapName(t.f.colorTrc, t.f.colorPrimaries)) == "HDR10 (PQ) tone mapped");
	CHECK(cFrameSampler::ToneMapName(AVCOL_TRC_BT709, AVCOL_PRI_BT709) == nullptr);
}
