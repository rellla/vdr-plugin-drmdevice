/**
 * @file test_filllevel.cpp
 * Unit tests for cBufferFillLevelLowPassFilter
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../filllevel.h"

using Catch::Matchers::WithinAbs;

// 30 seconds with one packet every 160ms (see filllevel.h)
static constexpr int SETTLING_PACKETS = 30 / 0.16;

TEST_CASE("cBufferFillLevelLowPassFilter - Frame counters", "[filllevel]") {
	cBufferFillLevelLowPassFilter filter;

	filter.ReceivedFrames(100);
	filter.ReceivedFrames(50);
	filter.WroteFrames(30);

	REQUIRE(filter.GetFramesReceived() == 150);
	REQUIRE(filter.GetFramesPlayed() == 30);

	filter.ResetFramesCounters();
	REQUIRE(filter.GetFramesReceived() == 0);
	REQUIRE(filter.GetFramesPlayed() == 0);
}

TEST_CASE("cBufferFillLevelLowPassFilter - First update initializes the average", "[filllevel]") {
	cBufferFillLevelLowPassFilter filter;

	filter.ReceivedFrames(1000);
	filter.WroteFrames(400);
	filter.UpdateAvgBufferFillLevel(200);

	// received - written + hardware buffer
	REQUIRE_THAT(filter.GetBufferFillLevelFramesAvg(), WithinAbs(800.0, 1e-9));
	REQUIRE_FALSE(filter.IsSettled());
}

TEST_CASE("cBufferFillLevelLowPassFilter - Low pass filtering", "[filllevel]") {
	cBufferFillLevelLowPassFilter filter;

	filter.UpdateAvgBufferFillLevel(1000);
	REQUIRE_THAT(filter.GetBufferFillLevelFramesAvg(), WithinAbs(1000.0, 1e-9));

	// a single outlier only moves the average a little bit
	filter.UpdateAvgBufferFillLevel(2000);
	double avg = filter.GetBufferFillLevelFramesAvg();
	REQUIRE(avg > 1000.0);
	REQUIRE(avg < 1100.0);
}

TEST_CASE("cBufferFillLevelLowPassFilter - Settling", "[filllevel]") {
	cBufferFillLevelLowPassFilter filter;

	filter.UpdateAvgBufferFillLevel(500); // UNINITIALIZED -> SETTLING
	for (int i = 0; i < SETTLING_PACKETS; i++) {
		REQUIRE_FALSE(filter.IsSettled());
		filter.UpdateAvgBufferFillLevel(500);
	}

	filter.UpdateAvgBufferFillLevel(500);
	REQUIRE(filter.IsSettled());
	REQUIRE_THAT(filter.GetBufferFillLevelFramesAvg(), WithinAbs(500.0, 1e-6));

	SECTION("Reset starts settling again") {
		filter.Reset();
		REQUIRE_FALSE(filter.IsSettled());
		REQUIRE(filter.GetBufferFillLevelFramesAvg() == 0.0);
	}
}
