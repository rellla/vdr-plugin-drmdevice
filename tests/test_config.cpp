/**
 * @file test_config.cpp
 * Unit tests for cDeviceConfig
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include "../codec_audio.h"
#include "../config.h"
#include "../logger.h"

TEST_CASE("cDeviceConfig - Unknown parameters", "[config]") {
	cDeviceConfig config;

	REQUIRE_FALSE(config.SetupParse("ThisDoesNotExist", "1"));
	REQUIRE_FALSE(config.SetupParse("", ""));
}

TEST_CASE("cDeviceConfig - Parameter names are case insensitive", "[config]") {
	cDeviceConfig config;

	REQUIRE(config.SetupParse("audiodelay", "-120"));
	REQUIRE(config.ConfigVideoAudioDelayMs == -120);
	REQUIRE(config.SetupParse("AUDIODELAY", "80"));
	REQUIRE(config.ConfigVideoAudioDelayMs == 80);
}

TEST_CASE("cDeviceConfig - Audio passthrough", "[config]") {
	cDeviceConfig config;
	const int mask = CODEC_AC3 | CODEC_EAC3;

	SECTION("Positive value enables passthrough") {
		REQUIRE(config.SetupParse("AudioPassthrough", std::to_string(mask).c_str()));
		REQUIRE(config.ConfigAudioPassthroughMask == mask);
		REQUIRE(config.ConfigAudioPassthroughState);
	}

	SECTION("Negative value keeps the mask, but disables passthrough") {
		REQUIRE(config.SetupParse("AudioPassthrough", std::to_string(-mask).c_str()));
		REQUIRE(config.ConfigAudioPassthroughMask == mask);
		REQUIRE_FALSE(config.ConfigAudioPassthroughState);
	}
}

TEST_CASE("cDeviceConfig - Log level", "[config]") {
	cDeviceConfig config;

	SECTION("Positive value enables logging") {
		REQUIRE(config.SetupParse("LogLevel", std::to_string(L_DEBUG | L_SOUND).c_str()));
		REQUIRE(config.ConfigLogLevels == (L_DEBUG | L_SOUND));
		REQUIRE(config.ConfigLogState);
		REQUIRE(LOGGER->GetLogLevel() == (L_DEBUG | L_SOUND));
	}

	SECTION("Negative value keeps the levels, but disables logging") {
		REQUIRE(config.SetupParse("LogLevel", std::to_string(-(L_DEBUG | L_SOUND)).c_str()));
		REQUIRE(config.ConfigLogLevels == (L_DEBUG | L_SOUND));
		REQUIRE_FALSE(config.ConfigLogState);
		REQUIRE(LOGGER->GetLogLevel() == 0);
	}

	LOGGER->SetLogLevel(0);
}

TEST_CASE("cDeviceConfig - Equalizer bands", "[config]") {
	cDeviceConfig config;

	REQUIRE(config.SetupParse("AudioEqBand01b", "-3"));
	REQUIRE(config.SetupParse("AudioEqBand18b", "1"));
	REQUIRE(config.ConfigAudioEqBand[0] == -3);
	REQUIRE(config.ConfigAudioEqBand[17] == 1);
	REQUIRE_FALSE(config.SetupParse("AudioEqBand19b", "1"));
}

TEST_CASE("cDeviceConfig - CompareCurrentMode", "[config]") {
	cDeviceConfig config;
	config.CurrentDrmMode = { 1920, 1080, 50.0, false };

	sDrmMode same = { 1920, 1080, 50.0, false };
	sDrmMode otherRate = { 1920, 1080, 60.0, false };
	sDrmMode interlaced = { 1920, 1080, 50.0, true };

	REQUIRE(config.CompareCurrentMode(&same));
	REQUIRE_FALSE(config.CompareCurrentMode(&otherRate));
	REQUIRE_FALSE(config.CompareCurrentMode(&interlaced));
}
