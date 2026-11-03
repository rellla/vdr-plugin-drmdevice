// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file testpicture.cpp
 * Test Picture
 *
 * Shows the test picture of the plugin (DEVELOPER/testpicture_data.h) as a still picture.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>

#include "logger.h"
#include "testpicture.h"
#include "DEVELOPER/testpicture_data.h"

/** max. payload of a PES packet (the 16 bit length counts the header after it, too) */
static constexpr size_t PES_MAX_PAYLOAD = 0xFFFF - 3 - 5;

/*****************************************************************************
 * cTestPicturePlayer class
 ****************************************************************************/

/**
 * Create the player and pack the test picture into PES packets
 *
 * The first packet has a pts, so the device detects the start of the frame.
 */
cTestPicturePlayer::cTestPicturePlayer(void)
	: cPlayer(pmVideoOnly)
{
	const int64_t pts = 0;
	size_t pos = 0;

	while (pos < sizeof(s_testPictureH264)) {
		bool first = pos == 0;
		size_t payload = std::min(PES_MAX_PAYLOAD, sizeof(s_testPictureH264) - pos);
		size_t length = 3 + (first ? 5 : 0) + payload;

		uint8_t header[14] = {
			0x00, 0x00, 0x01, 0xE0,                                          // video stream
			static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length),
			0x80,                                                            // MPEG-2 PES
			static_cast<uint8_t>(first ? 0x80 : 0x00),                       // pts
			static_cast<uint8_t>(first ? 5 : 0),                             // header data length
			static_cast<uint8_t>(0x21 | ((pts >> 29) & 0x0E)),
			static_cast<uint8_t>(pts >> 22),
			static_cast<uint8_t>(0x01 | ((pts >> 14) & 0xFE)),
			static_cast<uint8_t>(pts >> 7),
			static_cast<uint8_t>(0x01 | ((pts << 1) & 0xFE)),
		};

		m_pes.insert(m_pes.end(), header, header + (first ? 14 : 9));
		m_pes.insert(m_pes.end(), s_testPictureH264 + pos, s_testPictureH264 + pos + payload);
		pos += payload;
	}
}

cTestPicturePlayer::~cTestPicturePlayer()
{
	Detach();
}

/**
 * Show the test picture, when the player is attached to the device
 *
 * @param on        true: attached, false: detached
 */
void cTestPicturePlayer::Activate(bool on)
{
	if (!on)
		return;

	LOGDEBUG2(L_STILL, "testpicture: %s: show the test picture (%zu bytes)", __FUNCTION__, m_pes.size());
	DeviceClear();
	DeviceStillPicture(m_pes.data(), static_cast<int>(m_pes.size()));
}

/*****************************************************************************
 * cTestPictureControl class
 ****************************************************************************/

cTestPictureControl::cTestPictureControl(void)
	: cControl(m_pPlayer = new cTestPicturePlayer())
{
}

cTestPictureControl::~cTestPictureControl()
{
	delete m_pPlayer;
}

/**
 * Process a key
 *
 * Back, OK and Stop close the test picture, VDR switches to the live view again.
 * The other keys are left to VDR (e.g. the volume).
 *
 * @param key       the pressed key
 */
eOSState cTestPictureControl::ProcessKey(eKeys key)
{
	switch (key) {
		case kBack:
		case kOk:
		case kStop:
			return osEnd;
		default:
			return osUnknown;
	}
}
