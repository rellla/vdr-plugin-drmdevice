/**
 * @file test_filllevel_regressions.cpp
 * Regression tests for fixed bugs of cBufferFillLevelLowPassFilter
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../filllevel.h"

using Catch::Matchers::WithinAbs;

// 30 seconds with one packet every 160ms (see filllevel.h)
static constexpr int SETTLING_PACKETS = 30 / 0.16;

TEST_CASE("cBufferFillLevelLowPassFilter - Frame counters don't overflow", "[filllevel]") {
	// Regression test: the counters were 32 bit and overflowed after about 12 hours at 48kHz
	cBufferFillLevelLowPassFilter filter;
	const int framesPerPacket = 48000;

	for (int i = 0; i < 13 * 3600; i++) {
		filter.ReceivedFrames(framesPerPacket);
		filter.WroteFrames(framesPerPacket);
	}

	REQUIRE(filter.GetFramesReceived() == (int64_t)13 * 3600 * framesPerPacket);
	REQUIRE(filter.GetFramesPlayed() == (int64_t)13 * 3600 * framesPerPacket);

	filter.UpdateAvgBufferFillLevel(100);
	REQUIRE(filter.GetBufferFillLevelFramesAvg() == 100);
}
