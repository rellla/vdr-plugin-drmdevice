// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file testpicture.h
 * Test Picture Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_TESTPICTURE_H
#define __DRMDEVICE_TESTPICTURE_H

#include <cstdint>
#include <vector>

#include <vdr/player.h>

/**
 * Player, which shows the test picture as a still picture
 *
 * The test picture is a single H.264 I-frame (DEVELOPER/testpicture_data.h).
 * It is sent in PES packets to the device like a still picture of a recording.
 *
 * @ingroup mediaplayer
 */
class cTestPicturePlayer : public cPlayer {
public:
	cTestPicturePlayer(void);
	virtual ~cTestPicturePlayer();

protected:
	virtual void Activate(bool);

private:
	std::vector<uint8_t> m_pes;     ///< the test picture in PES packets
};

/**
 * Control, which shows the test picture until Back, OK or Stop is pressed
 *
 * @ingroup mediaplayer
 */
class cTestPictureControl : public cControl {
public:
	cTestPictureControl(void);
	virtual ~cTestPictureControl();

	virtual void Hide(void) {}
	virtual eOSState ProcessKey(eKeys);

private:
	cTestPicturePlayer *m_pPlayer;  ///< the player
};

#endif
