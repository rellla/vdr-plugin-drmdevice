// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file setupmenu.cpp
 * Plugin Setup Menu
 *
 * This file defines cDeviceSetupMenu which describes the
 * setup menu and sets the config paramaters.
 *
 * @copyright 2011, 2015 by Johns.  All Rights Reserved.
 * @copyright 2018 zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <string>

#include <vdr/menuitems.h>

#include "audio.h"
#include "codec_audio.h"
#include "config.h"
#include "device.h"
#include "logger.h"
#include "setupmenu.h"

/**
 * Passthrough modes of the setup menu
 *
 * @ingroup menu
 */
enum PassthroughMode {
	PASSTHROUGH_OFF,       ///< decode all audio
	PASSTHROUGH_MANUAL,    ///< pass the selected codecs through
	PASSTHROUGH_AUTO       ///< pass the codecs through, which the sink reports in its ELD
};

/**
 * Create a seperator named item
 *
 * @param label       text inside separator
 *
 * @ingroup menu
 */
static inline cOsdItem *SeparatorName(const char *label)
{
	return new cOsdItem(cString::sprintf("%s:", label), osUnknown, false);
}

/**
 * Get the name of a section of the setup menu
 *
 * @param section   section (see cDeviceSetupMenu::Section)
 */
const char *cDeviceSetupMenu::SectionName(int section)
{
	switch (section) {
	case SECTION_GENERAL:    return tr("General");
	case SECTION_VIDEO:      return tr("Video");
	case SECTION_AUDIO:      return tr("Audio");
	case SECTION_AUDIO_EQ:   return tr("Audio equalizer");
	case SECTION_PIP:        return tr("Picture-in-picture");
	case SECTION_LOGGING:    return tr("Logging");
	case SECTION_STATISTICS: return tr("Statistics");
	case SECTION_EXPERT:     return tr("Expert settings");
	default:                 return "";
	}
}

/**
 * Show the overview or a section of the setup menu
 *
 * @param section   section to show (SECTION_OVERVIEW: the list of the sections)
 */
void cDeviceSetupMenu::ShowSection(int section)
{
	int current = -1;	// the first selectable item of a section

	if (section == SECTION_OVERVIEW) {
		// select the section, which was just closed
		for (size_t i = 0; i < m_overviewSections.size(); i++) {
			if (m_overviewSections[i] == m_section)
				current = i;
		}
		SetTitle(m_title);
	} else {
		// the title of the page is set by VDR after the construction (SetPlugin())
		if (m_section == SECTION_OVERVIEW)
			m_title = Title();
		SetTitle(cString::sprintf("%s - %s", *m_title, SectionName(section)));
	}

	m_section = section;
	Create(current);
}

/**
 * Get a readable name of a codec for the statistics
 *
 * @param codec     FFmpeg name of the codec (e.g. "eac3")
 * @param profile   profile of the codec (e.g. "DTS-HD MA" or empty)
 * @return          e.g. "E-AC-3" or "DTS-HD MA"
 *
 * @ingroup menu
 */
static std::string CodecLabel(const std::string &codec, const std::string &profile)
{
	static const struct { const char *codec; const char *label; } labels[] = {
		{ "mpeg2video", "MPEG-2" }, { "h264", "H.264" }, { "hevc", "HEVC" },
		{ "ac3", "AC-3" }, { "eac3", "E-AC-3" }, { "dts", "DTS" }, { "truehd", "TrueHD" },
		{ "mp2", "MP2" }, { "mp3", "MP3" }, { "aac", "AAC" }, { "aac_latm", "AAC-LATM" },
		{ "pcm_dvd", "LPCM" },
	};

	// the DTS profiles name the format (e.g. "DTS-HD MA")
	if (codec == "dts" && !profile.empty())
		return profile;

	std::string label = codec;
	for (const auto &l : labels) {
		if (codec == l.codec)
			label = l.label;
	}

	return profile.empty() ? label : label + " (" + profile + ")";
}

/**
 * Get the codecs of a passthrough mask for the statistics
 *
 * @param mask      CODEC_* bit mask
 * @return          e.g. "AC-3 E-AC-3" or "-", if the mask is empty
 *
 * @ingroup menu
 */
static std::string PassthroughCodecs(int mask)
{
	std::string codecs;

	if (mask & CODEC_AC3)    codecs += " AC-3";
	if (mask & CODEC_EAC3)   codecs += " E-AC-3";
	if (mask & CODEC_DTS)    codecs += " DTS";
	if (mask & CODEC_DTSHD)  codecs += " DTS-HD";
	if (mask & CODEC_TRUEHD) codecs += " TrueHD";

	return codecs.empty() ? "-" : codecs.substr(1);
}

/**
 * Add a statistics item, whose text is updated, while the menu is shown
 *
 * @param text      creates the current text of the item
 */
void cDeviceSetupMenu::AddLiveItem(std::function<cString(void)> text)
{
	cOsdItem *item = new cOsdItem(text(), osUnknown, false);
	Add(item);
	m_liveItems.push_back({ item, std::move(text) });
}

/**
 * Update the texts of the statistics items and redraw the menu, if one changed
 *
 * The whole menu is drawn again, because not every skin draws a single item,
 * which is changed with cOsdMenu::DisplayItem().
 */
void cDeviceSetupMenu::UpdateLiveItems(void)
{
	bool changed = false;

	for (auto &live : m_liveItems) {
		cString text = live.text();
		if (strcmp(text, live.item->Text())) {
			live.item->SetText(text);
			changed = true;
		}
	}

	if (changed)
		Display();
}

/**
 * Add the statistics of the played streams, the display and the audio output
 */
void cDeviceSetupMenu::AddStatistics(void)
{
	// video
	AddLiveItem([this]() {
		if (m_pDevice->IsDetached())
			return cString(tr("Device detached"));
		sVideoStreamInfo video = m_pConfig->GetVideoStreamInfo();
		if (video.codec.empty())
			return cString(tr("Video: none"));
		return cString::sprintf(tr("Video: %s %dx%d%s, %.2f fps"), CodecLabel(video.codec, "").c_str(),
			video.width, video.height, video.interlaced ? "i" : "p", video.frameRate);
	});
	AddLiveItem([this]() {
		return cString::sprintf(tr("Video decoder: %s (%s)"), m_pConfig->CurrentDecoderName, m_pConfig->CurrentDecoderType);
	});
	AddLiveItem([this]() {
		const char *deinterlacer = m_pConfig->GetVideoStreamInfo().deinterlacer;
		return cString::sprintf(tr("Deinterlacer: %s"), !strcmp(deinterlacer, "off") ? trVDR("off") : tr(deinterlacer));
	});
	AddLiveItem([this]() {
		return cString::sprintf(tr("Display: %dx%d@%.2f%s, %s"), m_pConfig->CurrentDrmMode.width, m_pConfig->CurrentDrmMode.height,
			m_pConfig->CurrentDrmMode.refreshRateHz, m_pConfig->CurrentDrmMode.interlaced ? "i" : "",
			m_pConfig->StatHdrActive ? "HDR" : "SDR");
	});
	AddLiveItem([this]() {
		int duped;
		int dropped;
		int counter;
		m_pDevice->GetStats(&duped, &dropped, &counter);
		return cString::sprintf(tr("Frames duped(%d) dropped(%d) total(%d)"), duped, dropped, counter);
	});

	// audio
	AddLiveItem([this]() {
		sAudioStreamInfo audio = m_pConfig->GetAudioStreamInfo();
		if (audio.codec.empty())
			return cString(tr("Audio: none"));
		std::string codec = CodecLabel(audio.codec, audio.profile);
		// the stream format of a bitstream is only known from the mediaplayer
		if (!audio.sampleRate || !audio.channels)
			return cString::sprintf(tr("Audio: %s"), codec.c_str());
		return cString::sprintf(tr("Audio: %s, %d Hz, %d channels"), codec.c_str(), audio.sampleRate, audio.channels);
	});
	AddLiveItem([this]() {
		sAudioStreamInfo audio = m_pConfig->GetAudioStreamInfo();
		if (audio.codec.empty())
			return cString(tr("Audio output: none"));
		return cString::sprintf(audio.passthrough ? tr("Audio output: passthrough, %d Hz, %d channels") : tr("Audio output: PCM, %d Hz, %d channels"),
			audio.outputSampleRate, audio.outputChannels);
	});
	AddLiveItem([this]() {
		cAudioStream *audio = m_pDevice->Audio();
		return cString::sprintf(tr("Audio device: %s"), audio ? audio->GetDeviceName().c_str() : "-");
	});
	AddLiveItem([this]() {
		cAudioStream *audio = m_pDevice->Audio();
		// in automatic mode the codecs of the sink's ELD are passed through
		if (audio && m_pDevice->IsPassthroughOn() && m_pConfig->ConfigAudioPassthroughAuto && audio->GetEldPassthroughMask() >= 0)
			return cString::sprintf(" %s (%s)", *m_pDevice->PassthroughString(), PassthroughCodecs(audio->GetEldPassthroughMask()).c_str());
		return cString::sprintf(" %s", *m_pDevice->PassthroughString());
	});

	// osd
#ifdef USE_GLES
	Add(new cOsdItem(cString::sprintf(tr("OSD: Using %s rendering"), m_pConfig->ConfigDisableOglOsd ? "software" : "hardware"), osUnknown, false));
#else
	Add(new cOsdItem(cString::sprintf(tr("OSD: Using software rendering")), osUnknown, false));
#endif
}

/**
 * Build setup menu
 */
void cDeviceSetupMenu::Create(int current)
{
	if (current == KEEP_CURRENT)
		current = Current();	// get current menu item index
	Clear();            	// clear the menu
	m_liveItems.clear();

	//
	// Overview: OK opens the section under the cursor
	//
	SetHasHotkeys(m_section == SECTION_OVERVIEW);
	if (m_section == SECTION_OVERVIEW) {
		m_overviewSections.clear();
		for (int section = 0; section < SECTION_COUNT; section++) {
			if (section == SECTION_PIP && !m_pDevice->UsePip())
				continue;
			Add(new cOsdItem(hk(SectionName(section))));
			m_overviewSections.push_back(section);
		}
	}

	//
	// General
	//
	if (m_section == SECTION_GENERAL) {
		Add(new cMenuEditBoolItem(tr("Hide main menu entry"), &m_cHideMainMenuEntry, trVDR("no"), trVDR("yes")));
	}

	//
	// Video
	//
	if (m_section == SECTION_VIDEO) {
		Add(new cMenuEditBoolItem(tr("Enable HDR"), &m_cVideoEnableHDR, trVDR("no"), trVDR("yes")));
		Add(new cMenuEditStraItem(tr("Display mode"), &m_cVideoDisplayMode, m_displayModePtrs.size(), m_displayModePtrs.data()));
		Add(new cMenuEditStraItem(tr("Enable fast channel switch"), &m_cVideoChannelSwitchMode, m_channelSwitchModePtrs.size(), m_channelSwitchModePtrs.data()));
		for (int i = 0; i < cDeviceConfig::ZOOM_LEVELS; i++)
			Add(new cMenuEditIntItem(cString::sprintf(tr("Zoom level %d (/1000)"), i + 1), &m_cZoomLevel[i], 0, cDeviceConfig::ZOOM_MAX, trVDR("off")));
	}

	//
	// Audio
	//
	if (m_section == SECTION_AUDIO) {
		Add(new cMenuEditBoolItem(tr("Volume control"), &m_cAudioSoftvol, tr("hardware"), tr("software")));
		Add(new cMenuEditBoolItem(tr("Enable stereo downmix"), &m_cAudioDownmix, trVDR("no"), trVDR("yes")));
		Add(new cMenuEditStraItem(tr("Passthrough"), &m_cAudioPassthroughMode, 3, m_passthroughModes));
		if (m_cAudioPassthroughMode == PASSTHROUGH_MANUAL) {
			Add(new cMenuEditBoolItem(tr(" AC-3 passthrough"), &m_cAudioPassthroughAC3, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" E-AC-3 passthrough"), &m_cAudioPassthroughEAC3, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" DTS passthrough"), &m_cAudioPassthroughDTS, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" DTS-HD passthrough"), &m_cAudioPassthroughDTSHD, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" TrueHD passthrough"), &m_cAudioPassthroughTrueHD, trVDR("no"), trVDR("yes")));
		}
		if (m_cAudioPassthroughMode != PASSTHROUGH_OFF)
			Add(new cMenuEditBoolItem(tr(" Disable automatic AES"), &m_cAudioDisableAutoAES, trVDR("no"), trVDR("yes")));
		Add(new cMenuEditIntItem(tr("Audio/Video delay (ms)"), &m_cAudioDelay, -1000, 1000));
		Add(new cMenuEditBoolItem(tr("Enable normalize volume"), &m_cAudioNormalize, trVDR("no"), trVDR("yes")));
		if (m_cAudioNormalize)
			Add(new cMenuEditIntItem(tr(" Max normalize factor (/1000)"), &m_cAudioMaxNormalize, 0, 10000));
		Add(new cMenuEditBoolItem(tr("Enable volume compression"), &m_cAudioCompression, trVDR("no"), trVDR("yes")));
		if (m_cAudioCompression)
			Add(new cMenuEditIntItem(tr(" Max compression factor (/1000)"), &m_cAudioMaxCompression, 0, 10000));
		Add(new cMenuEditIntItem(tr("Reduce stereo volume (/1000)"), &m_cAudioStereoDescent, 0, 1000));
	}

	//
	// Audio filter
	//
	if (m_section == SECTION_AUDIO_EQ) {
		Add(new cMenuEditBoolItem(tr("Enable audio equalizer"), &m_cAudioEq, trVDR("no"), trVDR("yes")));
		if (m_cAudioEq) {
			Add(new cMenuEditIntItem(tr(" 60 Hz band gain"),   &m_cAudioEqBand[0], -15, 1));
			Add(new cMenuEditIntItem(tr(" 72 Hz band gain"),   &m_cAudioEqBand[1], -15, 1));
			Add(new cMenuEditIntItem(tr(" 107 Hz band gain"),   &m_cAudioEqBand[2], -15, 1));
			Add(new cMenuEditIntItem(tr(" 150 Hz band gain"),   &m_cAudioEqBand[3], -15, 1));
			Add(new cMenuEditIntItem(tr(" 220 Hz band gain"),   &m_cAudioEqBand[4], -15, 1));
			Add(new cMenuEditIntItem(tr(" 310 Hz band gain"),   &m_cAudioEqBand[5], -15, 1));
			Add(new cMenuEditIntItem(tr(" 430 Hz band gain"),   &m_cAudioEqBand[6], -15, 1));
			Add(new cMenuEditIntItem(tr(" 620 Hz band gain"),   &m_cAudioEqBand[7], -15, 1));
			Add(new cMenuEditIntItem(tr(" 860 Hz band gain"),   &m_cAudioEqBand[8], -15, 1));
			Add(new cMenuEditIntItem(tr(" 1200 Hz band gain"),  &m_cAudioEqBand[9], -15, 1));
			Add(new cMenuEditIntItem(tr(" 1700 Hz band gain"),  &m_cAudioEqBand[10], -15, 1));
			Add(new cMenuEditIntItem(tr(" 2500 Hz band gain"),  &m_cAudioEqBand[11], -15, 1));
			Add(new cMenuEditIntItem(tr(" 3500 Hz band gain"),  &m_cAudioEqBand[12], -15, 1));
			Add(new cMenuEditIntItem(tr(" 4800 Hz band gain"),  &m_cAudioEqBand[13], -15, 1));
			Add(new cMenuEditIntItem(tr(" 7000 Hz band gain"),  &m_cAudioEqBand[14], -15, 1));
			Add(new cMenuEditIntItem(tr(" 9500 Hz band gain"),  &m_cAudioEqBand[15], -15, 1));
			Add(new cMenuEditIntItem(tr(" 13500 Hz band gain"), &m_cAudioEqBand[16], -15, 1));
			Add(new cMenuEditIntItem(tr(" 17200 Hz band gain"), &m_cAudioEqBand[17], -15, 1));
		}
	}

	//
	// PiP
	//
	if (m_section == SECTION_PIP && m_pDevice->UsePip()) {
		Add(new cMenuEditIntItem(tr("video scaling factor (%)"), &m_cPipScalePercent, 10, 100));
		Add(new cMenuEditIntItem(tr("video left (%)"), &m_cPipLeftPercent, 0, 100));
		Add(new cMenuEditIntItem(tr("video top (%)"), &m_cPipTopPercent, 0, 100));
		Add(new cMenuEditBoolItem(tr("use alternative position as default"), &m_cPipUseAlt, trVDR("no"), trVDR("yes")));
		Add(new cMenuEditIntItem(tr("alternative video scaling factor (%)"), &m_cPipAltScalePercent, 10, 100));
		Add(new cMenuEditIntItem(tr("alternative video left (%)"), &m_cPipAltLeftPercent, 0, 100));
		Add(new cMenuEditIntItem(tr("alternative video top (%)"), &m_cPipAltTopPercent, 0, 100));
	}

	//
	// Logging
	//
	if (m_section == SECTION_LOGGING) {
		Add(new cMenuEditBoolItem(tr("Enable logging"), &m_cLogDefault, trVDR("off"), trVDR("on")));
		if (m_cLogDefault) {
			Add(new cMenuEditBoolItem(tr(" Standard debug logs"), &m_cLogDebug_, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" DRM debug logs"), &m_cLogDRM, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" Codec debug logs"), &m_cLogCodec, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" AV Sync debug logs"), &m_cLogAVSync, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" Sound debug logs"), &m_cLogSound, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" FFmpeg debug logs"), &m_cLogFFmpeg, trVDR("no"), trVDR("yes")));
			if (m_cLogFFmpeg)
				Add(new cMenuEditStraItem(tr("  FFmpeg log level"), &m_cLogFFmpegLevel, FFMPEG_LOG_LEVELS, m_ffmpegLogLevels));
			Add(new cMenuEditBoolItem(tr(" Packet tracking logs"), &m_cLogPacket, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" OSD debug logs"), &m_cLogOSD, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" Grabbing debug logs"), &m_cLogGrab, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" Stillpicture debug logs"), &m_cLogStill, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" Trickspeed debug logs"), &m_cLogTrick, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" Mediaplayer debug logs"), &m_cLogMedia, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" OpenGL OSD debug logs"), &m_cLogGL, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" OpenGL OSD time measurement"), &m_cLogGLTime, trVDR("no"), trVDR("yes")));
			Add(new cMenuEditBoolItem(tr(" OpenGL OSD time measurement (extensive)"), &m_cLogGLTimeAll, trVDR("no"), trVDR("yes")));
		}
	}

	//
	// Statistics
	//
	if (m_section == SECTION_STATISTICS)
		AddStatistics();

	//
	// Expert settings
	//
	if (m_section == SECTION_EXPERT) {
		Add(SeparatorName(tr("Audio settings")));
		Add(new cMenuEditIntItem(tr("Adjust a/v buffer size (ms)"), &m_cAdditionalBufferLengthMs, - (m_pDevice->GetMinBufferFillLevelThresholdMs() - 100), 1000));

		AddLiveItem([this]() {
			return cString::sprintf(tr("  Current a/v buffer size: %dms"), m_pDevice->GetMinBufferFillLevelThresholdMs() + m_cAdditionalBufferLengthMs);
		});
		AddLiveItem([this]() {
			return cString::sprintf(tr("  Audio jitter: %dms, max: %dms"), m_pConfig->StatMaxShortTermAudioJitterMs.load(), m_pConfig->StatMaxLongTermAudioJitterMs.load());
		});
		AddLiveItem([this]() {
			return cString::sprintf(tr("  Video jitter: %dms, max: %dms"), m_pConfig->StatMaxShortTermVideoJitterMs.load(), m_pConfig->StatMaxLongTermVideoJitterMs.load());
		});

		Add(SeparatorName(tr("Video settings")));
		Add(new cMenuEditBoolItem(tr("Disable deinterlacer"), &m_cDisableDeint, trVDR("no"), trVDR("yes")));
		Add(new cMenuEditBoolItem(tr("Enable SW decoder fallback"), &m_cDecoderFallbackToSw, trVDR("no"), trVDR("yes")));
		if (m_cDecoderFallbackToSw) {
			Add(new cOsdItem(cString::sprintf(tr(" (minimum: %d)"), m_pConfig->GetDecoderNeedsMaxPackets() + 1), osUnknown, false));
			Add(new cMenuEditIntItem(tr(" fallback after num packets"), &m_cDecoderFallbackToSwNumPkts, 22));
		}
		Add(new cMenuEditIntItem(tr("H.264: Parse stream until num I-Frames"), &m_cParseH264StreamStart, 0, 20));
		Add(new cMenuEditIntItem(tr("H.264: Drop invalid P-Frames until num I-Frames"), &m_cDropInvalidH264PFrames, 0, 20));
#ifdef USE_GLES
		Add(SeparatorName(tr("OSD settings")));
		if (!m_pConfig->ConfigDisableOglOsd) {
			Add(new cMenuEditIntItem(tr("GPU mem used for image caching (MB)"), &m_cMaxSizeGPUImageCache, 0, 4000));
		}
#endif
		Add(SeparatorName(tr("Misc settings")));
		Add(new cMenuEditBoolItem(tr("Show channel switch duration"), &m_cShowChannelSwitchDurationMessage, trVDR("no"), trVDR("yes")));
	}

	SetCurrent(Get(current));	// restore selected menu entry
	Display();               	// display build menu
}

/**
 * Process key for setup menu.
 *
 * The overview lists the sections, OK opens the section under the cursor.
 * In a section, OK stores the settings and Back discards the changes, both
 * return to the overview. Back in the overview closes the setup menu.
 *
 * @param key          pressed key
 */
eOSState cDeviceSetupMenu::ProcessKey(eKeys key)
{
	int old_cAudioPassthroughMode = m_cAudioPassthroughMode;
	int old_cAudioNormalize = m_cAudioNormalize;
	int old_cAudioCompression = m_cAudioCompression;
	int old_cAudioEq = m_cAudioEq;
	int old_cLogDefault = m_cLogDefault;
	int old_cLogFFmpeg = m_cLogFFmpeg;
	int old_cDecoderFallbackToSw = m_cDecoderFallbackToSw;

	if (m_section == SECTION_OVERVIEW) {
		// cMenuSetupPage would store the settings and close the menu with OK
		if (key == kOk) {
			int current = Current();
			if (current >= 0 && current < (int)m_overviewSections.size())
				ShowSection(m_overviewSections[current]);
			return osContinue;
		}
		return cMenuSetupPage::ProcessKey(key);
	}

	// let an edited item handle the key first (e.g. OK in a string item)
	eOSState state = cOsdMenu::ProcessKey(key);

	if (state == osUnknown && key == kOk) {
		if (m_section != SECTION_STATISTICS)	// nothing to store
			Store();
		ShowSection(SECTION_OVERVIEW);
		return osContinue;
	}
	if (state == osBack) {
		// discard the changes of the section
		LoadValues();
		ShowSection(SECTION_OVERVIEW);
		return osContinue;
	}

	// update the statistics, while the menu is shown
	if (key == kNone && !HasSubMenu() && !m_liveItems.empty() && m_liveUpdateTimer.TimedOut()) {
		UpdateLiveItems();
		m_liveUpdateTimer.Set(LIVE_UPDATE_INTERVAL_MS);
	}

	if (key != kNone) {
		// update menu only, if something on the structure has changed
		// this is needed because VDR menus are evil slow
		if (old_cAudioPassthroughMode    != m_cAudioPassthroughMode ||
		    old_cAudioNormalize          != m_cAudioNormalize ||
		    old_cAudioCompression        != m_cAudioCompression ||
		    old_cAudioEq                 != m_cAudioEq ||
		    old_cLogDefault              != m_cLogDefault ||
		    old_cLogFFmpeg               != m_cLogFFmpeg ||
		    old_cDecoderFallbackToSw     != m_cDecoderFallbackToSw) {

			Create();	// update menu
		}
	}

	return state;
}

/**
 * Init the setup menu and build the overview
 */
cDeviceSetupMenu::cDeviceSetupMenu(cOutputDevice *device)
	: m_pDevice(device),
	  m_pConfig(m_pDevice->Config())
{
	BuildDisplayModeList();
	BuildChannelSwitchModeList();

	m_passthroughModes[PASSTHROUGH_OFF] = trVDR("off");
	m_passthroughModes[PASSTHROUGH_MANUAL] = tr("manual");
	m_passthroughModes[PASSTHROUGH_AUTO] = tr("automatic");

	LoadValues();
	Create();
}

/**
 * Copy the setup values of the config into the menu
 *
 * Also used to discard the changes of a section.
 */
void cDeviceSetupMenu::LoadValues(void)
{
	//
	// General
	//
	m_cHideMainMenuEntry = m_pConfig->ConfigHideMainMenuEntry;

	//
	// Video
	//
	m_cVideoEnableHDR          = m_pConfig->ConfigVideoEnableHDR;
	m_cVideoDisplayMode        = m_pConfig->ConfigVideoDisplayMode;
	m_cVideoChannelSwitchMode  = m_pConfig->ConfigVideoChannelSwitchMode;
	for (int i = 0; i < cDeviceConfig::ZOOM_LEVELS; i++)
		m_cZoomLevel[i] = m_pConfig->ConfigZoomLevel[i];

	//
	// Audio
	//
	m_cAudioSoftvol            = m_pConfig->ConfigAudioSoftvol;
	m_cAudioDownmix            = m_pConfig->ConfigAudioDownmix;
	m_cAudioPassthroughMode    = !m_pConfig->ConfigAudioPassthroughState ? PASSTHROUGH_OFF :
	                             m_pConfig->ConfigAudioPassthroughAuto ? PASSTHROUGH_AUTO : PASSTHROUGH_MANUAL;
	m_cAudioPassthroughAC3     = m_pConfig->ConfigAudioPassthroughMask & CODEC_AC3;
	m_cAudioPassthroughEAC3    = m_pConfig->ConfigAudioPassthroughMask & CODEC_EAC3;
	m_cAudioPassthroughDTS     = m_pConfig->ConfigAudioPassthroughMask & CODEC_DTS;
	m_cAudioPassthroughTrueHD  = m_pConfig->ConfigAudioPassthroughMask & CODEC_TRUEHD;
	m_cAudioPassthroughDTSHD   = m_pConfig->ConfigAudioPassthroughMask & CODEC_DTSHD;
	m_cAudioDisableAutoAES     = m_pConfig->ConfigAudioDisableAutoAES;
	m_cAudioDelay              = m_pConfig->ConfigVideoAudioDelayMs;
	m_cAudioNormalize          = m_pConfig->ConfigAudioNormalize;
	m_cAudioMaxNormalize       = m_pConfig->ConfigAudioMaxNormalize;
	m_cAudioCompression        = m_pConfig->ConfigAudioCompression;
	m_cAudioMaxCompression     = m_pConfig->ConfigAudioMaxCompression;
	m_cAudioStereoDescent      = m_pConfig->ConfigAudioStereoDescent;

	//
	// Audio equalizer
	//
	m_cAudioEq = m_pConfig->ConfigAudioEq;
	for (int i = 0; i < 18; i++) {
		m_cAudioEqBand[i] = m_pConfig->ConfigAudioEqBand[i];
	}

	//
	// Picture-in-picture
	//
	m_cPipScalePercent = m_pConfig->ConfigPipScalePercent;
	m_cPipLeftPercent = m_pConfig->ConfigPipLeftPercent;
	m_cPipTopPercent = m_pConfig->ConfigPipTopPercent;
	m_cPipUseAlt = m_pConfig->ConfigPipUseAlt;
	m_cPipAltScalePercent = m_pConfig->ConfigPipAltScalePercent;
	m_cPipAltLeftPercent = m_pConfig->ConfigPipAltLeftPercent;
	m_cPipAltTopPercent = m_pConfig->ConfigPipAltTopPercent;

	//
	// Logging
	//
	m_cLogDefault   = m_pConfig->ConfigLogState;
	m_cLogDebug_    = m_pConfig->ConfigLogLevels & L_DEBUG;
	m_cLogDRM       = m_pConfig->ConfigLogLevels & L_DRM;
	m_cLogCodec     = m_pConfig->ConfigLogLevels & L_CODEC;
	m_cLogAVSync    = m_pConfig->ConfigLogLevels & L_AV_SYNC;
	m_cLogSound     = m_pConfig->ConfigLogLevels & L_SOUND;
	m_cLogFFmpeg    = m_pConfig->ConfigLogLevels & L_FFMPEG;
	m_cLogFFmpegLevel = m_pConfig->ConfigLogFFmpegLevel;
	m_cLogPacket    = m_pConfig->ConfigLogLevels & L_PACKET;
	m_cLogOSD       = m_pConfig->ConfigLogLevels & L_OSD;
	m_cLogGrab      = m_pConfig->ConfigLogLevels & L_GRAB;
	m_cLogStill     = m_pConfig->ConfigLogLevels & L_STILL;
	m_cLogTrick     = m_pConfig->ConfigLogLevels & L_TRICK;
	m_cLogMedia     = m_pConfig->ConfigLogLevels & L_MEDIA;
	m_cLogGL        = m_pConfig->ConfigLogLevels & L_OPENGL;
	m_cLogGLTime    = m_pConfig->ConfigLogLevels & L_OPENGL_TIME;
	m_cLogGLTimeAll = m_pConfig->ConfigLogLevels & L_OPENGL_TIME_ALL;

	//
	// Expert settings
	//
	m_cAdditionalBufferLengthMs= m_pConfig->ConfigAdditionalBufferLengthMs;
	m_cDisableDeint = m_pConfig->ConfigDisableDeint;
	m_cDecoderFallbackToSw = m_pConfig->ConfigDecoderFallbackToSw;
	m_cDecoderFallbackToSwNumPkts = m_pConfig->ConfigDecoderFallbackToSwNumPkts;
	m_cParseH264StreamStart = m_pConfig->ConfigParseH264StreamStart;
	m_cDropInvalidH264PFrames = m_pConfig->ConfigDropInvalidH264PFrames;
#ifdef USE_GLES
	m_cMaxSizeGPUImageCache = m_pConfig->ConfigMaxSizeGPUImageCache;
#endif
	m_cShowChannelSwitchDurationMessage = m_pConfig->ConfigShowChannelSwitchDurationMessage;
}

void cDeviceSetupMenu::BuildDisplayModeList(void)
{
	m_displayMode.clear();

	// CONFIG_DISPLAY_MODE_DEFAULT = 0
	m_displayMode.push_back(*cString::sprintf(tr("default %dx%d@%.2f%s"),
		m_pConfig->AutoDetectedDrmMode.width,
		m_pConfig->AutoDetectedDrmMode.height,
		m_pConfig->AutoDetectedDrmMode.refreshRateHz,
		m_pConfig->AutoDetectedDrmMode.interlaced ? "i" : ""));

	// CONFIG_DISPLAY_MODE_FOLLOW_VIDEO = 1
	if (m_pConfig->ConfigVideoDisplayMode == CONFIG_DISPLAY_MODE_FOLLOW_VIDEO)
		m_displayMode.push_back(*cString::sprintf(tr("match video %dx%d@%.2f%s"),
			m_pConfig->CurrentDrmMode.width,
			m_pConfig->CurrentDrmMode.height,
			m_pConfig->CurrentDrmMode.refreshRateHz,
			m_pConfig->CurrentDrmMode.interlaced ? "i" : ""));
	else
		m_displayMode.push_back(tr("match video"));

	// CONFIG_DISPLAY_MODE_FOLLOW_VIDEO_INTERLACED = 2
	if (m_pConfig->ConfigVideoDisplayMode == CONFIG_DISPLAY_MODE_FOLLOW_VIDEO_INTERLACED)
		m_displayMode.push_back(*cString::sprintf(tr("match video (interlaced) %dx%d@%.2f%s"),
			m_pConfig->CurrentDrmMode.width,
			m_pConfig->CurrentDrmMode.height,
			m_pConfig->CurrentDrmMode.refreshRateHz,
			m_pConfig->CurrentDrmMode.interlaced ? "i" : ""));
	else
		m_displayMode.push_back(tr("match video (interlaced)"));

	// CONFIG_DISPLAY_MODE_MANUAL = 3
	for (size_t i = CONFIG_DISPLAY_MODE_MANUAL; i < m_pConfig->CollectedDrmModes.size() + CONFIG_DISPLAY_MODE_MANUAL; i++) {
		m_displayMode.push_back(*cString::sprintf("%dx%d@%.2f%s",
			m_pConfig->CollectedDrmModes[i - CONFIG_DISPLAY_MODE_MANUAL].width,
			m_pConfig->CollectedDrmModes[i - CONFIG_DISPLAY_MODE_MANUAL].height,
			m_pConfig->CollectedDrmModes[i - CONFIG_DISPLAY_MODE_MANUAL].refreshRateHz,
			m_pConfig->CollectedDrmModes[i - CONFIG_DISPLAY_MODE_MANUAL].interlaced ? "i" : ""));
	}

	m_displayModePtrs.clear();
	for (auto &s : m_displayMode)
		m_displayModePtrs.push_back(s.c_str());
}

void cDeviceSetupMenu::BuildChannelSwitchModeList(void)
{
	m_channelSwitchMode.clear();

	m_channelSwitchMode.push_back(trVDR("off"));
	m_channelSwitchMode.push_back(tr("video"));
	m_channelSwitchMode.push_back(tr("video and audio"));

	for (auto &s : m_channelSwitchMode)
		m_channelSwitchModePtrs.push_back(s.c_str());
}

/**
 * Store settings
 */
void cDeviceSetupMenu::Store(void)
{
	//
	// General
	//
	SetupStore("HideMainMenuEntry", m_pConfig->ConfigHideMainMenuEntry = m_cHideMainMenuEntry);

	//
	// Video
	//
	SetupStore("VideoEnableHDR", m_pConfig->ConfigVideoEnableHDR = m_cVideoEnableHDR);
	bool displayModeChanged = m_pConfig->ConfigVideoDisplayMode != m_cVideoDisplayMode;
	m_pConfig->ConfigVideoDisplayMode = m_cVideoDisplayMode;
	// only save default and auto adjusted modes
	if (m_pConfig->ConfigVideoDisplayMode < CONFIG_DISPLAY_MODE_MANUAL)
		SetupStore("VideoDisplayMode", m_cVideoDisplayMode);
	SetupStore("VideoChannelSwitchMode", m_pConfig->ConfigVideoChannelSwitchMode = m_cVideoChannelSwitchMode);
	m_pDevice->SetChannelSwitchMode(static_cast<ChannelSwitchMode>(m_pConfig->ConfigVideoChannelSwitchMode));
	for (int i = 0; i < cDeviceConfig::ZOOM_LEVELS; i++)
		SetupStore(cString::sprintf("ZoomLevel%d", i + 1), m_pConfig->ConfigZoomLevel[i] = m_cZoomLevel[i]);
	// a disabled zoom level can't stay active
	if (m_pDevice->GetZoomLevel() && !m_pDevice->GetZoom())
		m_pDevice->SetZoomLevel(0);

	//
	// Audio
	//
	SetupStore("AudioSoftvol", m_pConfig->ConfigAudioSoftvol = m_cAudioSoftvol);
	// FIXME: can handle more audio state changes here
	// downmix changed reset audio, to get change direct
	bool downmixChanged = m_pConfig->ConfigAudioDownmix != m_cAudioDownmix;
	SetupStore("AudioDownmix", m_pConfig->ConfigAudioDownmix = m_cAudioDownmix);
	if (downmixChanged)
		m_pDevice->ResetChannelId();
	m_pConfig->ConfigAudioPassthroughMask = (m_cAudioPassthroughAC3 ? CODEC_AC3 : 0)
	                                      | (m_cAudioPassthroughEAC3 ? CODEC_EAC3 : 0)
	                                      | (m_cAudioPassthroughDTS ? CODEC_DTS : 0)
	                                      | (m_cAudioPassthroughTrueHD ? CODEC_TRUEHD : 0)
	                                      | (m_cAudioPassthroughDTSHD ? CODEC_DTSHD : 0);
	m_pConfig->ConfigAudioPassthroughState = m_cAudioPassthroughMode != PASSTHROUGH_OFF;
	// switching the passthrough off keeps the automatic mode for the passthrough toggle
	if (m_cAudioPassthroughMode != PASSTHROUGH_OFF)
		m_pConfig->ConfigAudioPassthroughAuto = m_cAudioPassthroughMode == PASSTHROUGH_AUTO;
	// the automatic mode is stored as an additional bit, the sign is the on/off state
	int passthrough = m_pConfig->ConfigAudioPassthroughMask | (m_pConfig->ConfigAudioPassthroughAuto ? CODEC_PASSTHROUGH_AUTO : 0);
	SetupStore("AudioPassthrough", m_pConfig->ConfigAudioPassthroughState ? passthrough : -passthrough);
	m_pDevice->SetPassthroughOn(m_pConfig->ConfigAudioPassthroughState);
	SetupStore("AudioDisableAutoAES", m_pConfig->ConfigAudioDisableAutoAES = m_cAudioDisableAutoAES);
	SetupStore("AudioDelay", m_pConfig->ConfigVideoAudioDelayMs = m_cAudioDelay);
	SetupStore("AudioNormalize", m_pConfig->ConfigAudioNormalize = m_cAudioNormalize);
	SetupStore("AudioMaxNormalize", m_pConfig->ConfigAudioMaxNormalize = m_cAudioMaxNormalize);
	SetupStore("AudioCompression", m_pConfig->ConfigAudioCompression = m_cAudioCompression);
	SetupStore("AudioMaxCompression", m_pConfig->ConfigAudioMaxCompression = m_cAudioMaxCompression);
	SetupStore("AudioStereoDescent", m_pConfig->ConfigAudioStereoDescent = m_cAudioStereoDescent);

	//
	// Audio equalizer
	//
	SetupStore("AudioEq", m_pConfig->ConfigAudioEq = m_cAudioEq);
	SetupStore("AudioEqBand01b", m_pConfig->ConfigAudioEqBand[0]  = m_cAudioEqBand[0]);
	SetupStore("AudioEqBand02b", m_pConfig->ConfigAudioEqBand[1]  = m_cAudioEqBand[1]);
	SetupStore("AudioEqBand03b", m_pConfig->ConfigAudioEqBand[2]  = m_cAudioEqBand[2]);
	SetupStore("AudioEqBand04b", m_pConfig->ConfigAudioEqBand[3]  = m_cAudioEqBand[3]);
	SetupStore("AudioEqBand05b", m_pConfig->ConfigAudioEqBand[4]  = m_cAudioEqBand[4]);
	SetupStore("AudioEqBand06b", m_pConfig->ConfigAudioEqBand[5]  = m_cAudioEqBand[5]);
	SetupStore("AudioEqBand07b", m_pConfig->ConfigAudioEqBand[6]  = m_cAudioEqBand[6]);
	SetupStore("AudioEqBand08b", m_pConfig->ConfigAudioEqBand[7]  = m_cAudioEqBand[7]);
	SetupStore("AudioEqBand09b", m_pConfig->ConfigAudioEqBand[8]  = m_cAudioEqBand[8]);
	SetupStore("AudioEqBand10b", m_pConfig->ConfigAudioEqBand[9]  = m_cAudioEqBand[9]);
	SetupStore("AudioEqBand11b", m_pConfig->ConfigAudioEqBand[10] = m_cAudioEqBand[10]);
	SetupStore("AudioEqBand12b", m_pConfig->ConfigAudioEqBand[11] = m_cAudioEqBand[11]);
	SetupStore("AudioEqBand13b", m_pConfig->ConfigAudioEqBand[12] = m_cAudioEqBand[12]);
	SetupStore("AudioEqBand14b", m_pConfig->ConfigAudioEqBand[13] = m_cAudioEqBand[13]);
	SetupStore("AudioEqBand15b", m_pConfig->ConfigAudioEqBand[14] = m_cAudioEqBand[14]);
	SetupStore("AudioEqBand16b", m_pConfig->ConfigAudioEqBand[15] = m_cAudioEqBand[15]);
	SetupStore("AudioEqBand17b", m_pConfig->ConfigAudioEqBand[16] = m_cAudioEqBand[16]);
	SetupStore("AudioEqBand18b", m_pConfig->ConfigAudioEqBand[17] = m_cAudioEqBand[17]);

	//
	// Picture-in-picture
	//
	bool pipChanged = m_pConfig->ConfigPipScalePercent    != m_cPipScalePercent ||
	                  m_pConfig->ConfigPipLeftPercent     != m_cPipLeftPercent ||
	                  m_pConfig->ConfigPipTopPercent      != m_cPipTopPercent ||
	                  m_pConfig->ConfigPipUseAlt          != m_cPipUseAlt ||
	                  m_pConfig->ConfigPipAltScalePercent != m_cPipAltScalePercent ||
	                  m_pConfig->ConfigPipAltLeftPercent  != m_cPipAltLeftPercent ||
	                  m_pConfig->ConfigPipAltTopPercent   != m_cPipAltTopPercent;

	SetupStore("PipScalePercent", m_pConfig->ConfigPipScalePercent = m_cPipScalePercent);
	SetupStore("PipLeftPercent", m_pConfig->ConfigPipLeftPercent = m_cPipLeftPercent);
	SetupStore("PipTopPercent", m_pConfig->ConfigPipTopPercent = m_cPipTopPercent);
	SetupStore("PipUseAlt", m_pConfig->ConfigPipUseAlt = m_cPipUseAlt);
	SetupStore("PipAltScalePercent", m_pConfig->ConfigPipAltScalePercent = m_cPipAltScalePercent);
	SetupStore("PipAltLeftPercent", m_pConfig->ConfigPipAltLeftPercent = m_cPipAltLeftPercent);
	SetupStore("PipAltTopPercent", m_pConfig->ConfigPipAltTopPercent = m_cPipAltTopPercent);
	if (m_pDevice->UsePip() && pipChanged)
		m_pDevice->PipSetSize();

	//
	// Logging
	//
	m_pConfig->ConfigLogLevels =
		(m_cLogDebug_    ? L_DEBUG : 0) |
		(m_cLogDRM       ? L_DRM : 0) |
		(m_cLogCodec     ? L_CODEC : 0) |
		(m_cLogAVSync    ? L_AV_SYNC : 0) |
		(m_cLogSound     ? L_SOUND : 0) |
		(m_cLogFFmpeg    ? L_FFMPEG : 0) |
		(m_cLogPacket    ? L_PACKET : 0) |
		(m_cLogOSD       ? L_OSD : 0) |
		(m_cLogGrab      ? L_GRAB : 0) |
		(m_cLogStill     ? L_STILL : 0) |
		(m_cLogTrick     ? L_TRICK : 0) |
		(m_cLogMedia     ? L_MEDIA : 0) |
		(m_cLogGL        ? L_OPENGL : 0) |
		(m_cLogGLTime    ? L_OPENGL_TIME : 0) |
		(m_cLogGLTimeAll ? L_OPENGL_TIME_ALL : 0);
	m_pConfig->ConfigLogState = m_cLogDefault;
	SetupStore("LogFFmpegLevel", m_pConfig->ConfigLogFFmpegLevel = m_cLogFFmpegLevel);
	LOGGER->SetFFmpegLogLevel(cDeviceConfig::FFmpegLogLevel(m_pConfig->ConfigLogFFmpegLevel));
	if (m_pConfig->ConfigLogState) {
		SetupStore("LogLevel", m_pConfig->ConfigLogLevels);
		if (LOGGER->GetLogLevel() != m_pConfig->ConfigLogLevels) {
			m_pConfig->PrintLogLevel(m_pConfig->ConfigLogLevels);
			LOGGER->SetLogLevel(m_pConfig->ConfigLogLevels);
		}
	} else {
		SetupStore("LogLevel", -m_pConfig->ConfigLogLevels);
		LOGGER->SetLogLevel(0);
	}

	//
	// Expert settings
	//
	SetupStore("AdditionalBufferLengthMs", m_pConfig->ConfigAdditionalBufferLengthMs = m_cAdditionalBufferLengthMs);
	SetupStore("DisableDeint", m_pConfig->ConfigDisableDeint = m_cDisableDeint);
	if (m_pConfig->ConfigDisableDeint) {
		LOGDEBUG("Disable deinterlacer!");
	}
	m_pDevice->SetDisableDeint();
	SetupStore("DecoderFallbackToSw", m_pConfig->ConfigDecoderFallbackToSw = m_cDecoderFallbackToSw);
	SetupStore("DecoderFallbackToSwNumPkts", m_pConfig->ConfigDecoderFallbackToSwNumPkts = m_cDecoderFallbackToSwNumPkts);
	m_pDevice->SetDecoderFallbackToSw(m_pConfig->ConfigDecoderFallbackToSw);
	SetupStore("ParseH264StreamStart", m_pConfig->ConfigParseH264StreamStart = m_cParseH264StreamStart);
	SetupStore("DropInvalidH264PFrames", m_pConfig->ConfigDropInvalidH264PFrames = m_cDropInvalidH264PFrames);
#ifdef USE_GLES
	SetupStore("MaxSizeGPUImageCache", m_pConfig->ConfigMaxSizeGPUImageCache = m_cMaxSizeGPUImageCache);
#endif
	SetupStore("ShowChannelSwitchDurationMessage", m_pConfig->ConfigShowChannelSwitchDurationMessage = m_cShowChannelSwitchDurationMessage);

	//
	// Apply the settings to the output objects
	//
	// They only exist in attached state, the setup menu may still be open after a detach
	// (e.g. via SVDRP). On the next attach, they are created with the settings of the config.
	if (m_pDevice->IsDetached())
		return;

	m_pDevice->SetEnableHdr(m_pConfig->ConfigVideoEnableHDR);

	// the audio stream is recreated on an attach, so don't store its pointer in the menu
	cAudioStream *audio = m_pDevice->Audio();
	audio->SetSoftvol(m_pConfig->ConfigAudioSoftvol);
	audio->SetDownmix(m_pConfig->ConfigAudioDownmix);
	audio->SetAutoAES(!m_pConfig->ConfigAudioDisableAutoAES);
	audio->SetNormalize(m_pConfig->ConfigAudioNormalize, m_pConfig->ConfigAudioMaxNormalize);
	audio->SetCompression(m_pConfig->ConfigAudioCompression, m_pConfig->ConfigAudioMaxCompression);
	audio->SetStereoDescent(m_pConfig->ConfigAudioStereoDescent);
	audio->SetEqualizer(m_pConfig->ConfigAudioEq, m_pConfig->ConfigAudioEqBand);

	if (displayModeChanged)
		m_pDevice->SetDisplayMode(m_pConfig->ConfigVideoDisplayMode);
}
