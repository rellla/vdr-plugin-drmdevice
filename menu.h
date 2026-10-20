// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file menu.h
 * Plugin Main Menu Header File
 *
 * @copyright 2020 zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_MENU_H
#define __DRMDEVICE_MENU_H

#include <string>

#include <vdr/osdbase.h>

class cOutputDevice;

/**
 * Plugin Menus
 * @defgroup menu Menus
 */

/**
 * Hotkey States
 *
 * @ingroup menu
 */
typedef enum {
	Initial,       ///< no hotkey prefix pressed
	Blue,          ///< blue was pressed, a number selects a pip function
	Red            ///< red was pressed, a number selects a detach/attach function
} HotkeyState;

/**
 * Plugin Main Menu
 *
 * Main menu with the pip, detach and mediaplayer functions, and the
 * mediaplayer's file and playlist menus.
 *
 * @ingroup menu
 */
class cDeviceMenu : public cOsdMenu {
public:
	cDeviceMenu(const char *, cOutputDevice *, int = 0, int = 0, int = 0, int = 0, int = 0);
	virtual ~cDeviceMenu();
	static cDeviceMenu *pMenu;                          ///< the currently open menu (nullptr, if none)
	static cDeviceMenu *Menu() { return pMenu; }        ///< get the currently open menu

	// mediaplayer
	static void OpenMediaMenu(const char *);
	virtual eOSState ProcessKey(eKeys);

private:
	cOutputDevice *m_pDevice;                           ///< pointer to the device

	HotkeyState m_hotkeyState = HotkeyState::Initial;   ///< current hotkey state
	void HandleHotKey(int);

	// mediaplayer
	void PlayListMenu(void);
	void UpdatePlayListMenu(void);
	void MainMenu(void);
	void MediaMenu(const std::string &);
	void SelectPlaylistMenu(void);
	void FindFileMenu(const std::string &, FILE *);
	void MakePlayList(const char *, const char *);
	bool IsValidMediaFile(const char *);
	void PlayMedia(const char *);
	std::string m_path;                                 ///< current directory of the file menu
	std::string m_lastItem;                             ///< last selected item (to select it again)
	std::string m_playlist;                             ///< name of the current playlist
	bool m_playlistShown = false;                       ///< true, if the playlist menu is shown
	cPlaylistEntry *m_pShownEntry = nullptr;            ///< playlist entry, which is selected in the playlist menu
	static bool s_openMediaMenu;                        ///< true, if the next menu shall show the file of the stopped mediaplayer
	static std::string s_mediaSource;                   ///< file or playlist of the stopped mediaplayer
};

#endif
