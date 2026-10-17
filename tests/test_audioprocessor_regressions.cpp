/**
 * @file test_audioprocessor_regressions.cpp
 * Regression tests for fixed bugs of cAudioProcessor
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "../audioprocessor.h"

static constexpr int BYTES_PER_SAMPLE = 2;

TEST_CASE("cAudioProcessor - Compressor keeps signed samples", "[audioprocessor]") {
	// Regression test: the samples were processed as unsigned values, so negative
	// samples became large positive ones (e.g. -1000 -> 32719)
	cAudioProcessor ap(BYTES_PER_SAMPLE);
	ap.SetCompressor(2000);
	ap.ResetCompressor();

	int16_t samples[] = { -1000, 1000, -1000, 1000 };
	ap.Compress(samples, sizeof(samples));

	REQUIRE(samples[0] < 0);
	REQUIRE(samples[1] > 0);
	REQUIRE(samples[0] == -samples[1]);
	// the maximum compression factor is 2.0
	REQUIRE(samples[1] == 2000);
}

TEST_CASE("cAudioProcessor - Compressor limits to full scale", "[audioprocessor]") {
	cAudioProcessor ap(BYTES_PER_SAMPLE);
	ap.SetCompressor(10000);
	ap.ResetCompressor();

	int16_t samples[] = { -20000, 20000 };
	for (int i = 0; i < 100; i++) {
		samples[0] = -20000;
		samples[1] = 20000;
		ap.Compress(samples, sizeof(samples));
	}

	// the factor never exceeds INT16_MAX / max sample, so nothing clips
	REQUIRE(samples[1] <= INT16_MAX);
	REQUIRE(samples[1] > 30000);
	REQUIRE(samples[0] == -samples[1]);
}

TEST_CASE("cAudioProcessor - Normalizer keeps signed samples", "[audioprocessor]") {
	cAudioProcessor ap(BYTES_PER_SAMPLE);
	ap.SetNormalizer(1000);
	ap.ResetNormalizer();

	// usable without a previous ResetNormalizer(), too (members are initialized)
	cAudioProcessor ap2(BYTES_PER_SAMPLE);

	int16_t samples[] = { -1000, 1000, -500, 500 };
	ap.Normalize(samples, sizeof(samples));
	REQUIRE(samples[0] == -1000);
	REQUIRE(samples[1] == 1000);
	REQUIRE(samples[2] == -500);

	int16_t samples2[] = { -1000, 1000 };
	ap2.Normalize(samples2, sizeof(samples2));
	REQUIRE(samples2[0] == -1000);
	REQUIRE(samples2[1] == 1000);
}
