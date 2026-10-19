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
#include <cinttypes>
#include <cstdlib>
#include <fstream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <vdr/config.h>
#include <vdr/player.h>
#include <vdr/remux.h>

#include "audio.h"
#include "device.h"
#include "logger.h"
#include "mediaplayer.h"
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
			PlayFile(m_pCurrentEntry->GetPath().c_str());

			if (!m_noModify) {
				m_pCurrentEntry = m_pCurrentEntry->GetNextEntry();

				if (m_random) {
					srand (time (NULL));
					SetEntry(std::rand() % (m_entries));
				}
			}
			m_noModify = false;

			if (cDeviceMenu::Menu())
				cDeviceMenu::Menu()->PlayListMenu();
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
 * Without a playlist, the playback ends.
 */
void cDrmDevicePlayer::NextEntry(void)
{
	m_stopFile = true;
}

/**
 * Stop the current file and continue with the previous entry of the playlist
 *
 * The first entry is played again. Without a playlist, the playback ends.
 */
void cDrmDevicePlayer::PrevEntry(void)
{
	if (!m_pCurrentEntry) {
		m_stopFile = true;
		return;
	}

	int index = 0;
	for (cPlaylistEntry *entry = m_pFirstEntry; entry && entry != m_pCurrentEntry; entry = entry->GetNextEntry())
		index++;

	SetEntry(index > 0 ? index - 1 : 0);
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

	int audioIdx = -1;
	for (unsigned int i = 0; i < format->nb_streams; i++) {
		if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioIdx = i;
			break;
		}
	}

	int videoIdx = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);

	// e.g. the cover art of a mp3 file is no video
	if (videoIdx >= 0 && (format->streams[videoIdx]->disposition & AV_DISPOSITION_ATTACHED_PIC))
		videoIdx = -1;

	if (audioIdx < 0 && videoIdx < 0) {
		LOGERROR("mediaplayer: %s: no audio or video stream in file '%s'", __FUNCTION__, url);
		avformat_close_input(&format);
		return false;
	}

	if (videoIdx < 0)
		LOGDEBUG2(L_MEDIA, "mediaplayer: %s: stream does not seem to contain video", __FUNCTION__);

	LOCK_THREAD;

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

	return true;
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

	while (Running() && !m_stopFile) {
		bool sleep;
		{
			LOCK_THREAD;
			sleep = !Feed();

			if (CheckBeginEnd())
				break;
		}

		if (sleep)
			cCondWait::SleepMs(m_playMode == pmPause || m_playMode == pmStill ? 10 : 3);
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
		if (m_playMode == pmStill || m_playMode == pmFast || (m_playMode == pmSlow && m_playDir == pdBackward))
			Empty();

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
	delete m_pPlayer;
	m_pPlayer = NULL;
	m_pControl = NULL;
}

/**
 * Close the replay OSD
 */
void cDrmDeviceControl::Hide(void)
{
	LOGDEBUG2(L_MEDIA, "mediaplayer: %s:", __FUNCTION__);
	if (m_pOsd)
		delete m_pOsd;

	m_pOsd = NULL;
}

/**
 * Open the replay OSD
 */
void cDrmDeviceControl::ShowProgress(void)
{
	if (!m_pOsd) {
		LOGDEBUG2(L_MEDIA, "mediaplayer: %s: get OSD", __FUNCTION__);
		m_pOsd = Skins.Current()->DisplayReplay(false);
	}

	m_pOsd->SetTitle(m_pPlayer->GetCurrentPlaylistEntry() ? m_pPlayer->GetCurrentPlaylistEntry()->GetPath().c_str() : m_pPlayer->GetSource());
	int current, total;
	m_pPlayer->GetIndex(current, total);
	m_pOsd->SetProgress(current, total);
	m_pOsd->SetCurrent(IndexToHMSF(current, false, m_pPlayer->FramesPerSecond()));
	m_pOsd->SetTotal(IndexToHMSF(total, false, m_pPlayer->FramesPerSecond()));

	Skins.Flush();
}

/**
 * Handle a key event
 *
 * @param key     pressed key
 */
eOSState cDrmDeviceControl::ProcessKey(eKeys key)
{
	switch (key) {
		case kNone:
			if (m_pOsd)
				ShowProgress();
			if (m_closing) {
				Hide();
				return osStopReplay;
			}
			break;
		case kOk:
			if (m_pOsd)
				Hide();
			else
				ShowProgress();
			break;
		case kPlay:
		case kUp:
			m_pPlayer->Play();
			break;
		case kGreen:
			m_pPlayer->SkipSeconds(-60);
			break;
		case kYellow:
			m_pPlayer->SkipSeconds(60);
			break;
		case kLeft:
			m_pPlayer->SkipSeconds(-5);
			break;
		case kRight:
			m_pPlayer->SkipSeconds(5);
			break;
		case kBlue:
		case kBack:
			Hide();
			return osStopReplay;
		case kPause:
		case kDown:
			m_pPlayer->Pause();
			break;
		case kNext:
			m_pPlayer->NextEntry();
			break;
		default:
			break;
	}

	return osContinue;
}
