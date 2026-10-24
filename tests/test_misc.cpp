/**
 * @file test_misc.cpp
 * Unit tests for the helper functions in misc.h
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <thread>

#include "../misc.h"

TEST_CASE("misc - ReadBytes", "[misc]") {
	const uint8_t data[] = { 0x12, 0x34, 0x56, 0x78, 0x9A };

	REQUIRE(ReadBytes(data, 1) == 0x12);
	REQUIRE(ReadBytes(data, 2) == 0x1234);
	REQUIRE(ReadBytes(data, 3) == 0x123456);
	REQUIRE(ReadBytes(data, 4) == 0x12345678);
	REQUIRE(ReadBytes(&data[1], 4) == 0x3456789A);
}

TEST_CASE("misc - Timestamp2String", "[misc]") {
	SECTION("Milliseconds") {
		REQUIRE(std::string(Timestamp2StringMs(0)) == " 0:00:00.000");
		REQUIRE(std::string(Timestamp2StringMs(1)) == " 0:00:00.001");
		REQUIRE(std::string(Timestamp2StringMs(61001)) == " 0:01:01.001");
		REQUIRE(std::string(Timestamp2StringMs(3723456)) == " 1:02:03.456");
	}

	SECTION("PTS ticks (90 kHz)") {
		REQUIRE(std::string(Timestamp2StringPts(90000)) == " 0:00:01.000");
		REQUIRE(std::string(Timestamp2StringPts(90LL * 3723456)) == " 1:02:03.456");
	}

	SECTION("No PTS") {
		REQUIRE(std::string(Timestamp2StringMs(AV_NOPTS_VALUE)) == "--:--:--.---");
		REQUIRE(std::string(Timestamp2StringPts(AV_NOPTS_VALUE)) == "--:--:--.---");
	}

	SECTION("Results of consecutive calls stay valid") {
		// the function uses rotating static buffers, so it can be called
		// multiple times within one log statement
		const char *a = Timestamp2StringMs(1000);
		const char *b = Timestamp2StringMs(2000);
		const char *c = Timestamp2StringMs(3000);

		REQUIRE(std::string(a) == " 0:00:01.000");
		REQUIRE(std::string(b) == " 0:00:02.000");
		REQUIRE(std::string(c) == " 0:00:03.000");
	}
}

TEST_CASE("misc - isInterlacedFrame", "[misc]") {
	AVFrame *frame = av_frame_alloc();
	REQUIRE(frame);

	REQUIRE_FALSE(isInterlacedFrame(frame));

#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(58,7,100)
	frame->interlaced_frame = 1;
#else
	frame->flags |= AV_FRAME_FLAG_INTERLACED;
#endif
	REQUIRE(isInterlacedFrame(frame));

	av_frame_free(&frame);
}

TEST_CASE("misc - The string buffers are per thread", "[misc]") {
	SECTION("Timestamp2String") {
		const char *mine = Timestamp2StringMs(61001);

		// another thread uses all of its buffers
		std::thread other([]() {
			for (int i = 0; i < 3; i++)
				REQUIRE(std::string(Timestamp2StringMs(3723456)) == " 1:02:03.456");
		});
		other.join();

		REQUIRE(std::string(mine) == " 0:01:01.001");
	}

#ifdef av_err2str
	SECTION("av_err2str") {
		const char *mine = av_err2str(AVERROR(EAGAIN));
		std::string expected = mine;

		std::thread other([]() {
			for (int i = 0; i < 3; i++)
				REQUIRE(std::string(av_err2str(AVERROR_EOF)) == "End of file");
		});
		other.join();

		REQUIRE(std::string(mine) == expected);
	}

	SECTION("Three strings can be used at once") {
		const char *a = av_err2str(AVERROR_EOF);
		const char *b = av_err2str(AVERROR(EAGAIN));
		const char *c = av_err2str(AVERROR(ENOMEM));

		REQUIRE(std::string(a) == "End of file");
		REQUIRE(std::string(b) != std::string(c));
	}
#endif
}

TEST_CASE("misc - Alpha blending", "[misc]") {
	SECTION("Unmultiply a premultiplied color") {
		REQUIRE(UnmultiplyAlpha(0x80, 0xFF) == 0x80);
		REQUIRE(UnmultiplyAlpha(0x40, 0x80) == 0x7F);
		REQUIRE(UnmultiplyAlpha(0x80, 0x80) == 0xFF);
		REQUIRE(UnmultiplyAlpha(0x00, 0x01) == 0x00);
	}

	SECTION("An incorrectly premultiplied color doesn't overflow") {
		REQUIRE(UnmultiplyAlpha(0xFF, 0x80) == 0xFF);
		REQUIRE(UnmultiplyAlpha(0x02, 0x01) == 0xFF);
	}

	SECTION("Blend") {
		REQUIRE(BlendColor(0x00, 0xFF, 0xFF) == 0xFF);
		REQUIRE(BlendColor(0xFF, 0x00, 0x00) == 0xFF);
		REQUIRE(BlendColor(0x00, 0xFF, 0x80) == 0x80);
		REQUIRE(BlendColor(0xFF, 0xFF, 0x80) == 0xFF);
	}
}

TEST_CASE("misc - ZoomCropRect", "[misc]") {
	int x, y, w, h;

	SECTION("no zoom shows the whole picture") {
		ZoomCropRect(1920, 1080, 0, &x, &y, &w, &h);
		REQUIRE((x == 0 && y == 0 && w == 1920 && h == 1080));
	}

	SECTION("+34.4% fills a 2.39:1 picture on 16:9") {
		ZoomCropRect(1920, 1080, 344, &x, &y, &w, &h);
		// 1920 / 1.344 = 1428.57, 1080 / 1.344 = 803.57
		REQUIRE((w == 1428 && h == 802));
		REQUIRE((x == 246 && y == 138));
		// centered and the aspect ratio is kept
		REQUIRE(x * 2 + w <= 1920);
		REQUIRE(y * 2 + h <= 1080);
		REQUIRE(std::abs(w * 9 - h * 16) < 32);
	}

	SECTION("offsets and sizes are even") {
		ZoomCropRect(720, 576, 125, &x, &y, &w, &h);
		REQUIRE((x % 2 == 0 && y % 2 == 0 && w % 2 == 0 && h % 2 == 0));
		REQUIRE((w == 640 && h == 512 && x == 40 && y == 32));
	}

	SECTION("invalid sizes are passed") {
		ZoomCropRect(0, 0, 344, &x, &y, &w, &h);
		REQUIRE((x == 0 && y == 0 && w == 0 && h == 0));
	}
}
