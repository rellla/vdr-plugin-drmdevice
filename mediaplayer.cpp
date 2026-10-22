// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mediaplayer.cpp
 * Mediaplayer
 *
 * This file defines all classes used for the integrated mediaplayer
 *    - cDrmDevicePlayer (cPlayer)
 *    - cDrmDeviceControl (cControl)
 *
 * @copyright 2020 zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <vdr/config.h>
#include <vdr/i18n.h>
#include <vdr/player.h>
#include <vdr/plugin.h>
#include <vdr/remux.h>

#include "audio.h"
#include "device.h"
#include "logger.h"
#include "mediaplayer.h"
#include "mediaresume.h"
#include "menu.h"
#include "misc.h"

/**
 * @addtogroup mediaplayer
 * @{
 */
cDrmDeviceControl *cDrmDeviceControl::m_pControl = NULL; ///< Control instance
cDrmDevicePlayer *cDrmDeviceControl::m_pPlayer = NULL; //< Player instance
/** @} */

/*****************************************************************************
 * cPlaylistEntry
 ****************************************************************************/

/**
 * Builds the playlist entry from a file name
 *
 * @param path    full path name to the file
 */
cPlaylistEntry::cPlaylistEntry(const std::string &path)
	: m_path(path)
{
	m_file = m_path.substr(m_path.find_last_of("/") +1, std::string::npos);

	std::string subString = m_path.substr(0, m_path.find_last_of("/"));
	m_subFolder = subString.substr(subString.find_last_of("/") +1, std::string::npos);

	std::string folderString = m_path.substr(0, subString.find_last_of("/"));
	m_folder = folderString.substr(folderString.find_last_of("/") +1, std::string::npos);
}

/**
 * Compose a full-path-string for the OSD entry
 */
std::string cPlaylistEntry::OsdItemString(void)
{
	return (m_folder + " - " + m_subFolder + " - " + m_file);
}

/*****************************************************************************
 * cDrmDevicePlayer (cPlayer mediaplayer)
 ****************************************************************************/

/**
 * Timebase of all packets sent to the device
 *
 * The device works with the 90kHz timebase of VDR's PES packets, e.g. the
 * renderer converts the frame pts to ms with it.
 *
 * @ingroup mediaplayer
 */
static constexpr AVRational PES_TIMEBASE = { 1, PTSTICKS };

/**
 * Pts of the start of a media file in the PES timebase
 *
 * The device handles negative pts as a pts wrap, so the pts of the packets
 * are moved by 10s. This keeps them positive, even if a stream starts a bit
 * before the start time of the file.
 *
 * @ingroup mediaplayer
 */
static constexpr int64_t MEDIA_START_PTS = 10LL * PTSTICKS;

/**
 * Convert the timestamps of a packet to the PES timebase
 *
 * @param pkt             packet to convert
 * @param timebase        timebase of the packet's stream
 * @param startTime       start time of the file (AV_TIME_BASE units)
 *
 * @ingroup mediaplayer
 */
static void RescaleToPesTimebase(AVPacket *pkt, AVRational timebase, int64_t startTime)
{
	av_packet_rescale_ts(pkt, timebase, PES_TIMEBASE);

	int64_t offset = MEDIA_START_PTS - av_rescale_q(startTime, AV_TIME_BASE_Q, PES_TIMEBASE);
	if (pkt->pts != AV_NOPTS_VALUE)
		pkt->pts += offset;
	if (pkt->dts != AV_NOPTS_VALUE)
		pkt->dts += offset;
}

/**
 * Check, if an audio codec is passed in a Dolby track
 *
 * Like in VDR, the formats, which can be passed through, are Dolby tracks.
 * They are preferred, if Setup.CurrentDolby is set.
 *
 * @param codecId         codec of the audio stream
 *
 * @ingroup mediaplayer
 */
static bool IsDolbyTrack(AVCodecID codecId)
{
	return codecId == AV_CODEC_ID_AC3 || codecId == AV_CODEC_ID_EAC3 ||
	       codecId == AV_CODEC_ID_TRUEHD || codecId == AV_CODEC_ID_DTS;
}

/**
 * Describe an audio stream for the track menu
 *
 * @param stream          audio stream
 * @return                e.g. "AC-3 5.1" or "AAC stereo - Commentary"
 *
 * @ingroup mediaplayer
 */
static std::string AudioTrackDescription(const AVStream *stream)
{
	const AVCodecParameters *par = stream->codecpar;
	std::string description;

	switch (par->codec_id) {
		case AV_CODEC_ID_AC3:    description = "AC-3"; break;
		case AV_CODEC_ID_EAC3:   description = "E-AC-3"; break;
		case AV_CODEC_ID_TRUEHD: description = "TrueHD"; break;
		case AV_CODEC_ID_DTS:    description = "DTS"; break;
		case AV_CODEC_ID_MP2:    description = "MP2"; break;
		case AV_CODEC_ID_MP3:    description = "MP3"; break;
		default: {
			const AVCodecDescriptor *desc = avcodec_descriptor_get(par->codec_id);
			description = desc ? desc->name : "?";
			std::transform(description.begin(), description.end(), description.begin(), ::toupper);
			break;
		}
	}

	switch (par->ch_layout.nb_channels) {
		case 0:  break;
		case 1:  description += " mono"; break;
		case 2:  description += " stereo"; break;
		case 6:  description += " 5.1"; break;
		case 8:  description += " 7.1"; break;
		default: description += " " + std::to_string(par->ch_layout.nb_channels) + "ch"; break;
	}

	const AVDictionaryEntry *title = av_dict_get(stream->metadata, "title", nullptr, 0);
	if (title && *title->value)
		description += std::string(" - ") + title->value;

	return description;
}

/**
 * Get the language of a stream
 *
 * @param stream          the stream
 * @return                ISO 639-2 language code or an empty string
 *
 * @ingroup mediaplayer
 */
static const char *StreamLanguage(const AVStream *stream)
{
	const AVDictionaryEntry *language = av_dict_get(stream->metadata, "language", nullptr, 0);

	return language && strcmp(language->value, "und") ? language->value : "";
}

/**
 * Returns true, if the playlist is a m3u playlist
 *
 * @param source          file or playlist to be played
 *
 * @ingroup mediaplayer
 */
static bool IsM3UPlaylist(char *source)
{
	return (strcasestr(source, ".M3U") && !strcasestr(source, ".M3U8"));
}

/**
 * Trick speeds (like cDvbPlayer)
 *
 * Negative values are slow motion, positive values fast forward/backward.
 */
const int cDrmDevicePlayer::SPEEDS[] = { 0, -2, -4, -8, 1, 2, 4, 12, 0 };

/**
 * Minimum distance of the frames in fast forward/backward mode
 *
 * Like in cDvbPlayer, the next frame is the next I-frame, which is at
 * least this distance away.
 *
 * @ingroup mediaplayer
 */
static constexpr int64_t TRICK_STEP_MS = 400;

/**
 * Number of seconds to wait at the begin/end of a file, if the device
 * doesn't play the last frame (like in cDvbPlayer)
 *
 * @ingroup mediaplayer
 */
static constexpr int MAX_STUCK_AT_EOF_MS = 3000;

/**
 * Number of seconds to back up when resuming an interrupted replay (like cDvbPlayer)
 *
 * @ingroup mediaplayer
 */
static constexpr int64_t RESUME_BACKUP_MS = 10000;

/**
 * Create a new player for a file or playlist
 *
 * @param url             file or playlist to be played
 * @param device          pointer to device
 */
cDrmDevicePlayer::cDrmDevicePlayer(const char *url, cOutputDevice *device)
	: cPlayer(pmAudioVideo),
	  cThread("drmdevice mediaplayer"),
	  m_pDevice(device)
{
	m_pSource = (char *) malloc(1 + strlen(url));
	strcpy(m_pSource, url);

	if (IsM3UPlaylist(m_pSource)) {
		ReadPlaylist(m_pSource);
		m_pCurrentEntry = m_pFirstEntry;
	} else
		m_pFirstEntry = m_pCurrentEntry = nullptr;

	LOGDEBUG2(L_MEDIA, "mediaplayer: %s: player started", __FUNCTION__);
}

cDrmDevicePlayer::~cDrmDevicePlayer()
{
	// stop the player thread before freeing the data it uses
	m_stopFile = true;
	Cancel(3);
	Detach();
	CloseFile();
	free(m_pSource);

	while (m_pFirstEntry) {
		cPlaylistEntry *entry = m_pFirstEntry;
		m_pFirstEntry = entry->GetNextEntry();
		delete entry;
		m_entries--;
	}

	LOGDEBUG2(L_MEDIA, "mediaplayer: %s: player stopped", __FUNCTION__);
}

/**
 * Start or stop the player thread
 *
 * Called right after the player has been attached or before it gets detached
 *
 * @param on         true starts the player, false stops it
 */
void cDrmDevicePlayer::Activate(bool on)
{
	LOGDEBUG2(L_MEDIA, "mediaplayer: %s: %s", __FUNCTION__, on ? "On" : "Off");
	if (on)
		Start();
	else {
		m_stopFile = true;
		Cancel(3);
	}
}

/**
 * Main thread action
 *
 * Plays the file or all files of the playlist.
 */
void cDrmDevicePlayer::Action(void)
{
	LOGDEBUG2(L_MEDIA, "mediaplayer: %s:", __FUNCTION__);
	m_noModify = false;

	if (IsM3UPlaylist(m_pSource)) {
		while (m_pCurrentEntry && Running()) {
			PlayFile(m_pCurrentEntry.load()->GetPath().c_str());

			if (!m_noModify) {
				m_pCurrentEntry = m_pCurrentEntry.load()->GetNextEntry();

				if (m_random) {
					std::uniform_int_distribution<int> distribution(0, m_entries - 1);
					SetEntry(distribution(m_randomGenerator));
				}
			}
			m_noModify = false;
		}
	} else
		PlayFile(m_pSource);

	if (Running() && cDrmDeviceControl::Control())
		cDrmDeviceControl::Control()->Close();
}

/**
 * Read the playlist file
 *
 * @param playlist       full path to the playlist
 */
void cDrmDevicePlayer::ReadPlaylist(const char *playlist)
{
	std::ifstream inputFile;
	cPlaylistEntry *lastEntry = nullptr;
	m_entries = 0;

	inputFile.open(playlist);
	if (!inputFile.good()) {
		LOGERROR("mediaplayer: %s: open PL %s failed", __FUNCTION__, playlist);
		return;
	}

	while (!inputFile.eof()) {
		std::string s;
		getline(inputFile, s);
		if (s.size() && s.compare(0, 1, "#")) {
			cPlaylistEntry *entry = new cPlaylistEntry(s);

			if (!lastEntry)
				m_pFirstEntry = entry;
			else
				lastEntry->SetNextEntry(entry);

			lastEntry = entry;
			m_entries++;
		}
	}

	inputFile.close();
}

/**
 * Set the current entry to play
 *
 * @param index       list index (in random mode) or menu item index cOsdMenu::Current()
 */
void cDrmDevicePlayer::SetEntry(int index)
{
	cPlaylistEntry *entry;
	entry = m_pFirstEntry;

	for (int i = 0; i < index && entry; i++) {
		entry = entry->GetNextEntry();
	}

	m_pCurrentEntry = entry;
	m_noModify = true;
	m_stopFile = true;
}

/**
 * Stop the current file and continue with the next entry of the playlist
 *
 * Does nothing without a playlist.
 */
void cDrmDevicePlayer::NextEntry(void)
{
	if (m_pCurrentEntry)
		m_stopFile = true;
}

/**
 * Stop the current file and continue with the previous entry of the playlist
 *
 * The first entry is played again. Does nothing without a playlist.
 */
void cDrmDevicePlayer::PrevEntry(void)
{
	if (!m_pCurrentEntry)
		return;

	int index = 0;
	for (cPlaylistEntry *entry = m_pFirstEntry; entry && entry != m_pCurrentEntry; entry = entry->GetNextEntry())
		index++;

	SetEntry(index > 0 ? index - 1 : 0);
}

/**
 * Get the current file
 *
 * @param[out] title     title of the file
 * @param[out] path      path of the file
 * @return               number of the file, which changes with every new file (0, if none was played yet)
 */
int cDrmDevicePlayer::GetFile(std::string &title, std::string &path)
{
	std::lock_guard<std::mutex> lock(m_fileMutex);
	title = m_fileTitle;
	path = m_filePath;

	return m_fileNumber;
}

/*****************************************************************************
 * cDrmDevicePlayer - file handling and feeding
 ****************************************************************************/

/**
 * Open a file and the decoders for its streams
 *
 * @param url       file to open
 * @retval true     the file was opened
 * @retval false    the file can't be played
 */
bool cDrmDevicePlayer::OpenFile(const char *url)
{
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59,0,100)
	AVCodec *videoCodec;
#else
	const AVCodec *videoCodec;
#endif
	AVFormatContext *format = avformat_alloc_context();
	if (avformat_open_input(&format, url, NULL, NULL) != 0) {
		LOGERROR("mediaplayer: %s: Could not open file '%s'", __FUNCTION__, url);
		return false;
	}
#ifdef MEDIA_DEBUG
	av_dump_format(format, -1, url, 0);
#endif
	if (avformat_find_stream_info(format, NULL) < 0) {
		LOGERROR("mediaplayer: %s: Could not retrieve stream info from file '%s'", __FUNCTION__, url);
		avformat_close_input(&format);
		return false;
	}

	bool hasAudio = false;
	for (unsigned int i = 0; i < format->nb_streams; i++)
		hasAudio |= format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;

	int videoIdx = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);

	// e.g. the cover art of a mp3 file is no video
	if (videoIdx >= 0 && (format->streams[videoIdx]->disposition & AV_DISPOSITION_ATTACHED_PIC))
		videoIdx = -1;

	if (!hasAudio && videoIdx < 0) {
		LOGERROR("mediaplayer: %s: no audio or video stream in file '%s'", __FUNCTION__, url);
		avformat_close_input(&format);
		return false;
	}

	if (videoIdx < 0)
		LOGDEBUG2(L_MEDIA, "mediaplayer: %s: stream does not seem to contain video", __FUNCTION__);

	LOCK_THREAD;

	int audioIdx = AddTracks(format);
	m_requestedAudioIdx = NO_TRACK_REQUEST;
	m_resyncAfterPause = false;

	m_pFormat = format;
	m_audioIdx = audioIdx;
	m_videoIdx = videoIdx;
	m_startTime = format->start_time != AV_NOPTS_VALUE ? format->start_time : 0;
	m_durationMs = format->duration > 0 ? format->duration / (AV_TIME_BASE / 1000) : 0;
	m_seekable = format->pb && (format->pb->seekable & AVIO_SEEKABLE_NORMAL);

	m_fps = DEFAULTFRAMESPERSECOND;
	if (videoIdx >= 0) {
		AVRational rate = format->streams[videoIdx]->avg_frame_rate;
		if (!rate.num || !rate.den)
			rate = format->streams[videoIdx]->r_frame_rate;
		if (rate.num && rate.den && av_q2d(rate) > 1 && av_q2d(rate) < 200)
			m_fps = av_q2d(rate);
	}

	// a new file starts in normal play mode
	m_playMode = pmPlay;
	m_playDir = pdForward;
	m_trickSpeed = NORMAL_SPEED;
	m_seekMs = -1;
	m_stillPending = false;
	m_trickMs = 0;
	m_eof = m_bof = false;
	m_stuckPositionMs = -1;
	m_positionMs = 0;

	DeviceClear();
	m_staleStc = m_pDevice->GetSTC();
	m_pDevice->SetMediaStreams(audioIdx >= 0, videoIdx >= 0);

	if (audioIdx >= 0)
		m_pDevice->SetAudioCodec(format->streams[audioIdx]->codecpar->codec_id,
			format->streams[audioIdx]->codecpar, PES_TIMEBASE);

	if (videoIdx >= 0)
		m_pDevice->SetVideoCodec(videoCodec->id, format->streams[videoIdx]->codecpar, PES_TIMEBASE);

	std::lock_guard<std::mutex> lock(m_fileMutex);
	std::string path = url;
	m_fileNumber++;
	m_filePath = path;
	cPlaylistEntry *entry = m_pCurrentEntry;
	m_fileTitle = entry ? entry->OsdItemString() : path.substr(path.find_last_of('/') + 1);

	return true;
}

/**
 * Tell VDR the audio tracks of a file and select one
 *
 * The audio track is selected like VDR does with the tracks of a recording:
 * the preferred audio language of the setup and, with Setup.CurrentDolby,
 * the Dolby tracks are preferred. Otherwise the default track of the file is
 * played. The user can select another track with the Audio key.
 *
 * @param format    format context of the file
 * @return          stream index of the selected audio track (-1, if none)
 */
int cDrmDevicePlayer::AddTracks(AVFormatContext *format)
{
	DeviceClrAvailableTracks();

	int numAudio = 0;
	int numDolby = 0;
	std::vector<std::pair<eTrackType, int>> tracks;           // track type and stream index

	for (unsigned int i = 0; i < format->nb_streams && i < 0xFFFF; i++) {
		AVStream *stream = format->streams[i];
		if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
			continue;

		bool dolby = IsDolbyTrack(stream->codecpar->codec_id);
		eTrackType type = dolby ? ttDolby : ttAudio;
		int index = dolby ? numDolby : numAudio;
		if (type + index > (dolby ? ttDolbyLast : ttAudioLast))
			continue;

		(dolby ? numDolby : numAudio)++;
		DeviceSetAvailableTrack(type, index, i + 1, StreamLanguage(stream), AudioTrackDescription(stream).c_str());
		tracks.emplace_back(eTrackType(type + index), i);
	}

	if (tracks.empty())
		return -1;

	// the default track of the file, if no language is preferred
	int audioIdx = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	eTrackType selected = ttNone;
	for (auto &track : tracks) {
		if (track.second == audioIdx)
			selected = track.first;
	}
	if (selected == ttNone) {
		selected = tracks.front().first;
		audioIdx = tracks.front().second;
	}

	// like cDevice::EnsureAudioTrack()
	int languagePreference = -1;
	for (int pass = 0; pass < 2; pass++) {
		// with Setup.CurrentDolby the Dolby tracks are checked first, so they win at the same preference
		bool dolby = (pass == 0) == static_cast<bool>(Setup.CurrentDolby);
		for (auto &track : tracks) {
			if (IS_DOLBY_TRACK(track.first) != dolby)
				continue;
			if (I18nIsPreferredLanguage(Setup.AudioLanguages, StreamLanguage(format->streams[track.second]), languagePreference)) {
				selected = track.first;
				audioIdx = track.second;
			}
		}
	}

	DeviceSetCurrentAudioTrack(selected);
	LOGDEBUG2(L_MEDIA, "mediaplayer: %s: %zu audio tracks, play stream %d", __FUNCTION__, tracks.size(), audioIdx);

	return audioIdx;
}

/**
 * Select the audio track (called by VDR, e.g. from the track menu)
 *
 * The track is switched by the player thread.
 *
 * @param type      track type
 * @param trackId   track (the id is the stream index + 1)
 */
void cDrmDevicePlayer::SetAudioTrack([[maybe_unused]] eTrackType type, const tTrackId *trackId)
{
	if (trackId && trackId->id)
		m_requestedAudioIdx = trackId->id - 1;
}

/**
 * Switch to the audio track, which the user selected
 *
 * In normal play mode the file is read again from the current position, so
 * the new track starts at once (like cDvbPlayer). In pause mode this is done,
 * when the playback continues.
 *
 * Must be called with the thread lock.
 */
void cDrmDevicePlayer::SwitchAudioTrack(void)
{
	int audioIdx = m_requestedAudioIdx.exchange(NO_TRACK_REQUEST);

	if (audioIdx == NO_TRACK_REQUEST || audioIdx == m_audioIdx || audioIdx < 0 ||
	    audioIdx >= static_cast<int>(m_pFormat->nb_streams) ||
	    m_pFormat->streams[audioIdx]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
		return;

	LOGDEBUG2(L_MEDIA, "mediaplayer: %s: switch from stream %d to %d", __FUNCTION__, m_audioIdx, audioIdx);

	m_audioIdx = audioIdx;
	if (m_seekable && m_playMode == pmPlay)
		Empty();
	else if (m_seekable && m_playMode == pmPause)
		m_resyncAfterPause = true;
	else if (m_pPacket && m_pPacket->stream_index != m_videoIdx)
		av_packet_free(&m_pPacket);

	AVStream *stream = m_pFormat->streams[audioIdx];
	m_pDevice->SetAudioCodec(stream->codecpar->codec_id, stream->codecpar, PES_TIMEBASE);
}

/**
 * Close the current file
 */
void cDrmDevicePlayer::CloseFile(void)
{
	LOCK_THREAD;

	if (m_pPacket)
		av_packet_free(&m_pPacket);

	if (m_pFormat)
		avformat_close_input(&m_pFormat);

	m_audioIdx = m_videoIdx = -1;
	m_durationMs = 0;
	m_seekable = false;
}

/**
 * Play a file
 *
 * Feeds the device until the file ends or is stopped.
 *
 * @param url       file to play
 */
void cDrmDevicePlayer::PlayFile(const char *url)
{
	m_stopFile = false;

	if (!OpenFile(url))
		return;

	// resume the replay of local files (like cDvbPlayer)
	bool canResume = m_seekable && m_durationMs > 0 && url[0] == '/';
	cMediaResume resume(std::string(cPlugin::CacheDirectory(PLUGIN_NAME_I18N)) + "/resume");

	if (canResume) {
		int64_t positionMs = resume.Get(url);
		if (positionMs > 0 && positionMs < m_durationMs) {
			LOCK_THREAD;
			m_seekMs = m_positionMs = positionMs;
			LOGINFO("mediaplayer: resuming replay at %s", *IndexToHMSF(positionMs / 1000, false, 1));
		}
	}

	bool ended = false;
	while (Running() && !m_stopFile) {
		bool sleep;
		{
			LOCK_THREAD;
			sleep = !Feed();

			if (CheckBeginEnd()) {
				ended = true;
				break;
			}
		}

		if (sleep)
			cCondWait::SleepMs(m_playMode == pmPause || m_playMode == pmStill ? 10 : 3);
	}

	// a file, which was played to the end, starts at the beginning next time
	if (canResume) {
		if (ended)
			resume.Delete(url);
		else
			resume.Store(url, GetPositionMs() - RESUME_BACKUP_MS);
	}

	CloseFile();
}

/**
 * Convert a pts of a packet to the position in the file
 *
 * @param pts       pts in the PES timebase
 * @return          position in ms
 */
int64_t cDrmDevicePlayer::PtsToPositionMs(int64_t pts)
{
	return (pts - MEDIA_START_PTS) * 1000 / PTSTICKS;
}

/**
 * Get the current playback position
 *
 * After an Empty() the device still reports the position of the old frame,
 * until a frame of the new position is played. Meanwhile the new position
 * is returned.
 *
 * @return          position in ms
 */
int64_t cDrmDevicePlayer::GetPositionMs(void)
{
	int64_t stc = m_pDevice->GetSTC();

	if (stc != AV_NOPTS_VALUE && stc != m_staleStc) {
		m_staleStc = AV_NOPTS_VALUE;

		int64_t positionMs = std::max(PtsToPositionMs(stc), (int64_t)0);
		if (m_durationMs > 0)
			positionMs = std::min(positionMs, (int64_t)m_durationMs);

		m_positionMs = positionMs;
	}

	return m_positionMs;
}

/**
 * Seek in the current file
 *
 * @param positionMs  position in ms
 * @param forward     true: seek to the I-frame at or after the position,
 *                    false: seek to the I-frame at or before the position
 * @retval true       on success
 */
bool cDrmDevicePlayer::Seek(int64_t positionMs, bool forward)
{
	if (!m_seekable)
		return false;

	int64_t ts = m_startTime + av_rescale(positionMs, AV_TIME_BASE, 1000);
	int err = forward ? avformat_seek_file(m_pFormat, -1, ts, ts, INT64_MAX, 0)
	                  : avformat_seek_file(m_pFormat, -1, INT64_MIN, ts, ts, 0);

	if (err < 0) {
		LOGDEBUG2(L_MEDIA, "mediaplayer: %s: seek to %" PRId64 "ms failed: %s", __FUNCTION__, positionMs, av_err2str(err));
		return false;
	}

	m_eof = false;
	return true;
}

/**
 * Read the next packet of the audio or video stream
 *
 * The timestamps of the packet are converted to the PES timebase.
 *
 * @return          the packet, nullptr at the end of the file
 */
AVPacket *cDrmDevicePlayer::ReadPacket(void)
{
	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		LOGFATAL("mediaplayer: %s: out of memory", __FUNCTION__);
		return nullptr;
	}

	while (true) {
		int err = av_read_frame(m_pFormat, pkt);
		if (err < 0) {
			LOGDEBUG2(L_MEDIA, "mediaplayer: %s: av_read_frame: %s", __FUNCTION__, av_err2str(err));
			m_eof = true;
			av_packet_free(&pkt);
			return nullptr;
		}

		if (pkt->stream_index == m_audioIdx || pkt->stream_index == m_videoIdx) {
			RescaleToPesTimebase(pkt, m_pFormat->streams[pkt->stream_index]->time_base, m_startTime);
			return pkt;
		}

		av_packet_unref(pkt);
	}
}

/**
 * Read the next frame in fast forward/backward and slow backward mode
 *
 * Like cDvbPlayer, only I-frames are played in these modes. The next frame is
 * at least TRICK_STEP_MS away from the last one. An audio-only file is played
 * in steps of TRICK_STEP_MS.
 *
 * @param forward   true, if the next frame is searched forward
 * @return          the packet, nullptr at the begin/end of the file
 */
AVPacket *cDrmDevicePlayer::ReadTrickFrame(bool forward)
{
	int trickIdx = m_videoIdx >= 0 ? m_videoIdx : m_audioIdx;
	int64_t stepMs = TRICK_STEP_MS;

	while (true) {
		int64_t targetMs = forward ? m_trickMs + stepMs : std::max(m_trickMs - stepMs, (int64_t)0);

		// if the seek fails, the frame is searched from the current read position
		Seek(targetMs, forward);

		AVPacket *pkt;
		while ((pkt = ReadPacket())) {
			if (pkt->stream_index == trickIdx && pkt->pts != AV_NOPTS_VALUE &&
			    (trickIdx == m_audioIdx || (pkt->flags & AV_PKT_FLAG_KEY))) {
				int64_t ms = PtsToPositionMs(pkt->pts);

				if (forward ? ms > m_trickMs : ms < m_trickMs)
					return pkt;

				// the seek went back to the last frame again, go back further
				if (!forward) {
					av_packet_free(&pkt);
					break;
				}
			}
			av_packet_free(&pkt);
		}

		if (forward)
			return nullptr;              // end of file

		m_eof = false;
		if (targetMs == 0) {
			m_bof = true;
			return nullptr;
		}

		stepMs *= 2;
	}
}

/**
 * Send a packet to the device
 *
 * @param pkt       packet to send
 * @retval true     the device took the packet
 * @retval false    the device is full, the packet must be sent again
 */
bool cDrmDevicePlayer::FeedPacket(AVPacket *pkt)
{
	if (pkt->stream_index == m_videoIdx)
		return m_pDevice->PlayVideoPkts(pkt);           // the device takes the ownership

	if (!m_pDevice->PlayAudioPkts(pkt))
		return false;

	av_packet_free(&pkt);
	return true;
}

/**
 * Display the I-frame at the position m_seekMs as a still picture
 */
void cDrmDevicePlayer::ShowStillPicture(void)
{
	if (m_videoIdx < 0)
		return;

	AVPacket *pkt;
	while ((pkt = ReadPacket())) {
		if (pkt->stream_index == m_videoIdx && (pkt->flags & AV_PKT_FLAG_KEY)) {
			if (pkt->pts != AV_NOPTS_VALUE)
				m_trickMs = m_positionMs = PtsToPositionMs(pkt->pts);
			m_pDevice->StillPicturePkt(pkt);  // the device takes the ownership
			return;
		}
		av_packet_free(&pkt);
	}
}

/**
 * Feed the device according to the play mode
 *
 * Must be called with the thread lock.
 *
 * @retval true     something was done, call again
 * @retval false    nothing to do or the device is full, call again later
 */
bool cDrmDevicePlayer::Feed(void)
{
	SwitchAudioTrack();

	// reposition after an Empty()
	if (m_seekMs >= 0) {
		Seek(m_seekMs, false);
		m_trickMs = m_seekMs;
		m_seekMs = -1;
		m_eof = m_bof = false;
		m_stuckPositionMs = -1;

		if (m_stillPending) {
			m_stillPending = false;
			ShowStillPicture();
			return true;
		}
	}

	if (m_playMode == pmPause || m_playMode == pmStill || m_eof || m_bof)
		return false;

	bool trickFrames = m_playMode == pmFast || (m_playMode == pmSlow && m_playDir == pdBackward);

	if (!m_pPacket) {
		if (trickFrames) {
			m_pPacket = ReadTrickFrame(m_playDir == pdForward);
			if (m_pPacket)
				m_trickMs = PtsToPositionMs(m_pPacket->pts);
		} else {
			m_pPacket = ReadPacket();

			// only video in trick mode, slow forward plays audio like cDvbPlayer
			while (m_pPacket && m_playMode != pmPlay && !(m_playMode == pmSlow && m_playDir == pdForward) &&
			       m_pPacket->stream_index != m_videoIdx) {
				av_packet_free(&m_pPacket);
				m_pPacket = ReadPacket();
			}
		}

		if (!m_pPacket)
			return false;
	}

	if (!FeedPacket(m_pPacket))
		return false;

	m_pPacket = nullptr;
	return true;
}

/**
 * Check, if the device played all data
 *
 * With newer VDR versions this also drains the decoder, so the last
 * frames are displayed.
 *
 * @retval true     the buffers of the device are empty
 */
bool cDrmDevicePlayer::DeviceBuffersEmpty(void)
{
#if APIVERSNUM >= 30014
	return DeviceDrain();
#else
	return DeviceFlush(0);
#endif
}

/**
 * Handle hitting the begin/end of the file (like cDvbPlayer)
 *
 * At the end of the file, the file ends after the device played all frames.
 * At the begin of the file in backward mode, the normal playback starts.
 *
 * Must be called with the thread lock.
 *
 * @retval true     the file ended
 */
bool cDrmDevicePlayer::CheckBeginEnd(void)
{
	if (!m_eof && !m_bof)
		return false;

	// the device played all frames, if its buffers are empty or the position doesn't change any more
	bool done = false;
	int64_t positionMs = GetPositionMs();
	if (m_playMode == pmPause || m_playMode == pmStill || positionMs != m_stuckPositionMs) {
		m_stuckPositionMs = positionMs;
		m_stuckTimer.Set(MAX_STUCK_AT_EOF_MS);
	} else if (m_stuckTimer.TimedOut())
		done = true;

	if (m_playMode != pmPause && m_playMode != pmStill && !m_pPacket && DeviceBuffersEmpty())
		done = true;

	if (!done)
		return false;

	if (m_eof && m_playDir == pdForward) {
		LOGDEBUG2(L_MEDIA, "mediaplayer: %s: end of file", __FUNCTION__);
		return true;
	}

	// begin of file reached in backward mode, switch to play
	Empty();
	DevicePlay();
	m_playMode = pmPlay;
	m_playDir = pdForward;

	return false;
}

/*****************************************************************************
 * cDrmDevicePlayer - positioning (like cDvbPlayer)
 ****************************************************************************/

/**
 * Clear the device and all buffered data
 *
 * The next packet is read from the current playback position.
 */
void cDrmDevicePlayer::Empty(void)
{
	LOCK_THREAD;

	m_seekMs = GetPositionMs();
	m_stillPending = false;

	if (m_pPacket)
		av_packet_free(&m_pPacket);

	DeviceClear();
	m_staleStc = m_pDevice->GetSTC();
}

/**
 * Change the trick speed
 *
 * @param increment   step in the SPEEDS table
 */
void cDrmDevicePlayer::TrickSpeed(int increment)
{
	int nts = m_trickSpeed + increment;

	if (SPEEDS[nts] == 1) {
		m_trickSpeed = nts;
		if (m_playMode == pmFast)
			Play();
		else
			Pause();
	} else if (SPEEDS[nts]) {
		m_trickSpeed = nts;
		int mult = (m_playMode == pmSlow && m_playDir == pdForward) ? 1 : SPEED_MULT;
		int sp = (SPEEDS[nts] > 0) ? mult / SPEEDS[nts] : -SPEEDS[nts] * mult;
		if (sp > MAX_VIDEO_SLOWMOTION)
			sp = MAX_VIDEO_SLOWMOTION;
		DeviceTrickSpeed(sp, m_playDir == pdForward);
	}
}

/**
 * Pause the playback, or continue it, if it's paused
 */
void cDrmDevicePlayer::Pause(void)
{
	if (m_playMode == pmPause || m_playMode == pmStill)
		Play();
	else {
		LOCK_THREAD;
		if (m_playMode == pmFast || (m_playMode == pmSlow && m_playDir == pdBackward))
			Empty();

		DeviceFreeze();
		m_playMode = pmPause;
	}
}

/**
 * Start the normal playback
 */
void cDrmDevicePlayer::Play(void)
{
	if (m_playMode != pmPlay) {
		LOCK_THREAD;
		if (m_playMode == pmStill || m_playMode == pmFast || (m_playMode == pmSlow && m_playDir == pdBackward) ||
		    (m_playMode == pmPause && m_resyncAfterPause))
			Empty();
		m_resyncAfterPause = false;

		DevicePlay();
		m_playMode = pmPlay;
		m_playDir = pdForward;
	}
}

/**
 * Fast forward, or slow forward if paused
 *
 * With Setup.MultiSpeedMode, repeated calls change the speed.
 */
void cDrmDevicePlayer::Forward(void)
{
	if (!m_seekable)
		return;

	LOCK_THREAD;
	switch (m_playMode) {
		case pmFast:
			if (Setup.MultiSpeedMode) {
				TrickSpeed(m_playDir == pdForward ? 1 : -1);
				break;
			} else if (m_playDir == pdForward) {
				Play();
				break;
			}
			// run into pmPlay
			[[fallthrough]];
		case pmPlay:
			Empty();
			if (DeviceIsPlayingVideo())
				DeviceMute();
			m_playMode = pmFast;
			m_playDir = pdForward;
			m_trickSpeed = NORMAL_SPEED;
			TrickSpeed(Setup.MultiSpeedMode ? 1 : MAX_SPEEDS);
			break;
		case pmSlow:
			if (Setup.MultiSpeedMode) {
				TrickSpeed(m_playDir == pdForward ? -1 : 1);
				break;
			} else if (m_playDir == pdForward) {
				Pause();
				break;
			}
			Empty();
			// run into pmPause
			[[fallthrough]];
		case pmStill:
		case pmPause:
			DeviceMute();
			m_playMode = pmSlow;
			m_playDir = pdForward;
			m_trickSpeed = NORMAL_SPEED;
			TrickSpeed(Setup.MultiSpeedMode ? -1 : -MAX_SPEEDS);
			break;
	}
}

/**
 * Fast backward, or slow backward if paused
 *
 * With Setup.MultiSpeedMode, repeated calls change the speed.
 */
void cDrmDevicePlayer::Backward(void)
{
	if (!m_seekable)
		return;

	LOCK_THREAD;
	switch (m_playMode) {
		case pmFast:
			if (Setup.MultiSpeedMode) {
				TrickSpeed(m_playDir == pdBackward ? 1 : -1);
				break;
			} else if (m_playDir == pdBackward) {
				Play();
				break;
			}
			// run into pmPlay
			[[fallthrough]];
		case pmPlay:
			Empty();
			if (DeviceIsPlayingVideo())
				DeviceMute();
			m_playMode = pmFast;
			m_playDir = pdBackward;
			m_trickSpeed = NORMAL_SPEED;
			TrickSpeed(Setup.MultiSpeedMode ? 1 : MAX_SPEEDS);
			break;
		case pmSlow:
			if (Setup.MultiSpeedMode) {
				TrickSpeed(m_playDir == pdBackward ? -1 : 1);
				break;
			} else if (m_playDir == pdBackward) {
				Pause();
				break;
			}
			Empty();
			// run into pmPause
			[[fallthrough]];
		case pmStill:
		case pmPause:
			Empty();
			DeviceMute();
			m_playMode = pmSlow;
			m_playDir = pdBackward;
			m_trickSpeed = NORMAL_SPEED;
			TrickSpeed(Setup.MultiSpeedMode ? -1 : -MAX_SPEEDS);
			break;
	}
}

/**
 * Skip the given number of seconds and continue with the normal playback
 *
 * @param seconds     seconds to skip (negative: backward)
 */
void cDrmDevicePlayer::SkipSeconds(int seconds)
{
	if (!m_seekable || !seconds)
		return;

	LOCK_THREAD;
	int64_t positionMs = GetPositionMs();
	Empty();

	positionMs = std::max<int64_t>(positionMs + seconds * 1000LL, 0);
	if (m_durationMs > 0)
		positionMs = std::min(positionMs, (int64_t)m_durationMs);

	m_seekMs = m_positionMs = positionMs;
	Play();
}

/**
 * Go to the given position
 *
 * @param seconds     position in seconds
 * @param still       true: show the I-frame at the position as a still picture
 *                    false: start the normal playback at the position
 */
void cDrmDevicePlayer::Goto(int seconds, bool still)
{
	if (!m_seekable)
		return;

	LOCK_THREAD;
	Empty();

	int64_t positionMs = std::max<int64_t>(seconds * 1000LL, 0);
	if (m_durationMs > 0)
		positionMs = std::min(positionMs, (int64_t)m_durationMs);

	m_seekMs = m_positionMs = positionMs;

	if (still) {
		if (m_playMode == pmPause)
			DevicePlay();
		m_stillPending = true;
		m_playMode = pmStill;
	} else
		Play();
}

/**
 * Get the current and total frame index
 *
 * The frame indexes are calculated from the position and the frame rate.
 *
 * @param[out] current    current frame index
 * @param[out] total      total number of frames (0, if unknown)
 * @param snapToIFrame    unused
 * @retval true           if a file is played
 */
bool cDrmDevicePlayer::GetIndex(int &current, int &total, [[maybe_unused]] bool snapToIFrame)
{
	current = (int)(GetPositionMs() * m_fps / 1000);
	total = (int)(m_durationMs * m_fps / 1000);

	return true;
}

/**
 * Get the current and total frame number
 *
 * @param[out] current    current frame number
 * @param[out] total      total number of frames (0, if unknown)
 * @retval true           if a file is played
 */
bool cDrmDevicePlayer::GetFrameNumber(int &current, int &total)
{
	return GetIndex(current, total);
}

/**
 * Get the current replay mode (like cDvbPlayer)
 *
 * @param[out] play       true, if playing (also in fast mode), false if paused (also in slow mode)
 * @param[out] forward    true, if playing forward
 * @param[out] speed      -1 in normal play/pause mode, 0 in single speed mode,
 *                        >0 in multi speed mode
 * @retval true           always
 */
bool cDrmDevicePlayer::GetReplayMode(bool &play, bool &forward, int &speed)
{
	play = (m_playMode == pmPlay || m_playMode == pmFast);
	forward = (m_playDir == pdForward);
	if (m_playMode == pmFast || m_playMode == pmSlow)
		speed = Setup.MultiSpeedMode ? abs(m_trickSpeed - NORMAL_SPEED) : 0;
	else
		speed = -1;

	return true;
}

/*****************************************************************************
 * cDrmDeviceControl (cControl mediaplayer)
 ****************************************************************************/

/**
 * Time in seconds, after which a replay mode display is hidden (like cReplayControl)
 *
 * @ingroup mediaplayer
 */
static constexpr int MODE_TIMEOUT = 3;

/**
 * Time in ms between the updates of the replay progress display (like cReplayControl)
 *
 * @ingroup mediaplayer
 */
static constexpr int PROGRESS_TIMEOUT = 100;

/**
 * Seconds to stay off the end of a file with the time search (like cReplayControl)
 *
 * @ingroup mediaplayer
 */
static constexpr int STAY_SECONDS_OFF_END = 10;

/**
 * Create a new control interface and corresponding player
 *
 * @param url             file or playlist to be played
 * @param device          pointer to device
 */
cDrmDeviceControl::cDrmDeviceControl(const char *url, cOutputDevice *device)
	: cControl(m_pPlayer = new cDrmDevicePlayer(url, device))
{
	m_pControl = this;
}

cDrmDeviceControl::~cDrmDeviceControl()
{
	cDrmDeviceControl::Hide();
	StopReplaying();

	delete m_pPlayer;
	m_pPlayer = NULL;
	m_pControl = NULL;
}

/**
 * Report the end of the replay of the current file
 */
void cDrmDeviceControl::StopReplaying(void)
{
	if (m_fileNumber)
		cStatus::MsgReplaying(this, NULL, m_path.c_str(), false);

	m_fileNumber = 0;
}

/**
 * Check, if the player started a new file
 *
 * Reports the replay of the new file and shows the replay display
 * like cReplayControl does at the replay start.
 */
void cDrmDeviceControl::CheckNewFile(void)
{
	std::string title, path;
	int fileNumber = m_pPlayer->GetFile(title, path);

	if (fileNumber == m_fileNumber)
		return;

	StopReplaying();

	m_fileNumber = fileNumber;
	m_title = title;
	m_path = path;
	cStatus::MsgReplaying(this, m_title.c_str(), m_path.c_str(), true);

	if (m_visible && !m_modeOnly)
		m_shown = ShowProgress(true);
	else if (Setup.ProgressDisplayTime)
		ShowTimed(Setup.ProgressDisplayTime);
}

/**
 * Show the replay display for the given time
 *
 * @param seconds     time in seconds, 0 shows the display until it is hidden
 */
void cDrmDeviceControl::ShowTimed(int seconds)
{
	if (m_modeOnly)
		Hide();

	if (!m_visible) {
		m_shown = ShowProgress(true);
		m_timeoutShow = (m_shown && seconds > 0) ? time(NULL) + seconds : 0;
	} else if (m_timeoutShow && seconds > 0)
		m_timeoutShow = time(NULL) + seconds;
}

/**
 * Show the replay display
 */
void cDrmDeviceControl::Show(void)
{
	ShowTimed();
}

/**
 * Hide the replay display
 */
void cDrmDeviceControl::Hide(void)
{
	if (m_visible) {
		LOGDEBUG2(L_MEDIA, "mediaplayer: %s:", __FUNCTION__);
		delete m_pDisplayReplay;
		m_pDisplayReplay = nullptr;
		SetNeedsFastResponse(false);
		m_visible = false;
		m_modeOnly = false;
		m_lastPlay = m_lastForward = false;
		m_lastSpeed = -2;               // an invalid value
		m_timeSearchActive = false;
		m_timeoutShow = 0;
	}
}

/**
 * Show the replay mode
 *
 * If the replay display isn't shown, only the mode is shown, if
 * Setup.ShowReplayMode is set.
 */
void cDrmDeviceControl::ShowMode(void)
{
	if (m_visible || (Setup.ShowReplayMode && !cOsd::IsOpen())) {
		bool play, forward;
		int speed;
		if (GetReplayMode(play, forward, speed) && (!m_visible || play != m_lastPlay || forward != m_lastForward || speed != m_lastSpeed)) {
			bool normalPlay = (play && speed == -1);

			if (!m_visible) {
				if (normalPlay)
					return;         // no need to do indicate ">" unless there was a different mode displayed before

				m_visible = m_modeOnly = true;
				m_pDisplayReplay = Skins.Current()->DisplayReplay(m_modeOnly);
			}

			if (m_modeOnly && !m_timeoutShow && normalPlay)
				m_timeoutShow = time(NULL) + MODE_TIMEOUT;

			m_pDisplayReplay->SetMode(play, forward, speed);
			m_lastPlay = play;
			m_lastForward = forward;
			m_lastSpeed = speed;
		}
	}
}

/**
 * Show or update the replay progress
 *
 * @param initial     true, if the display is shown the first time
 * @retval true       the progress is shown
 */
bool cDrmDeviceControl::ShowProgress(bool initial)
{
	int current, total;

	if (!(initial || m_updateTimer.TimedOut()))
		return m_visible;

	if (!GetIndex(current, total))
		return false;

	// e.g. a live stream has no duration
	if (total <= 0)
		total = std::max(current, 1);

	if (!m_visible) {
		m_pDisplayReplay = Skins.Current()->DisplayReplay(m_modeOnly);
		SetNeedsFastResponse(true);
		m_visible = true;
	}

	if (initial) {
		m_pDisplayReplay->SetTitle(m_title.c_str());
		m_lastCurrent = m_lastTotal = -1;
	}

	if (current != m_lastCurrent || total != m_lastTotal) {
		if (Setup.ShowRemainingTime || total != m_lastTotal) {
			int index = total;
			if (Setup.ShowRemainingTime)
				index = current - index;
			m_pDisplayReplay->SetTotal(IndexToHMSF(index, false, FramesPerSecond()));
		}
		m_pDisplayReplay->SetProgress(current, total);
		m_pDisplayReplay->SetCurrent(IndexToHMSF(current, false, FramesPerSecond()));
		m_pDisplayReplay->Flush();
		m_lastCurrent = current;
	}

	m_lastTotal = total;
	ShowMode();
	m_updateTimer.Set(PROGRESS_TIMEOUT);

	return true;
}

/**
 * Show the entered time of the time search
 */
void cDrmDeviceControl::TimeSearchDisplay(void)
{
	char buf[64];
	// TRANSLATORS: note the trailing blank!
	strcpy(buf, tr("Jump: "));
	int len = strlen(buf);
	char h10 = '0' + (m_timeSearchTime >> 24);
	char h1  = '0' + ((m_timeSearchTime & 0x00FF0000) >> 16);
	char m10 = '0' + ((m_timeSearchTime & 0x0000FF00) >> 8);
	char m1  = '0' + (m_timeSearchTime & 0x000000FF);
	char ch10 = m_timeSearchPos > 3 ? h10 : '-';
	char ch1  = m_timeSearchPos > 2 ? h1  : '-';
	char cm10 = m_timeSearchPos > 1 ? m10 : '-';
	char cm1  = m_timeSearchPos > 0 ? m1  : '-';
	snprintf(buf + len, sizeof(buf) - len, "%c%c:%c%c", ch10, ch1, cm10, cm1);
	m_pDisplayReplay->SetJump(buf);
}

/**
 * Handle a key of the time search (like cReplayControl)
 *
 * The digits enter a time (hh:mm). Left/Right skip the time backward/forward,
 * Play/Up go to the time and Pause/Down/Ok show the frame at the time.
 *
 * @param key     pressed key
 */
void cDrmDeviceControl::TimeSearchProcess(eKeys key)
{
	int seconds = (m_timeSearchTime >> 24) * 36000 + ((m_timeSearchTime & 0x00FF0000) >> 16) * 3600 +
	              ((m_timeSearchTime & 0x0000FF00) >> 8) * 600 + (m_timeSearchTime & 0x000000FF) * 60;
	int current = int(round(m_lastCurrent / FramesPerSecond()));
	int total = int(round(m_lastTotal / FramesPerSecond()));

	switch (key) {
		case k0 ... k9:
			if (m_timeSearchPos < 4) {
				m_timeSearchTime <<= 8;
				m_timeSearchTime |= key - k0;
				m_timeSearchPos++;
				TimeSearchDisplay();
			}
			break;
		case kFastRew:
		case kLeft:
		case kFastFwd:
		case kRight: {
			int dir = ((key == kRight || key == kFastFwd) ? 1 : -1);
			if (dir > 0)
				seconds = std::min(total - current - STAY_SECONDS_OFF_END, seconds);
			m_pPlayer->SkipSeconds(seconds * dir);
			m_timeSearchActive = false;
			}
			break;
		case kPlayPause:
		case kPlay:
		case kUp:
		case kPause:
		case kDown:
		case kOk:
			if (m_timeSearchPos > 0) {
				seconds = std::min(total - STAY_SECONDS_OFF_END, seconds);
				bool still = key == kDown || key == kPause || key == kOk;
				m_pPlayer->Goto(seconds, still);
			}
			m_timeSearchActive = false;
			break;
		default:
			if (!(key & k_Flags))   // ignore repeat/release keys
				m_timeSearchActive = false;
			break;
	}

	if (!m_timeSearchActive) {
		if (m_timeSearchHide)
			Hide();
		else
			m_pDisplayReplay->SetJump(NULL);
		ShowMode();
	}
}

/**
 * Start the time search
 */
void cDrmDeviceControl::TimeSearch(void)
{
	if (!m_pPlayer->CanSeek())
		return;

	m_timeSearchTime = m_timeSearchPos = 0;
	m_timeSearchHide = false;

	if (m_modeOnly)
		Hide();

	if (!m_visible) {
		Show();
		if (m_visible)
			m_timeSearchHide = true;
		else
			return;
	}

	m_timeoutShow = 0;
	TimeSearchDisplay();
	m_timeSearchActive = true;
}

/**
 * Handle a key event
 *
 * The keys work like in VDR's cReplayControl.
 *
 * @param key     pressed key
 */
eOSState cDrmDeviceControl::ProcessKey(eKeys key)
{
	if (m_closing || !m_pPlayer->IsActive()) {
		Hide();
		return osEnd;
	}

	CheckNewFile();

	if (m_visible) {
		if (m_timeoutShow && time(NULL) > m_timeoutShow) {
			Hide();
			ShowMode();
			m_timeoutShow = 0;
		} else if (m_modeOnly)
			ShowMode();
		else
			m_shown = ShowProgress(!m_shown) || m_shown;
	}

	if (m_timeSearchActive && key != kNone) {
		TimeSearchProcess(key);
		return osContinue;
	}

	if (key == kPlayPause) {
		bool play, forward;
		int speed;
		GetReplayMode(play, forward, speed);
		if (speed >= 0)
			key = play ? kPlay : kPause;
		else
			key = play ? kPause : kPlay;
	}

	bool doShowMode = true;
	switch (int(key)) {
		// positioning
		case kPlay:
		case kUp:
			m_pPlayer->Play();
			break;
		case kPause:
		case kDown:
			m_pPlayer->Pause();
			break;
		case kFastRew | k_Release:
		case kLeft | k_Release:
			if (Setup.MultiSpeedMode)
				break;
			[[fallthrough]];
		case kFastRew:
		case kLeft:
			m_pPlayer->Backward();
			break;
		case kFastFwd | k_Release:
		case kRight | k_Release:
			if (Setup.MultiSpeedMode)
				break;
			[[fallthrough]];
		case kFastFwd:
		case kRight:
			m_pPlayer->Forward();
			break;
		case kRed:
			TimeSearch();
			break;
		case kGreen | k_Repeat:
			m_pPlayer->SkipSeconds(-Setup.SkipSecondsRepeat);
			break;
		case kGreen:
			m_pPlayer->SkipSeconds(-Setup.SkipSeconds);
			break;
		case kYellow | k_Repeat:
			m_pPlayer->SkipSeconds(Setup.SkipSecondsRepeat);
			break;
		case kYellow:
			m_pPlayer->SkipSeconds(Setup.SkipSeconds);
			break;
		case kStop:
		case kBlue:
			Hide();
			return osEnd;
		default:
			doShowMode = false;
			switch (int(key)) {
				// playlist
				case kPrev:
					m_pPlayer->PrevEntry();
					break;
				case kNext:
					m_pPlayer->NextEntry();
					break;
				// menu control
				case kOk:
					if (m_visible && !m_modeOnly) {
						Hide();
						doShowMode = true;
					} else
						Show();
					break;
				case kBack:
					Hide();
					cDeviceMenu::OpenMediaMenu(m_pPlayer->GetSource());
					return osEnd;
				default:
					return osUnknown;
			}
	}

	if (doShowMode)
		ShowMode();

	return osContinue;
}
