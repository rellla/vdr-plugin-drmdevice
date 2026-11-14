// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file decoderwatchdog.h
 * Decoder Watchdog Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_DECODERWATCHDOG_H
#define __DRMDEVICE_DECODERWATCHDOG_H

#include <cstdint>

/**
 * Decoder Watchdog
 *
 * Detects a decoder, which stalls without an error: it gets packets, but
 * doesn't output any frame. The time only counts, while the decoder could make
 * progress (e.g. packets are waiting and the output isn't full), so a paused
 * playback or an empty input doesn't trigger it.
 *
 * @ingroup video
 */
class cDecoderWatchdog {
public:
	/** Create a watchdog, which triggers after the given time without progress */
	explicit cDecoderWatchdog(int64_t timeoutMs) : m_timeoutMs(timeoutMs) {}

	/** The decoder made progress (or can't make progress): restart the time */
	void Progress(int64_t nowMs) { m_progressMs = nowMs; }

	/**
	 * Check, if the decoder stalled
	 *
	 * @param nowMs     current time in ms
	 * @param waiting   true, if the decoder should output frames (otherwise the time is restarted)
	 *
	 * @return true, if the decoder didn't make progress for the timeout, while it was waited for
	 */
	bool Stalled(int64_t nowMs, bool waiting)
	{
		if (!waiting || m_progressMs < 0) {
			m_progressMs = nowMs;
			return false;
		}

		return nowMs - m_progressMs >= m_timeoutMs;
	}

private:
	int64_t m_timeoutMs;            ///< time without progress, after which the decoder stalled
	int64_t m_progressMs = -1;      ///< time of the last progress (-1: none yet)
};

#endif
