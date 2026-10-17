// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file jittertracker.h
 * Jitter Tracking of Incoming Packets Header File
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_JITTERTRACKER_H
#define __DRMDEVICE_JITTERTRACKER_H

#include <atomic>
#include <chrono>

/**
 * Jitter Tracker
 *
 * @ingroup misc
 */
class cJitterTracker {
public:
	cJitterTracker(const char* identifier) : m_identifier(identifier) {}
	void PacketReceived(void);
	void Reset(void);
	int GetLongTermMaxJitterMs(void) { return m_longTermMaxJitterMs; };
	int GetShortTermMaxJitterMs(void) { return m_shortTermMaxJitterMs; };

private:
	int64_t m_lastDiffMs = 0;                           ///< time between the last two packets
	std::chrono::steady_clock::time_point m_lastTime;   ///< arrival time of the last packet
	std::atomic<int> m_shortTermMaxJitterMs = 0;        ///< short term maximum jitter
	int m_packetCounter = 0;                            ///< number of received packets
	std::atomic<int> m_longTermMaxJitterMs = 0;         ///< long term maximum jitter
	bool m_firstPacket = true;                          ///< true, until the first packet was received
	bool m_secondPacket = true;                         ///< true, until the second packet was received
	[[maybe_unused]] const char *m_identifier; ///< stream name (for debugging)
};

#endif
