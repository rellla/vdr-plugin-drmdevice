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
#include <string>

#include <vdr/player.h>

class cAudioStream;
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
 * @ingroup mediaplayer
 */
class cDrmDevicePlayer : public cPlayer, cThread {
public:
	cDrmDevicePlayer(const char *, cOutputDevice *);
	virtual ~cDrmDevicePlayer();

	void SetEntry(int);
	const char *GetSource(void) { return m_pSource; }   ///< get the played file or playlist

	void JumpSec(int seconds) { m_jumpSec = seconds; }                        ///< jump the given seconds forward/backward
	void Pause(bool pause) { m_paused = pause; }                              ///< pause/resume the playback
	bool IsPaused(void) { return m_paused; }                                  ///< true, if the playback is paused
	void Stop(void) { m_stopped = true; }                                     ///< stop the current file (a playlist continues with the next one)
	void ToggleRandomPlay(void) { m_random = !m_random; }                     ///< toggle random play of the playlist
	bool IsRandomPlayActive(void) { return m_random; }                        ///< true, if random play is active
	int GetCurrentTime(void) { return m_currentTime; }                        ///< get the current playback position in seconds
	int GetDuration(void) { return m_duration; }                              ///< get the duration of the current file in seconds
	cPlaylistEntry *GetFirstPlaylistEntry(void) { return m_pFirstEntry; }     ///< get the first playlist entry
	cPlaylistEntry *GetCurrentPlaylistEntry(void) { return m_pCurrentEntry; } ///< get the currently played playlist entry

protected:
	virtual void Activate(bool On);
	virtual void Action(void);

private:
	void Play(const char *);
	void ReadPlaylist(const char *);

	cPlaylistEntry *m_pFirstEntry = nullptr;            ///< first playlist entry
	cPlaylistEntry *m_pCurrentEntry = nullptr;          ///< currently played playlist entry

	char *m_pSource;                                    ///< played file or playlist
	int m_entries = 0;                                  ///< number of playlist entries
	cOutputDevice *m_pDevice;                           ///< pointer to the device
	cAudioStream *m_pAudio;                             ///< pointer to the audio stream
	std::atomic<int> m_jumpSec = 0;                     ///< requested jump in seconds
	std::atomic<bool> m_paused = false;                 ///< true, if paused
	std::atomic<bool> m_stopped = false;                ///< true, if the current file shall be stopped
	std::atomic<bool> m_random = false;                 ///< true, if random play is active
	bool m_noModify = false;                            ///< true, if the next entry was already selected (don't advance)
	int m_currentTime = 0;                              ///< current playback position in seconds
	int m_duration = 0;                                 ///< duration of the current file in seconds
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

	virtual void Hide(void);
	virtual eOSState ProcessKey(eKeys);
	static cDrmDeviceControl *Control() { return m_pControl; } ///< get the current control (nullptr, if none)
	static cDrmDevicePlayer *Player() { return m_pPlayer; }    ///< get the current player (nullptr, if none)
	void Close(void) { m_closing = true; }                     ///< close the control (end of playback)

private:
	void ShowProgress();

	static cDrmDeviceControl *m_pControl;               ///< the current control
	static cDrmDevicePlayer *m_pPlayer;                 ///< the current player
	cSkinDisplayReplay *m_pOsd = nullptr;               ///< replay osd
	cOutputDevice *m_pDevice;                           ///< pointer to the device
	bool m_closing = false;                             ///< true, if the control shall be closed
};

#endif
