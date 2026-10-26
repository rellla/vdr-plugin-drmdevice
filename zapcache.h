// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zapcache.h
 * Zap Cache Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_ZAPCACHE_H
#define __DRMDEVICE_ZAPCACHE_H

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include <vdr/receiver.h>
#include <vdr/remux.h>
#include <vdr/tools.h>

#include "zapbuffer.h"

class cChannel;
class cChannels;
class cDevice;

/**
 * Zap cache
 * @defgroup zapcache Zap Cache
 */

/**
 * Receiver for the channel, which is expected to be switched to next
 *
 * It receives the pids of the channel like VDR's transfer receiver does, so
 * the tuner of the channel is locked and the pids are streamed, before the
 * channel is switched to. It has the lowest priority, so it never blocks a
 * recording or the live view.
 *
 * The packets since the last I-frame are kept in a cZapBuffer, so a channel
 * switch can start with them (see cZapCache::Splice()).
 *
 * @ingroup zapcache
 */
class cZapReceiver : public cReceiver {
public:
	cZapReceiver(const cChannel *, cDevice *);
	virtual ~cZapReceiver(void);

	int ChannelNumber(void) const { return m_channelNumber; }                ///< number of the received channel
	cDevice *Device(void) const { return m_attached ? m_pDevice : nullptr; } ///< the receiving device (nullptr, if detached)
	bool HasPid(int) const;
	std::vector<uint8_t> Take(const uint8_t *, int *);

protected:
	virtual void Activate(bool);
	virtual void Receive(const uchar *, int);

private:
	int m_channelNumber;                    ///< number of the received channel
	cDevice *m_pDevice;                     ///< device, which receives the channel
	std::atomic<bool> m_attached = false;   ///< the receiver is attached (VDR may detach it, if it needs the device)
	std::vector<int> m_pids;                ///< received pids
	int m_vpid;                             ///< video pid (0: radio channel, nothing is buffered)

	std::mutex m_mutex;                     ///< protects the members below (receiver thread and Take())
	cFrameDetector m_frameDetector;         ///< detects the frames of the video pid
	std::vector<uint8_t> m_pending;         ///< packets, which the frame detector needs ahead
	cZapBuffer m_buffer;                    ///< packets since the last I-frame
	bool m_taken = false;                   ///< the buffer was taken, the receiver doesn't buffer anymore

	void AddChannelPid(int);
};

/**
 * Tunes the next channel in zap direction on a free device ahead
 *
 * After a channel switch the next channel in the direction of the last channel
 * switch (or the channel before for a switch back) is received by a cZapReceiver
 * on a free device. VDR prefers a device, which is tuned to the transponder of a
 * channel, so a switch to this channel doesn't have to tune and to wait for the
 * tuner lock.
 *
 * The switch to this channel starts with the packets since its last I-frame,
 * before VDR's transfer receiver plays its first packet (Splice()), so it
 * doesn't have to wait for the next I-frame.
 *
 * The receiver is started and stopped by the main thread (MainThreadHook()),
 * like VDR's own channel switches. The channel switches are reported by
 * cOutputDevice::ChannelSwitch(), which may be called by another thread (SVDRP).
 *
 * @ingroup zapcache
 */
class cZapCache {
public:
	cZapCache(void) {}
	~cZapCache(void);

	void ChannelSwitchStarted(void);
	void ChannelSwitched(int);
	void MainThreadHook(bool);
	void Clear(void);

	bool SpliceArmed(void) const { return m_spliceArmed; }   ///< true, if a channel switch may start with the buffered packets
	void Splice(const uchar *, const std::function<int(const uchar *, int)> &);

private:
	static constexpr int PREPARE_DELAY_MS = 500;   ///< delay after a channel switch (the live view gets the tuner first)
	static constexpr int SPLICE_TIMEOUT_MS = 3000; ///< max. time from a channel switch until the transfer plays its first packet
	static constexpr int PLAY_RETRIES = 20;        ///< max. number of retries for a buffered packet (like cTransfer)
	static constexpr int PLAY_RETRY_WAIT_MS = 5;   ///< time between two retries

	std::mutex m_mutex;                      ///< protects the members below
	cZapReceiver *m_pReceiver = nullptr;     ///< receiver of the next channel
	int m_liveChannel = 0;                   ///< current live channel
	int m_previousChannel = 0;               ///< live channel before the last channel switch
	bool m_preparePending = false;           ///< a channel was switched, the next channel has to be tuned
	cTimeMs m_prepareTimer;                  ///< delays the tuning of the next channel after a channel switch
	int m_direction = 1;                     ///< zap direction (1: up, -1: down)
	std::atomic<bool> m_spliceArmed = false; ///< a channel switch may start with the buffered packets
	int m_armedFromChannel = 0;              ///< live channel before the channel switch (its packets may still be played)
	cTimeMs m_spliceTimer;                   ///< keeps the receiver of the new live channel until the splice

	void Prepare(int, int);
	static const cChannel *Neighbour(const cChannels *, const cChannel *, int);
};

#endif
