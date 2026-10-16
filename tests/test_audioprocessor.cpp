/**
 * @file test_audioprocessor.cpp
 * Unit tests for cAudioProcessor
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "../audioprocessor.h"

static constexpr int BYTES_PER_SAMPLE = 2;

TEST_CASE("cAudioProcessor - Amplify", "[audioprocessor]") {
	cAudioProcessor ap(BYTES_PER_SAMPLE);

	SECTION("Unity gain keeps the samples") {
		int16_t samples[] = { 0, 100, -100, INT16_MAX, INT16_MIN };
		ap.SetAmplifier(1000);
		ap.Amplify(samples, sizeof(samples), 1000);

		REQUIRE(samples[0] == 0);
		REQUIRE(samples[1] == 100);
		REQUIRE(samples[2] == -100);
		REQUIRE(samples[3] == INT16_MAX);
		REQUIRE(samples[4] == INT16_MIN);
	}

	SECTION("Half gain") {
		int16_t samples[] = { 1000, -1000 };
		ap.SetAmplifier(500);
		ap.Amplify(samples, sizeof(samples), 500);

		REQUIRE(samples[0] == 500);
		REQUIRE(samples[1] == -500);
	}

	SECTION("Gain is clipped") {
		int16_t samples[] = { 30000, -30000 };
		ap.SetAmplifier(2000);
		ap.Amplify(samples, sizeof(samples), 1000);

		REQUIRE(samples[0] == INT16_MAX);
		REQUIRE(samples[1] == INT16_MIN);
	}

	SECTION("Volume 0 mutes") {
		int16_t samples[] = { 1000, -1000 };
		ap.SetAmplifier(1000);
		ap.Amplify(samples, sizeof(samples), 0);

		REQUIRE(samples[0] == 0);
		REQUIRE(samples[1] == 0);
	}

	SECTION("Amplifier 0 mutes") {
		int16_t samples[] = { 1000, -1000 };
		ap.SetAmplifier(0);
		ap.Amplify(samples, sizeof(samples), 1000);

		REQUIRE(samples[0] == 0);
		REQUIRE(samples[1] == 0);
	}
}

TEST_CASE("cAudioProcessor - Equalizer options", "[audioprocessor]") {
	cAudioProcessor ap(BYTES_PER_SAMPLE);
	int bands[18] = { 1, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15, 0 };

	ap.SetEqualizer(bands);

	REQUIRE(ap.GetEqualizerOptions() ==
		"1b=1.50:2b=1.00:3b=0.95"
		":4b=0.90:5b=0.85:6b=0.80"
		":7b=0.75:8b=0.70:9b=0.65"
		":10b=0.60:11b=0.55:12b=0.50"
		":13b=0.45:14b=0.40:15b=0.35:"
		"16b=0.30:17b=0.25:18b=1.00 ");
}
