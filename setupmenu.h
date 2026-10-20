// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file setupmenu.h
 * Plugin Setup Menu Header File
 *
 * @copyright 2011, 2014 by Johns.  All Rights Reserved.
 * @copyright 2018 - 2019 zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_SETUPMENU_H
#define __DRMDEVICE_SETUPMENU_H

#include <vdr/menuitems.h>

class cAudioStream;
class cDeviceConfig;
class cOutputDevice;

/**
 * Plugin Setup Menu
 *
 * The setup page of the plugin with collapsible sections.
 *
 * @ingroup menu
 */
class cDeviceSetupMenu : public cMenuSetupPage {
public:
	cDeviceSetupMenu(cOutputDevice *);
	virtual eOSState ProcessKey(eKeys);

protected:
	/**
	 * @name Local copies of the setup values
	 *
	 * The values are edited in the menu and stored by Store(). A m_c*Menu member
	 * holds the state (expanded/collapsed) of its menu section.
	 */
	///@{

	// General
	int m_cGeneralMenu;
	int m_cHideMainMenuEntry;

	// Video
	int m_cVideoMenu;
	int m_cVideoEnableHDR;
	int m_cVideoDisplayMode;
	int m_cVideoChannelSwitchMode;

	// Audio
	int m_cAudioMenu;
	int m_cAudioSoftvol;
	int m_cAudioDownmix;
	int m_cAudioPassthroughDefault;
	int m_cAudioPassthroughAC3;
	int m_cAudioPassthroughEAC3;
	int m_cAudioPassthroughDTS;
	int m_cAudioPassthroughTrueHD;
	int m_cAudioPassthroughDTSHD;
	int m_cAudioAutoAES;
	int m_cAudioDelay;
	int m_cAudioNormalize;
	int m_cAudioMaxNormalize;
	int m_cAudioCompression;
	int m_cAudioMaxCompression;
	int m_cAudioStereoDescent;

	// Audio equalizer
	int m_cAudioFilterMenu;
	int m_cAudioEq;
	int m_cAudioEqBand[18];

	// Picture-in-Picture
	int m_cPipMenu;
	int m_cPipScalePercent;
	int m_cPipLeftPercent;
	int m_cPipTopPercent;
	int m_cPipUseAlt;
	int m_cPipAltScalePercent;
	int m_cPipAltLeftPercent;
	int m_cPipAltTopPercent;

	// Logging
	int m_cLoggingMenu;
	int m_cLogDefault;
	int m_cLogDebug_;
	int m_cLogDRM;
	int m_cLogCodec;
	int m_cLogAVSync;
	int m_cLogSound;
	int m_cLogFFmpeg;
	int m_cLogFFmpegLevel;
	int m_cLogPacket;
	int m_cLogOSD;
	int m_cLogGrab;
	int m_cLogStill;
	int m_cLogTrick;
	int m_cLogMedia;
	int m_cLogGL;
	int m_cLogGLTime;
	int m_cLogGLTimeAll;

	// Statistics
	int m_cStatisticsMenu;

	// Expert settings
	int m_cExpertMenu;
	int m_cAdditionalBufferLengthMs;
	int m_cDisableDeint;
	int m_cDecoderFallbackToSw;
	int m_cDecoderFallbackToSwNumPkts;
	int m_cParseH264StreamStart;
	int m_cDropInvalidH264PFrames;
#ifdef USE_GLES
	int m_cMaxSizeGPUImageCache;
#endif
	int m_cShowChannelSwitchDurationMessage;
	///@}

private:
	cOutputDevice *m_pDevice;                          ///< pointer to the device
	cDeviceConfig *m_pConfig;                          ///< pointer to the config

	std::vector<std::string> m_displayMode;            ///< texts of the display mode selection
	std::vector<const char *> m_displayModePtrs;       ///< pointers to m_displayMode for cMenuEditStraItem
	std::vector<std::string> m_channelSwitchMode;      ///< texts of the channel switch mode selection
	std::vector<const char *> m_channelSwitchModePtrs; ///< pointers to m_channelSwitchMode for cMenuEditStraItem
	static constexpr int FFMPEG_LOG_LEVELS = 4;        ///< number of the FFmpeg log levels
	/** texts of the FFmpeg log level selection (see cDeviceConfig::ConfigLogFFmpegLevel) */
	const char *m_ffmpegLogLevels[FFMPEG_LOG_LEVELS] = { "info", "verbose", "debug", "trace" };

	inline cOsdItem * CollapsedItem(const char *, int &, const char * = NULL);
	void Create(void);
	void BuildDisplayModeList(void);                    ///< fill the display mode selection
	void BuildChannelSwitchModeList(void);              ///< fill the channel switch mode selection

protected:
	virtual void Store(void);
};

#endif
