// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file test_decoderwatchdog.cpp
 * Unit tests for cDecoderWatchdog
 */

#include <catch2/catch_test_macros.hpp>

#include "decoderwatchdog.h"

TEST_CASE("cDecoderWatchdog", "[decoderwatchdog]") {
	cDecoderWatchdog watchdog(2000);

	SECTION("the first check starts the time") {
		REQUIRE_FALSE(watchdog.Stalled(10000, true));
		REQUIRE_FALSE(watchdog.Stalled(11999, true));
		REQUIRE(watchdog.Stalled(12000, true));
	}

	SECTION("a progress restarts the time") {
		watchdog.Progress(0);
		REQUIRE_FALSE(watchdog.Stalled(1500, true));
		watchdog.Progress(1500);
		REQUIRE_FALSE(watchdog.Stalled(3000, true));
		REQUIRE(watchdog.Stalled(3500, true));
	}

	SECTION("the time doesn't count, while the decoder isn't waited for") {
		watchdog.Progress(0);
		REQUIRE_FALSE(watchdog.Stalled(1900, true));
		REQUIRE_FALSE(watchdog.Stalled(5000, false));   // e.g. paused
		REQUIRE_FALSE(watchdog.Stalled(6900, true));
		REQUIRE(watchdog.Stalled(7000, true));
	}

	SECTION("it stays stalled, until a progress") {
		watchdog.Progress(0);
		REQUIRE(watchdog.Stalled(2000, true));
		REQUIRE(watchdog.Stalled(2500, true));
		watchdog.Progress(2500);
		REQUIRE_FALSE(watchdog.Stalled(2600, true));
	}
}
