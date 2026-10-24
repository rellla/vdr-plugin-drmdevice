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

#include <functional>
#include <string>
#include <vector>

#include <vdr/menuitems.h>
#include <vdr/tools.h>

#include "config.h"

class cAudioStream;
class cOutputDevice;

/**
 * Plugin Setup Menu
 *
 * The setup page of the plugin. It lists the sections, OK opens a section.
 * In a section, OK stores the settings and Back discards the changes, both
 * return to the list of the sections.
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
	 * The values are edited in the menu and stored by Store().
	 */
	///@{

	// General
	int m_cHideMainMenuEntry;

	// Video
	int m_cVideoEnableHDR;
	int m_cVideoDisplayMode;
	int m_cVideoChannelSwitchMode;
	int m_cZoomLevel[cDeviceConfig::ZOOM_LEVELS];

	// Audio
	int m_cAudioSoftvol;
	int m_cAudioDownmix;
	int m_cAudioPassthroughMode;
	int m_cAudioPassthroughAC3;
	int m_cAudioPassthroughEAC3;
	int m_cAudioPassthroughDTS;
	int m_cAudioPassthroughTrueHD;
	int m_cAudioPassthroughDTSHD;
	int m_cAudioDisableAutoAES;
	int m_cAudioDelay;
	int m_cAudioNormalize;
	int m_cAudioMaxNormalize;
	int m_cAudioCompression;
	int m_cAudioMaxCompression;
	int m_cAudioStereoDescent;

	// Audio equalizer
	int m_cAudioEq;
	int m_cAudioEqBand[18];

	// Picture-in-Picture
	int m_cPipScalePercent;
	int m_cPipLeftPercent;
	int m_cPipTopPercent;
	int m_cPipUseAlt;
	int m_cPipAltScalePercent;
	int m_cPipAltLeftPercent;
	int m_cPipAltTopPercent;

	// Logging
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

	// Expert settings
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
	/** sections of the setup menu */
	enum Section {
		SECTION_OVERVIEW = -1,                     ///< the list of the sections
		SECTION_GENERAL,                           ///< general settings
		SECTION_VIDEO,                             ///< video settings
		SECTION_AUDIO,                             ///< audio settings
		SECTION_AUDIO_EQ,                          ///< audio equalizer
		SECTION_PIP,                               ///< picture-in-picture (only, if pip can be used)
		SECTION_LOGGING,                           ///< logging
		SECTION_STATISTICS,                        ///< statistics (nothing to store)
		SECTION_EXPERT,                            ///< expert settings
		SECTION_COUNT                              ///< number of the sections
	};
	static constexpr int KEEP_CURRENT = -2;            ///< Create() keeps the current item
	/** returned for a handled OK, VDR's menus don't handle this state (see ProcessKey()) */
	static constexpr eOSState OK_HANDLED = os_User;

	cOutputDevice *m_pDevice;                          ///< pointer to the device
	cDeviceConfig *m_pConfig;                          ///< pointer to the config

	std::vector<std::string> m_displayMode;            ///< texts of the display mode selection
	std::vector<const char *> m_displayModePtrs;       ///< pointers to m_displayMode for cMenuEditStraItem
	const char *m_passthroughModes[3];                 ///< texts of the passthrough mode selection
	std::vector<std::string> m_channelSwitchMode;      ///< texts of the channel switch mode selection
	std::vector<const char *> m_channelSwitchModePtrs; ///< pointers to m_channelSwitchMode for cMenuEditStraItem
	static constexpr int FFMPEG_LOG_LEVELS = 4;        ///< number of the FFmpeg log levels
	/** texts of the FFmpeg log level selection (see cDeviceConfig::ConfigLogFFmpegLevel) */
	const char *m_ffmpegLogLevels[FFMPEG_LOG_LEVELS] = { "info", "verbose", "debug", "trace" };

	/** statistics item, whose text is updated, while the menu is shown */
	struct sLiveItem {
		cOsdItem *item;                            ///< the menu item
		std::function<cString(void)> text;         ///< creates the current text of the item
	};
	static constexpr int LIVE_UPDATE_INTERVAL_MS = 1000; ///< update interval of the statistics
	std::vector<sLiveItem> m_liveItems;                  ///< statistics items of the shown menu
	cTimeMs m_liveUpdateTimer;                           ///< timer for the next update of the statistics
	int m_section = SECTION_OVERVIEW;                    ///< shown section
	std::vector<int> m_overviewSections;                 ///< sections of the overview items
	cString m_title;                                     ///< title of the overview

	static const char *SectionName(int);
	void ShowSection(int);
	void LoadValues(void);
	void AddLiveItem(std::function<cString(void)>);
	void UpdateLiveItems(void);
	void AddStatistics(void);
	void Create(int = KEEP_CURRENT);
	void BuildDisplayModeList(void);                    ///< fill the display mode selection
	void BuildChannelSwitchModeList(void);              ///< fill the channel switch mode selection

protected:
	virtual void Store(void);
};

#endif
