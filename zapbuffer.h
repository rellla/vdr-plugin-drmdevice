// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zapbuffer.h
 * Zap Buffer Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_ZAPBUFFER_H
#define __DRMDEVICE_ZAPBUFFER_H

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Buffer for the TS packets of a channel since its last independent frame
 *
 * The packets of all pids are kept in their order. A new independent frame
 * (I-frame) discards the older packets, so a channel switch can start with
 * an I-frame at once instead of waiting for the next one. It doesn't depend
 * on VDR, the frames are detected by the caller.
 *
 * The buffer starts with the video PES packet of the I-frame: a frame detector
 * may detect the start of a frame in a later TS packet of the PES packet (e.g.
 * VDR's H.265 parser, if the slice follows the parameter sets).
 *
 * @ingroup zapcache
 */
class cZapBuffer {
public:
	/** max. frames since the I-frame (the device buffers them in addition to the live data, 1.28s at 50 fps) */
	static constexpr int MAX_FRAMES = 64;
	static constexpr size_t MAX_BYTES = 8 * 1024 * 1024; ///< max. size of the buffered packets

	void Put(const uint8_t *, bool, bool, bool);
	std::vector<uint8_t> Take(const uint8_t *);
	void Reset(void);

	bool Ready(void) const { return m_ready; }             ///< true, if the buffer starts with an I-frame
	int Frames(void) const { return m_frames; }            ///< number of frames since the I-frame
	size_t Size(void) const { return m_data.size(); }      ///< size of the buffered packets in bytes

private:
	static constexpr size_t NO_PES_START = static_cast<size_t>(-1); ///< m_pesStart: no video PES packet started

	std::vector<uint8_t> m_data;      ///< TS packets since the last I-frame (or since the last video PES start, while not ready)
	size_t m_pesStart = NO_PES_START; ///< offset of the last video PES start in m_data
	int m_frames = 0;                 ///< number of frames since the I-frame (including it)
	bool m_ready = false;             ///< the buffer starts with an I-frame
};

#endif
