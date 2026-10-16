/**
 * @file test_misc.cpp
 * Unit tests for the helper functions in misc.h
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <string>

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
