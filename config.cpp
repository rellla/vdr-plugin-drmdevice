// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config.cpp
 * Plugin Configuration
 *
 * This file defines cDeviceConfig, which is used to keep all
 * the config settings, which are set via setup.conf, commandline
 * or setup menu.
 *
 * @copyright 2011, 2015 by Johns.  All Rights Reserved.
 * @copyright 2018 zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <mutex>

#include "codec_audio.h"
#include "config.h"
#include "logger.h"

/**
 * Parse setup parameters
 *
 * @param name      paramter name (case sensetive)
 * @param value     value as string
 *
 * @return          true if the parameter is supported, false otherwise
 */
bool cDeviceConfig::SetupParse(const char *name, const char *value)
{
	//LOGDEBUG("config: %s: '%s' = '%s'", __FUNCTION__, name, value);

	// General
	if        (!strcasecmp(name, "HideMainMenuEntry"))     { ConfigHideMainMenuEntry = atoi(value);

	// Video
	} else if (!strcasecmp(name, "VideoEnableHDR"))        { ConfigVideoEnableHDR = atoi(value);
	} else if (!strcasecmp(name, "VideoDisplayMode"))      { ConfigVideoDisplayMode = std::min(atoi(value), static_cast<int>(CONFIG_DISPLAY_MODE_FOLLOW_VIDEO_INTERLACED));
	} else if (!strcasecmp(name, "VideoChannelSwitchMode")){ ConfigVideoChannelSwitchMode = atoi(value);
	} else if (!strcasecmp(name, "ZapAhead"))              { ConfigZapAhead = atoi(value);
	} else if (!strncasecmp(name, "ZoomLevel", 9) && name[9] >= '1' && name[9] < '1' + ZOOM_LEVELS && !name[10]) {
	                                                         ConfigZoomLevel[name[9] - '1'] = std::clamp(atoi(value), 0, ZOOM_MAX);

	// Audio
	} else if (!strcasecmp(name, "AudioSoftvol"))          { ConfigAudioSoftvol = atoi(value);
	} else if (!strcasecmp(name, "AudioDownmix"))          { ConfigAudioDownmix = atoi(value);
	} else if (!strcasecmp(name, "AudioPassthrough"))      { ConfigAudioPassthroughMask = abs(atoi(value)) & CODEC_PASSTHROUGH_ALL;
	                                                         ConfigAudioPassthroughAuto = abs(atoi(value)) & CODEC_PASSTHROUGH_AUTO;
	                                                         ConfigAudioPassthroughState = atoi(value) > 0;
	} else if (!strcasecmp(name, "AudioDisableAutoAES"))   { ConfigAudioDisableAutoAES = atoi(value);
	// replaced by AudioDisableAutoAES: the automatic AES is enabled by default now, the stored default (0) is ignored
	} else if (!strcasecmp(name, "AudioAutoAES"))          {
	} else if (!strcasecmp(name, "AudioDelay"))            { ConfigVideoAudioDelayMs = atoi(value);
	} else if (!strcasecmp(name, "AudioNormalize"))        { ConfigAudioNormalize = atoi(value);
	} else if (!strcasecmp(name, "AudioMaxNormalize"))     { ConfigAudioMaxNormalize = atoi(value);
	} else if (!strcasecmp(name, "AudioCompression"))      { ConfigAudioCompression = atoi(value);
	} else if (!strcasecmp(name, "AudioMaxCompression"))   { ConfigAudioMaxCompression = atoi(value);
	} else if (!strcasecmp(name, "AudioStereoDescent"))    { ConfigAudioStereoDescent = atoi(value);

	// Audio Equalizer
	} else if (!strcasecmp(name, "AudioEq"))               { ConfigAudioEq = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand01b"))        { ConfigAudioEqBand[0] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand02b"))        { ConfigAudioEqBand[1] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand03b"))        { ConfigAudioEqBand[2] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand04b"))        { ConfigAudioEqBand[3] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand05b"))        { ConfigAudioEqBand[4] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand06b"))        { ConfigAudioEqBand[5] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand07b"))        { ConfigAudioEqBand[6] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand08b"))        { ConfigAudioEqBand[7] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand09b"))        { ConfigAudioEqBand[8] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand10b"))        { ConfigAudioEqBand[9] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand11b"))        { ConfigAudioEqBand[10] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand12b"))        { ConfigAudioEqBand[11] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand13b"))        { ConfigAudioEqBand[12] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand14b"))        { ConfigAudioEqBand[13] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand15b"))        { ConfigAudioEqBand[14] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand16b"))        { ConfigAudioEqBand[15] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand17b"))        { ConfigAudioEqBand[16] = atoi(value);
	} else if (!strcasecmp(name, "AudioEqBand18b"))        { ConfigAudioEqBand[17] = atoi(value);

	// PiP
	} else if (!strcasecmp(name, "PipScalePercent"))       { ConfigPipScalePercent = atoi(value);
	} else if (!strcasecmp(name, "PipLeftPercent"))        { ConfigPipLeftPercent = atoi(value);
	} else if (!strcasecmp(name, "PipTopPercent"))         { ConfigPipTopPercent = atoi(value);
	} else if (!strcasecmp(name, "PipUseAlt"))             { ConfigPipUseAlt = atoi(value);
	} else if (!strcasecmp(name, "PipAltScalePercent"))    { ConfigPipAltScalePercent = atoi(value);
	} else if (!strcasecmp(name, "PipAltLeftPercent"))     { ConfigPipAltLeftPercent = atoi(value);
	} else if (!strcasecmp(name, "PipAltTopPercent"))      { ConfigPipAltTopPercent = atoi(value);

	// Logging
	} else if (!strcasecmp(name, "LogLevel"))              { ConfigLogLevels = abs(atoi(value));
	                                                         ConfigLogState = atoi(value) > 0;
	                                                         PrintLogLevel(ConfigLogState ? ConfigLogLevels : 0);
	                                                         cLogger::GetLogger()->SetLogLevel(ConfigLogState ? ConfigLogLevels : 0);
	} else if (!strcasecmp(name, "LogFFmpegLevel"))        { ConfigLogFFmpegLevel = std::clamp(atoi(value), 0, 3);
	                                                         cLogger::GetLogger()->SetFFmpegLogLevel(FFmpegLogLevel(ConfigLogFFmpegLevel));

	// Expert Settings
	} else if (!strcasecmp(name, "AdditionalBufferLengthMs"))   { ConfigAdditionalBufferLengthMs = atoi(value);
	} else if (!strcasecmp(name, "DisableDeint"))               { ConfigDisableDeint = atoi(value);
	} else if (!strcasecmp(name, "DecoderFallbackToSw"))        { ConfigDecoderFallbackToSw = atoi(value);
	} else if (!strcasecmp(name, "DecoderFallbackToSwNumPkts")) { ConfigDecoderFallbackToSwNumPkts = atoi(value);
	} else if (!strcasecmp(name, "ParseH264StreamStart"))       { ConfigParseH264StreamStart = atoi(value);
	} else if (!strcasecmp(name, "DropInvalidH264PFrames"))     { ConfigDropInvalidH264PFrames = atoi(value);
#ifdef USE_GLES
	} else if (!strcasecmp(name, "MaxSizeGPUImageCache"))  { ConfigMaxSizeGPUImageCache = atoi(value);
#endif
	} else if (!strcasecmp(name, "ShowChannelSwitchDurationMessage"))  { ConfigShowChannelSwitchDurationMessage = atoi(value);
	} else
		return false;

	return true;
}

/**
 * Convert the FFmpeg log level of the config to the FFmpeg log level
 *
 * @param configLevel  0 = info, 1 = verbose, 2 = debug, 3 = trace
 * @return             AV_LOG_INFO, AV_LOG_VERBOSE, AV_LOG_DEBUG or AV_LOG_TRACE
 */
int cDeviceConfig::FFmpegLogLevel(int configLevel)
{
	switch (configLevel) {
		case 1:
			return AV_LOG_VERBOSE;
		case 2:
			return AV_LOG_DEBUG;
		case 3:
			return AV_LOG_TRACE;
		default:
			return AV_LOG_INFO;
	}
}

/**
 * Log the enabled log categories
 *
 * @param loglevel     log level mask (see ::LogFlags)
 */
void cDeviceConfig::PrintLogLevel(int loglevel)
{
	if (!loglevel)
		return;

	char prefix[256] = "Set loglevels:";
	if (loglevel & L_DEBUG)
		strcat(prefix, " standard debugs,");
	if (loglevel & L_AV_SYNC)
		strcat(prefix, " AV-Sync,");
	if (loglevel & L_SOUND)
		strcat(prefix, " sound,");
	if (loglevel & L_OSD)
		strcat(prefix, " osd,");
	if (loglevel & L_DRM)
		strcat(prefix, " drm,");
	if (loglevel & L_CODEC)
		strcat(prefix, " codec,");
	if (loglevel & L_FFMPEG)
		strcat(prefix, " ffmpeg,");
	if (loglevel & L_STILL)
		strcat(prefix, " stillpicture,");
	if (loglevel & L_TRICK)
		strcat(prefix, " trickspeed,");
	if (loglevel & L_MEDIA)
		strcat(prefix, " mediaplayer,");
	if ((loglevel & L_OPENGL) ||
	    (loglevel & L_OPENGL_TIME) ||
	    (loglevel & L_OPENGL_TIME_ALL))
		strcat(prefix, " OpenGL OSD,");
	if (loglevel & L_PACKET)
		strcat(prefix, " packet tracking,");
	if (loglevel & L_GRAB)
		strcat(prefix, " grabbing");

	LOGINFO("%s", prefix);
}

/**
 * Set the maximum number of packets, the hardware decoder needed until it returned the first frame
 *
 * @param num          number of packets
 */
void cDeviceConfig::SetDecoderNeedsMaxPackets(int num)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_decoderNeedsMaxPackets = num;
}

/** Get the maximum number of packets, the hardware decoder needed until it returned the first frame */
int cDeviceConfig::GetDecoderNeedsMaxPackets(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_decoderNeedsMaxPackets;
}

/**
 * Set the properties of the played video stream (called by the decoding thread)
 *
 * @param info         properties of the video stream
 */
void cDeviceConfig::SetVideoStreamInfo(const sVideoStreamInfo &info)
{
	std::lock_guard<std::mutex> lock(m_streamInfoMutex);
	m_videoStreamInfo = info;
}

/** Get a copy of the properties of the played video stream (for the statistics) */
sVideoStreamInfo cDeviceConfig::GetVideoStreamInfo(void)
{
	std::lock_guard<std::mutex> lock(m_streamInfoMutex);
	return m_videoStreamInfo;
}

/**
 * Set the properties of the played audio stream (called by the decoding thread)
 *
 * @param info         properties of the audio stream
 */
void cDeviceConfig::SetAudioStreamInfo(const sAudioStreamInfo &info)
{
	std::lock_guard<std::mutex> lock(m_streamInfoMutex);
	m_audioStreamInfo = info;
}

/** Get a copy of the properties of the played audio stream (for the statistics) */
sAudioStreamInfo cDeviceConfig::GetAudioStreamInfo(void)
{
	std::lock_guard<std::mutex> lock(m_streamInfoMutex);
	return m_audioStreamInfo;
}

/**
 * Return true, if the given mode is equal to the current display mode
 *
 * @param newMode      mode to compare
 */
bool cDeviceConfig::CompareCurrentMode(sDrmMode *newMode)
{
	return (newMode->width == CurrentDrmMode.width &&
	        newMode->height == CurrentDrmMode.height &&
	        newMode->refreshRateHz == CurrentDrmMode.refreshRateHz &&
	        newMode->interlaced == CurrentDrmMode.interlaced);
}
