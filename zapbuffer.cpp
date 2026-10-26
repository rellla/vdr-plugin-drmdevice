// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zapbuffer.cpp
 * Zap Buffer
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <cstring>

#include <vdr/remux.h>

#include "zapbuffer.h"

/**
 * Add a TS packet
 *
 * A scrambled packet or a too long GOP discards the buffer until the next
 * I-frame.
 *
 * @param packet            TS packet (TS_SIZE bytes)
 * @param videoPesStart     the packet starts a video PES packet
 * @param frameStart        a video frame starts in the packet
 * @param independentFrame  the started frame is an independent frame (I-frame)
 */
void cZapBuffer::Put(const uint8_t *packet, bool videoPesStart, bool frameStart, bool independentFrame)
{
	// a channel switch can't start with scrambled data
	if (TsIsScrambled(packet)) {
		Reset();
		return;
	}

	if (videoPesStart) {
		// while waiting for an I-frame, only the PES packet is kept, which may contain it
		if (!m_ready)
			m_data.clear();
		m_pesStart = m_data.size();
	}

	if (frameStart && independentFrame) {
		if (m_pesStart == NO_PES_START) {
			Reset();
			return;
		}
		// the I-frame starts with its PES packet
		m_data.erase(m_data.begin(), m_data.begin() + m_pesStart);
		m_pesStart = 0;
		m_frames = 0;
		m_ready = true;
	}

	if (!m_ready && m_pesStart == NO_PES_START)
		return;

	if (frameStart && m_ready)
		m_frames++;

	if (m_frames > MAX_FRAMES || m_data.size() + TS_SIZE > MAX_BYTES) {
		Reset();
		return;
	}

	m_data.insert(m_data.end(), packet, packet + TS_SIZE);
}

/**
 * Take the buffered packets, which precede a given packet
 *
 * The given packet is played next. It is the last buffered one, if the buffer
 * got it before (the receivers of a device get the same packets one after
 * another), otherwise all buffered packets precede it. The buffer is empty
 * afterwards.
 *
 * @param next        TS packet, which is played next (may be nullptr)
 *
 * @return            the packets from the last I-frame up to the next packet
 *                    (empty, if the buffer doesn't start with an I-frame)
 */
std::vector<uint8_t> cZapBuffer::Take(const uint8_t *next)
{
	std::vector<uint8_t> data;

	if (m_ready) {
		data.swap(m_data);
		if (next && data.size() >= TS_SIZE && !memcmp(&data[data.size() - TS_SIZE], next, TS_SIZE))
			data.resize(data.size() - TS_SIZE);
	}

	Reset();

	return data;
}

/**
 * Discard the buffered packets and wait for the next I-frame
 */
void cZapBuffer::Reset(void)
{
	m_data.clear();
	m_pesStart = NO_PES_START;
	m_frames = 0;
	m_ready = false;
}
