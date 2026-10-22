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

TEST_CASE("cDeviceConfig - Passthrough of all codecs", "[config]") {
	cDeviceConfig config;
	const int mask = CODEC_AC3 | CODEC_EAC3 | CODEC_DTS | CODEC_TRUEHD | CODEC_DTSHD;

	REQUIRE(mask == CODEC_PASSTHROUGH_ALL);
	REQUIRE(config.SetupParse("AudioPassthrough", std::to_string(mask).c_str()));
	REQUIRE(config.ConfigAudioPassthroughMask == 124);
	REQUIRE(config.ConfigAudioPassthroughState);
}

TEST_CASE("cDeviceConfig - Automatic AES", "[config]") {
	cDeviceConfig config;

	SECTION("The automatic AES is enabled by default") {
		REQUIRE(config.ConfigAudioDisableAutoAES == 0);
	}

	SECTION("It can be disabled") {
		REQUIRE(config.SetupParse("AudioDisableAutoAES", "1"));
		REQUIRE(config.ConfigAudioDisableAutoAES == 1);
	}

	SECTION("The old parameter is accepted, but ignored") {
		REQUIRE(config.SetupParse("AudioAutoAES", "0"));
		REQUIRE(config.ConfigAudioDisableAutoAES == 0);
	}
}

TEST_CASE("cDeviceConfig - FFmpeg log level", "[config]") {
	cDeviceConfig config;

	REQUIRE(config.ConfigLogFFmpegLevel == 0);

	SECTION("Valid values select the FFmpeg log level") {
		REQUIRE(config.SetupParse("LogFFmpegLevel", "2"));
		REQUIRE(config.ConfigLogFFmpegLevel == 2);
		REQUIRE(LOGGER->GetFFmpegLogLevel() == AV_LOG_DEBUG);
	}

	SECTION("Invalid values are clamped") {
		REQUIRE(config.SetupParse("LogFFmpegLevel", "7"));
		REQUIRE(config.ConfigLogFFmpegLevel == 3);
		REQUIRE(LOGGER->GetFFmpegLogLevel() == AV_LOG_TRACE);
		REQUIRE(config.SetupParse("LogFFmpegLevel", "-1"));
		REQUIRE(config.ConfigLogFFmpegLevel == 0);
		REQUIRE(LOGGER->GetFFmpegLogLevel() == AV_LOG_INFO);
	}

	SECTION("Config levels map to the FFmpeg levels") {
		REQUIRE(cDeviceConfig::FFmpegLogLevel(0) == AV_LOG_INFO);
		REQUIRE(cDeviceConfig::FFmpegLogLevel(1) == AV_LOG_VERBOSE);
		REQUIRE(cDeviceConfig::FFmpegLogLevel(2) == AV_LOG_DEBUG);
		REQUIRE(cDeviceConfig::FFmpegLogLevel(3) == AV_LOG_TRACE);
	}

	LOGGER->SetFFmpegLogLevel(AV_LOG_INFO);
}

TEST_CASE("cDeviceConfig - FFmpeg log level only applies with the FFmpeg logs", "[config]") {
	cDeviceConfig config;

	REQUIRE(config.SetupParse("LogFFmpegLevel", "3"));

	SECTION("Without the FFmpeg logs FFmpeg keeps its default level") {
		REQUIRE(config.SetupParse("LogLevel", std::to_string(L_DEBUG).c_str()));
		REQUIRE(av_log_get_level() == AV_LOG_INFO);
	}

	SECTION("With the FFmpeg logs FFmpeg gets the selected level") {
		REQUIRE(config.SetupParse("LogLevel", std::to_string(L_FFMPEG).c_str()));
		REQUIRE(av_log_get_level() == AV_LOG_TRACE);
	}

	SECTION("Disabled logging keeps the FFmpeg default level") {
		REQUIRE(config.SetupParse("LogLevel", std::to_string(-L_FFMPEG).c_str()));
		REQUIRE(av_log_get_level() == AV_LOG_INFO);
	}

	config.SetupParse("LogFFmpegLevel", "0");
	LOGGER->SetLogLevel(0);
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

TEST_CASE("cDeviceConfig - zoom levels", "[config]") {
	cDeviceConfig config;

	// defaults: 2.39:1 and 2.00:1 on 16:9
	REQUIRE(config.ConfigZoomLevel[0] == 344);
	REQUIRE(config.ConfigZoomLevel[1] == 125);
	REQUIRE(config.ConfigZoomLevel[2] == 0);

	REQUIRE(config.SetupParse("ZoomLevel1", "200"));
	REQUIRE(config.ConfigZoomLevel[0] == 200);
	REQUIRE(config.SetupParse("ZoomLevel5", "1000"));
	REQUIRE(config.ConfigZoomLevel[4] == cDeviceConfig::ZOOM_MAX);
	REQUIRE(config.SetupParse("zoomlevel3", "-5"));
	REQUIRE(config.ConfigZoomLevel[2] == 0);
	REQUIRE_FALSE(config.SetupParse("ZoomLevel0", "1"));
	REQUIRE_FALSE(config.SetupParse("ZoomLevel6", "1"));
	REQUIRE_FALSE(config.SetupParse("ZoomLevel10", "1"));
	REQUIRE_FALSE(config.SetupParse("ZoomLevel", "1"));
}
