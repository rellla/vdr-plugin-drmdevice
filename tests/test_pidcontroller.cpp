/**
 * @file test_pidcontroller.cpp
 * Unit tests for cPidController
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../pidcontroller.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("cPidController - Proportional term", "[pid]") {
	cPidController pid(2.0, 0.0, 0.0, 1000.0);
	pid.SetTargetValue(100.0);

	// error = target - current = 10
	REQUIRE_THAT(pid.Update(90.0, 1.0), WithinAbs(20.0, 1e-9));
	REQUIRE_THAT(pid.GetPTerm(), WithinAbs(20.0, 1e-9));

	// error = -5
	REQUIRE_THAT(pid.Update(105.0, 1.0), WithinAbs(-10.0, 1e-9));
}

TEST_CASE("cPidController - Invalid time delta", "[pid]") {
	cPidController pid(2.0, 1.0, 1.0, 1000.0);
	pid.SetTargetValue(100.0);

	REQUIRE(pid.Update(0.0, 0.0) == 0.0);
	REQUIRE(pid.Update(0.0, -1.0) == 0.0);
}

TEST_CASE("cPidController - Integral and derivative term", "[pid]") {
	cPidController pid(0.0, 0.5, 2.0, 1000.0);
	pid.SetTargetValue(10.0);

	// The first run only initializes the error history, I and D stay 0
	REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(0.0, 1e-9));
	REQUIRE(pid.GetITerm() == 0.0);
	REQUIRE(pid.GetDTerm() == 0.0);

	// error 10 for 2s -> integral 20 -> iTerm 10, derivative 0
	REQUIRE_THAT(pid.Update(0.0, 2.0), WithinAbs(10.0, 1e-9));
	REQUIRE_THAT(pid.GetITerm(), WithinAbs(10.0, 1e-9));
	REQUIRE_THAT(pid.GetDTerm(), WithinAbs(0.0, 1e-9));

	// error drops from 10 to 6 in 1s -> integral 26 -> iTerm 13, dTerm 2 * -4 = -8
	REQUIRE_THAT(pid.Update(4.0, 1.0), WithinAbs(5.0, 1e-9));
	REQUIRE_THAT(pid.GetITerm(), WithinAbs(13.0, 1e-9));
	REQUIRE_THAT(pid.GetDTerm(), WithinAbs(-8.0, 1e-9));
}

TEST_CASE("cPidController - Saturation keeps the integral term", "[pid]") {
	// maxIntegral = maxOutput / ki = 100
	cPidController pid(0.0, 1.0, 0.0, 100.0);
	pid.SetTargetValue(30.0);

	pid.Update(0.0, 1.0); // first run
	REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(30.0, 1e-9));
	REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(60.0, 1e-9));
	REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(90.0, 1e-9));

	// the integral sum is clamped to 100, the output stays at the maximum
	for (int i = 0; i < 5; i++) {
		REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(100.0, 1e-9));
		REQUIRE_THAT(pid.GetITerm(), WithinAbs(100.0, 1e-9));
	}

	// the error changes its sign: the integral term decreases from the limit (no windup)
	pid.SetTargetValue(0.0);
	REQUIRE_THAT(pid.Update(10.0, 1.0), WithinAbs(90.0, 1e-9));
}

TEST_CASE("cPidController - The proportional term saturates the output", "[pid]") {
	cPidController pid(10.0, 1.0, 0.0, 100.0);
	pid.SetTargetValue(50.0);

	// P = 500 -> limited to 100, the integral term is kept
	pid.Update(0.0, 1.0); // first run
	REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(100.0, 1e-9));
	REQUIRE_THAT(pid.GetITerm(), WithinAbs(50.0, 1e-9));

	// back at the target, the integral term remains as correction
	REQUIRE_THAT(pid.Update(50.0, 1.0), WithinAbs(50.0, 1e-9));
}

TEST_CASE("cPidController - Output is limited", "[pid]") {
	cPidController pid(100.0, 0.0, 0.0, 50.0);
	pid.SetTargetValue(10.0);

	REQUIRE_THAT(pid.Update(0.0, 1.0), WithinAbs(50.0, 1e-9));
	REQUIRE_THAT(pid.Update(20.0, 1.0), WithinAbs(-50.0, 1e-9));
}

TEST_CASE("cPidController - Reset", "[pid]") {
	cPidController pid(1.0, 1.0, 1.0, 1000.0);
	pid.SetTargetValue(10.0);

	pid.Update(0.0, 1.0);
	pid.Update(0.0, 1.0);
	REQUIRE(pid.GetITerm() != 0.0);

	pid.Reset();
	REQUIRE(pid.GetPTerm() == 0.0);
	REQUIRE(pid.GetITerm() == 0.0);
	REQUIRE(pid.GetDTerm() == 0.0);
	REQUIRE(pid.GetTargetValue() == 10.0); // the target value is kept

	// after a reset the next update is a "first run" again
	REQUIRE_THAT(pid.Update(5.0, 1.0), WithinAbs(5.0, 1e-9));
	REQUIRE(pid.GetITerm() == 0.0);
}
