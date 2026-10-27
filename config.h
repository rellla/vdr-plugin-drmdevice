// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file config.h
 * Plugin Configuration Header File
 *
 * @copyright 2011, 2015 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_CONFIG_H
#define __DRMDEVICE_CONFIG_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

/**
 * Plugin Configuration
 * @defgroup config Plugin Configuration
 */

/**
 * Holds possible display configurations
 *
 * @ingroup drm
 */
struct sDrmMode {
	int width = 0;               ///< display width
	int height = 0;              ///< display height
	double refreshRateHz = 0.0;  ///< display refresh rate
	bool interlaced = false;     ///< is this an interlaced mode?
};

/**
 * Properties of the played video stream (shown in the statistics)
 *
 * @ingroup config
 */
struct sVideoStreamInfo {
	std::string codec;                  ///< codec (empty, if no video is played)
	int width = 0;                      ///< coded width
	int height = 0;                     ///< coded height
	double frameRate = 0.0;             ///< frame rate (0, if unknown)
	bool interlaced = false;            ///< true, if the stream is interlaced
	const char *deinterlacer = "off";   ///< deinterlacer: "hardware", "software" or "off"
};

/**
 * Properties of the played audio stream (shown in the statistics)
 *
 * @ingroup config
 */
struct sAudioStreamInfo {
	std::string codec;                  ///< codec (empty, if no audio is played)
	std::string profile;                ///< profile of the codec (e.g. "DTS-HD MA", empty, if unknown)
	int sampleRate = 0;                 ///< sample rate of the stream (0, if unknown)
	int channels = 0;                   ///< channels of the stream (0, if unknown)
	bool passthrough = false;           ///< true, if the stream is passed through
	int outputSampleRate = 0;           ///< sample rate of the audio device
	int outputChannels = 0;             ///< channels of the audio device
	std::string device;                 ///< opened audio device
};

/**
 * Display modes
 *
 * @ingroup drm
 */
enum ConfigDisplayModes {
	CONFIG_DISPLAY_MODE_DEFAULT                 = 0,
	CONFIG_DISPLAY_MODE_FOLLOW_VIDEO            = 1,
	CONFIG_DISPLAY_MODE_FOLLOW_VIDEO_INTERLACED = 2,
	CONFIG_DISPLAY_MODE_MANUAL                  = 3,
};

/**
 * Channel switch modes
 *
 * @ingroup device
 */
enum ChannelSwitchMode {
	CHANNEL_SWITCH_AVSYNC,
	CHANNEL_SWITCH_FAST_VIDEO,
	CHANNEL_SWITCH_FAST_AUDIO
};

/**
 * Plugin Configuration
 *
 * @ingroup config
 */
class cDeviceConfig {
public:
	static constexpr int ZOOM_LEVELS = 5;       ///< number of zoom levels
	static constexpr int ZOOM_MAX = 499;        ///< maximum zoom-in factor (1/10 %)

	//
	// setup.conf parameters
	//

	// General
	bool ConfigHideMainMenuEntry = false;       ///< config hide main menu entry

	// Video
	int ConfigVideoEnableHDR = 0;               ///< enable HDR
	int ConfigVideoDisplayMode = CONFIG_DISPLAY_MODE_DEFAULT; ///< display mode (enum ConfigDisplayModes)
	int ConfigVideoChannelSwitchMode = CHANNEL_SWITCH_AVSYNC; ///< channel switch mode (enum ChannelSwitchMode)
	/** zoom-in factor of the zoom levels in 1/10 % (0 = level disabled): +34.4% fills 2.39:1, +12.5% fills 2.00:1 on 16:9 */
	int ConfigZoomLevel[ZOOM_LEVELS] = { 344, 125, 0, 0, 0 };
	int ConfigZapAhead = 0;                     ///< tune the neighbour channels on free devices ahead
	int ConfigZapAheadTuners = 2;               ///< tuners for the live view and the zap cache (2: next channel, 3: next and previous channel)
	int ConfigZapAheadRelease = 2;              ///< release the devices without key press (see cZapCache::ReleaseSeconds())
	int ConfigZapAheadSyncAudio = 1;            ///< start a channel switch of the zap cache with synchronous audio
	int ConfigZapAheadMaxDelay = 2;             ///< max. delay of a start with synchronous audio (see cZapCache::MaxDelayMs())

	// Audio
	bool ConfigAudioSoftvol = false;            ///< config use software volume
	bool ConfigAudioDownmix = false;            ///< config ffmpeg audio downmix
	int ConfigAudioPassthroughMask = 0;         ///< config audio pass-through mask
	bool ConfigAudioPassthroughState = false;   ///< flag audio-passthrough on/off
	bool ConfigAudioPassthroughAuto = false;    ///< pass the codecs through, which the sink reports in its ELD
	int ConfigAudioDisableAutoAES = 0;          ///< don't append the AES parameters to the passthrough device
	int ConfigVideoAudioDelayMs = 0;            ///< config audio delay
	bool ConfigAudioNormalize = false;          ///< config use normalize volume
	int ConfigAudioMaxNormalize = 0;            ///< config max normalize factor
	bool ConfigAudioCompression = false;        ///< config use volume compression
	int ConfigAudioMaxCompression = 0;          ///< config max volume compression
	int ConfigAudioStereoDescent = 0;           ///< config reduce stereo loudness

	// Audio Equalizer
	int ConfigAudioEq = 0;                      ///< config equalizer filter
	int ConfigAudioEqBand[18] =
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0 };          ///< config equalizer filter bands

	// PiP
	// default position at right top, 25% scaled
	int ConfigPipScalePercent = 25;             ///< scale factor of pip video
	int ConfigPipLeftPercent = 100;             ///< 0 = aligned to left, 100 = aligned to right
	int ConfigPipTopPercent = 0;                ///< 0 = aligned to top, 100 = aligned to bottom
	int ConfigPipUseAlt = false;                ///< use the alternative pip position
	// alternative position at left top, 25% scaled
	int ConfigPipAltScalePercent = 25;          ///< alternative scale factor of pip video
	int ConfigPipAltLeftPercent = 0;            ///< 0 = aligned to left, 100 = aligned to right
	int ConfigPipAltTopPercent = 0;             ///< 0 = aligned to top, 100 = aligned to bottom

	// Logging
	bool ConfigLogState = true;                 ///< flag logging on/off
	int ConfigLogLevels = 0;                    ///< loglevel config
	int ConfigLogFFmpegLevel = 0;               ///< FFmpeg log level: 0 = info, 1 = verbose, 2 = debug, 3 = trace

	// Expert Settings
	int ConfigAdditionalBufferLengthMs = 0;     ///< config size ms of a/v buffer
	bool ConfigDisableDeint = false;            ///< disable deinterlacer
	bool ConfigDecoderFallbackToSw = false;     ///< fallback to software decoder if the hardware decoder fails
	int ConfigDecoderFallbackToSwNumPkts = 22;  ///< maximum number of packets sent before fallback to sw decoder
	int ConfigParseH264StreamStart = 0;         ///< log frames at stream start up to the given number of I-Frames
	int ConfigDropInvalidH264PFrames = 0;       ///< drop P-Frames with invalid references on stream start up to the given number of I-Frames
#ifdef USE_GLES
	int ConfigMaxSizeGPUImageCache = 128;       ///< config max gpu image cache size
#endif
	int ConfigShowChannelSwitchDurationMessage = 0; ///< let the skin show a short message about the channel switch duration

	//
	// command line parameters
	//
	const char *ConfigAudioPCMDevice = nullptr;         ///< audio PCM device
	const char *ConfigAudioMixerChannel = nullptr;      ///< audio mixer channel name
	const char *ConfigDrmConnector = nullptr;           ///< user requested drm connector (e.g. "HDMI-A-1")
	const char *ConfigDrmDevice = nullptr;              ///< user requested drm device (e.g. "/dev/dri/card0")
	const char *ConfigDisplayResolution = nullptr;      ///< display resolution (syntax: "1920x1080@50")
	const char *ConfigOsdResolution = nullptr;          ///< osd resolution (syntax: "1920x1080")
#ifdef USE_GLES
	int ConfigDisableOglOsd = 0;                        ///< config disable ogl osd
#endif

	//
	// runtime stats
	//
	const char *CurrentDecoderName = "unknown";         ///< current decoder name
	const char *CurrentDecoderType = "unknown";         ///< current decoder type: "hardware" or "software"
	std::atomic<int> StatMaxShortTermAudioJitterMs = 0; ///< logged max audio jitter of the last 1000 packets
	std::atomic<int> StatMaxLongTermAudioJitterMs = 0;  ///< logged max overall audio jitter since stream start
	std::atomic<int> StatMaxShortTermVideoJitterMs = 0; ///< logged max video jitter of the last 1000 packets
	std::atomic<int> StatMaxLongTermVideoJitterMs = 0;  ///< logged max overall video jitter since stream start
	std::atomic<bool> StatHdrActive = false;            ///< true, if the display gets HDR metadata
	std::vector<sDrmMode> CollectedDrmModes;            ///< collected available drm modes on the current connector
	sDrmMode AutoDetectedDrmMode;                       ///< auto detected mode on the first startup (maybe equal to UserSetMode)
	sDrmMode UserSetDrmMode;                            ///< user requested drm mode on the current connector
	sDrmMode CurrentDrmMode;                            ///< currently used drm mode on the current connector
	sDrmMode CurrentVideoDrmMode;                       ///< currently used video drm mode
	sDrmMode RequestedDrmMode;                          ///< is set to the requested mode which should be changed to

	cDeviceConfig(void) = default;
	bool SetupParse(const char *, const char *);
	void PrintLogLevel(int);
	static int FFmpegLogLevel(int);
	void SetDecoderNeedsMaxPackets(int);
	int GetDecoderNeedsMaxPackets(void);
	bool CompareCurrentMode(sDrmMode *);
	void SetVideoStreamInfo(const sVideoStreamInfo &);
	sVideoStreamInfo GetVideoStreamInfo(void);
	void SetAudioStreamInfo(const sAudioStreamInfo &);
	sAudioStreamInfo GetAudioStreamInfo(void);

private:
	int m_decoderNeedsMaxPackets = 0;                   ///< see GetDecoderNeedsMaxPackets()
	std::mutex m_mutex;                                 ///< protects m_decoderNeedsMaxPackets
	sVideoStreamInfo m_videoStreamInfo;                 ///< properties of the played video stream
	sAudioStreamInfo m_audioStreamInfo;                 ///< properties of the played audio stream
	std::mutex m_streamInfoMutex;                       ///< protects m_videoStreamInfo and m_audioStreamInfo
};

#endif
