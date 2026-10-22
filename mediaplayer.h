// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mediaplayer.h
 * Mediaplayer Header File
 *
 * @copyright 2020 zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_MEDIAPLAYER_H
#define __DRMDEVICE_MEDIAPLAYER_H

#include <atomic>
#include <ctime>
#include <mutex>
#include <random>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <vdr/player.h>
#include <vdr/tools.h>

#include "subtitle.h"

class cOutputDevice;

/**
 * Mediaplayer Related Stuff
 * @defgroup mediaplayer Mediaplayer
 */

/**
 * Playlist Entry
 *
 * One file of a m3u playlist, the entries form a single linked list.
 *
 * @ingroup mediaplayer
 */
class cPlaylistEntry {
public:
	cPlaylistEntry(const std::string &);

	std::string OsdItemString(void);
	cPlaylistEntry* GetNextEntry(void) { return m_pNextEntry; }        ///< get the next entry (nullptr at the end)
	void SetNextEntry(cPlaylistEntry *entry) { m_pNextEntry = entry; } ///< set the next entry
	std::string GetPath(void) { return m_path; }                       ///< get the full path of the file
private:
	std::string m_path;                                 ///< full path of the file
	std::string m_file;                                 ///< file name
	std::string m_subFolder;                            ///< parent folder of the file
	std::string m_folder;                               ///< grandparent folder of the file
	/** next entry */
	cPlaylistEntry *m_pNextEntry = nullptr;
};

/**
 * Media Player
 *
 * Player for the mediaplayer mode: reads a file or all files of a m3u
 * playlist with libavformat and sends the packets to the device.
 *
 * The play modes, the trick speeds and the positioning work like in VDR's
 * cDvbPlayer, so the replay behaves like the replay of a recording.
 *
 * @ingroup mediaplayer
 */
class cDrmDevicePlayer : public cPlayer, cThread {
public:
	cDrmDevicePlayer(const char *, cOutputDevice *);
	virtual ~cDrmDevicePlayer();

	// positioning (like cDvbPlayer)
	void Play(void);
	void Pause(void);
	void Forward(void);
	void Backward(void);
	void SkipSeconds(int);
	void Goto(int, bool = false);
	bool CanSeek(void) { return m_seekable; }                                 ///< true, if the current file is seekable
	bool IsActive(void) { return cThread::Active(); }                         ///< true, if the player thread is running

	// cPlayer
	virtual double FramesPerSecond(void) { return m_fps; }                    ///< get the frame rate of the current file
	virtual bool GetIndex(int &, int &, bool = false);
	virtual bool GetFrameNumber(int &, int &);
	virtual bool GetReplayMode(bool &, bool &, int &);
	virtual void SetAudioTrack(eTrackType, const tTrackId *);
	virtual void SetSubtitleTrack(eTrackType, const tTrackId *);

	// playlist
	void SetEntry(int);
	void NextEntry(void);
	void PrevEntry(void);
	const char *GetSource(void) { return m_pSource; }                         ///< get the played file or playlist
	void ToggleRandomPlay(void) { m_random = !m_random; }                     ///< toggle random play of the playlist
	bool IsRandomPlayActive(void) { return m_random; }                        ///< true, if random play is active
	cPlaylistEntry *GetFirstPlaylistEntry(void) { return m_pFirstEntry; }     ///< get the first playlist entry
	cPlaylistEntry *GetCurrentPlaylistEntry(void) { return m_pCurrentEntry; } ///< get the currently played playlist entry
	int GetFile(std::string &, std::string &);

protected:
	virtual void Activate(bool On);
	virtual void Action(void);

private:
	/** play modes (like cDvbPlayer) */
	enum ePlayModes { pmPlay, pmPause, pmSlow, pmFast, pmStill };
	/** play directions (like cDvbPlayer) */
	enum ePlayDirs { pdForward, pdBackward };

	static constexpr int NORMAL_SPEED = 4;              ///< index of the '1' entry in SPEEDS
	static constexpr int MAX_SPEEDS = 3;                ///< offset of the maximum speed from normal speed in either direction
	static constexpr int SPEED_MULT = 12;               ///< speed multiplier
	static constexpr int MAX_VIDEO_SLOWMOTION = 63;     ///< max. trick speed value sent to the device
	static const int SPEEDS[];                          ///< trick speeds (like cDvbPlayer)

	void PlayFile(const char *);
	bool OpenFile(const char *);
	int AddTracks(AVFormatContext *);
	void UpdateStreamDiscards(void);
	void SwitchAudioTrack(void);
	void SwitchSubtitleTrack(void);
	void CloseSubtitleDecoder(void);
	void DecodeSubtitle(const AVPacket *);
	void ProcessSubtitles(void);
	void CloseFile(void);
	void ReadPlaylist(const char *);

	void TrickSpeed(int);
	void Empty(void);
	bool Feed(void);
	bool FeedPacket(AVPacket *);
	void ShowStillPicture(void);
	bool Seek(int64_t, bool);
	AVPacket *ReadPacket(void);
	AVPacket *ReadTrickFrame(bool);
	bool CheckBeginEnd(void);
	bool DeviceBuffersEmpty(void);
	int64_t GetPositionMs(void);
	int64_t PtsToPositionMs(int64_t);

	cPlaylistEntry *m_pFirstEntry = nullptr;            ///< first playlist entry
	std::atomic<cPlaylistEntry *> m_pCurrentEntry = nullptr; ///< currently played playlist entry (read by the menu)

	char *m_pSource;                                    ///< played file or playlist
	bool m_isPlaylist = false;                          ///< true, if the source is a m3u playlist (not a HLS stream)
	int m_entries = 0;                                  ///< number of playlist entries
	cOutputDevice *m_pDevice;                           ///< pointer to the device
	std::atomic<bool> m_stopFile = false;               ///< true, if the current file shall be stopped
	std::atomic<bool> m_random = false;                 ///< true, if random play is active
	std::mt19937 m_randomGenerator{std::random_device{}()}; ///< random generator for the random play
	bool m_noModify = false;                            ///< true, if the next entry was already selected (don't advance)
	std::mutex m_fileMutex;                             ///< mutex to protect the file information below
	int m_fileNumber = 0;                               ///< number of the played files (incremented with every new file)
	std::string m_fileTitle;                            ///< title of the current file
	std::string m_filePath;                             ///< path of the current file

	// current file (protected by the thread lock)
	AVFormatContext *m_pFormat = nullptr;               ///< format context of the current file
	AVPacket *m_pPacket = nullptr;                      ///< packet, which wasn't accepted by the device yet
	int m_audioIdx = -1;                                ///< index of the audio stream (-1, if none)
	int m_videoIdx = -1;                                ///< index of the video stream (-1, if none)
	static constexpr int NO_TRACK_REQUEST = -2;         ///< no track change was requested
	/** stream index of the audio track selected by the user */
	std::atomic<int> m_requestedAudioIdx = NO_TRACK_REQUEST;
	bool m_resyncAfterPause = false;                    ///< true, if the audio track changed in pause mode
	int m_subtitleIdx = -1;                             ///< index of the shown subtitle stream (-1, if none)
	/** stream index of the subtitle track selected by VDR or the user (-1: off) */
	std::atomic<int> m_requestedSubtitleIdx = NO_TRACK_REQUEST;
	AVCodecContext *m_pSubtitleCtx = nullptr;           ///< decoder of the shown subtitle stream
	cTextSubtitles m_subtitles;                         ///< shows the subtitles
	int64_t m_startTime = 0;                            ///< start time of the file (AV_TIME_BASE units)
	std::atomic<int64_t> m_durationMs = 0;              ///< duration of the file in ms (0, if unknown)
	std::atomic<double> m_fps = DEFAULTFRAMESPERSECOND; ///< frame rate of the file
	std::atomic<bool> m_seekable = false;               ///< true, if the file is seekable

	// replay state (like cDvbPlayer)
	ePlayModes m_playMode = pmPlay;                     ///< current play mode
	ePlayDirs m_playDir = pdForward;                    ///< current play direction
	int m_trickSpeed = NORMAL_SPEED;                    ///< index into SPEEDS

	int64_t m_seekMs = -1;                              ///< position to read from next (-1, if the reading continues)
	bool m_stillPending = false;                        ///< true, if a still picture shall be shown at m_seekMs
	int64_t m_trickMs = 0;                              ///< position of the last frame sent in trick mode
	bool m_eof = false;                                 ///< true, if the end of the file was read
	bool m_bof = false;                                 ///< true, if the begin of the file was reached in backward trick mode
	cTimeMs m_stuckTimer;                               ///< time since the position doesn't change at the begin/end of the file
	int64_t m_stuckPositionMs = -1;                     ///< position at the begin/end of the file, which is checked for being stuck
	std::atomic<int64_t> m_positionMs = 0;              ///< last known playback position in ms
	std::atomic<int64_t> m_staleStc = AV_NOPTS_VALUE;   ///< stc after an Empty(), which doesn't belong to the new position
};

/**
 * Media Player Control
 *
 * The VDR control of the mediaplayer (keys, replay osd). Only one instance
 * exists at a time.
 *
 * @ingroup mediaplayer
 */
class cDrmDeviceControl : public cControl {
public:
	cDrmDeviceControl(const char *, cOutputDevice *);
	virtual ~cDrmDeviceControl();

	virtual void Show(void);
	virtual void Hide(void);
	virtual eOSState ProcessKey(eKeys);
	static cDrmDeviceControl *Control() { return m_pControl; } ///< get the current control (nullptr, if none)
	static cDrmDevicePlayer *Player() { return m_pPlayer; }    ///< get the current player (nullptr, if none)
	void Close(void) { m_closing = true; }                     ///< close the control (end of playback)

private:
	void ShowTimed(int = 0);
	void ShowMode(void);
	bool ShowProgress(bool);
	void TimeSearchDisplay(void);
	void TimeSearchProcess(eKeys);
	void TimeSearch(void);
	void CheckNewFile(void);
	void StopReplaying(void);

	static cDrmDeviceControl *m_pControl;               ///< the current control
	static cDrmDevicePlayer *m_pPlayer;                 ///< the current player
	cSkinDisplayReplay *m_pDisplayReplay = nullptr;     ///< replay display
	std::atomic<bool> m_closing = false;                ///< true, if the control shall be closed

	// replay display (like cReplayControl)
	bool m_visible = false;                             ///< true, if the replay display is shown
	bool m_modeOnly = false;                            ///< true, if only the replay mode is shown
	bool m_shown = false;                               ///< true, if the progress was shown
	int m_lastCurrent = -1;                             ///< last shown position (frame index)
	int m_lastTotal = -1;                               ///< last shown total length (frame index)
	bool m_lastPlay = false;                            ///< last shown play mode
	bool m_lastForward = false;                         ///< last shown direction
	int m_lastSpeed = -2;                               ///< last shown speed (-2 = invalid)
	time_t m_timeoutShow = 0;                           ///< time, when the replay display is hidden (0 = never)
	cTimeMs m_updateTimer;                              ///< timer for the progress updates
	bool m_timeSearchActive = false;                    ///< true, if the time search is active
	bool m_timeSearchHide = false;                      ///< true, if the replay display is hidden after the time search
	int m_timeSearchTime = 0;                           ///< entered digits of the time search
	int m_timeSearchPos = 0;                            ///< number of entered digits of the time search

	int m_fileNumber = 0;                               ///< number of the file, which is shown
	std::string m_title;                                ///< title of the current file
	std::string m_path;                                 ///< path of the current file
};

#endif
