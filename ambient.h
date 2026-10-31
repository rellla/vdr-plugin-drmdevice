// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ambient.h
 * Ambient Image Service Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_AMBIENT_H
#define __DRMDEVICE_AMBIENT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "ambientimage_service.h"
#include "framesampler.h"
#include "framesnapshot.h"

class cDrmBuffer;

/**
 * Ambient Image Service
 *
 * Provides a small RGB image of the displayed video to other plugins
 * (service AMBIENTIMAGE_SERVICE, e.g. for vdr-plugin-hyperion).
 *
 * The display thread only offers the displayed buffer with Offer(). This never
 * blocks and does nothing, unless a consumer waits for a new frame: then a
 * snapshot (duplicated DMA-BUF descriptors) is handed over. The image is read
 * from the snapshot in the thread of the consumer, in GetImage().
 *
 * It belongs to the device, so it exists also in detached state.
 *
 * @ingroup misc
 */
class cAmbientService {
public:
	void Offer(cDrmBuffer *, int, int);
	bool GetImage(AmbientImage_v1_0_t *);
	void SetPaused(bool paused) { m_paused = paused; }  ///< the playback is paused (VDR's Freeze())
	void SetDetached(bool);
	uint64_t SnapshotsTaken(void) const { return m_snapshotsTaken; } ///< number of taken snapshots (for the tests)

private:
	std::mutex m_mutex;                     ///< protects the members below
	std::condition_variable m_cond;         ///< signals a new snapshot or a detach
	cFrameSnapshot m_snapshot;              ///< the offered snapshot
	unsigned int m_frame = 0;               ///< number of the offered snapshot
	bool m_pending = false;                 ///< m_snapshot waits for a consumer
	int m_screenWidth = 0;                  ///< screen width of the snapshot
	int m_screenHeight = 0;                 ///< screen height of the snapshot

	std::atomic<int> m_waiting = 0;         ///< number of consumers, which wait for a new frame
	std::atomic<bool> m_videoShown = false; ///< a video is displayed
	std::atomic<bool> m_paused = false;     ///< the playback is paused
	std::atomic<bool> m_detached = true;    ///< the device is detached
	std::atomic<uint64_t> m_snapshotsTaken = 0; ///< number of taken snapshots

	// display thread only
	int m_lastFbId = -1;                    ///< framebuffer of the last snapshot
	int64_t m_lastPts = 0;                  ///< pts of the last snapshot

	cFrameSampler m_sampler;                ///< reads the image (only used under m_sampleMutex)
	std::mutex m_sampleMutex;               ///< serializes concurrent consumers

	bool CreateImage(cFrameSnapshot &, int, int, AmbientImage_v1_0_t *);
};

#endif
