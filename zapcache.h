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
#include <mutex>

#include <vdr/receiver.h>
#include <vdr/tools.h>

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
 * @ingroup zapcache
 */
class cZapReceiver : public cReceiver {
public:
	cZapReceiver(const cChannel *, cDevice *);
	virtual ~cZapReceiver(void);

	int ChannelNumber(void) const { return m_channelNumber; }                ///< number of the received channel
	bool IsAttached(void) const { return m_attached; }                       ///< true, if the receiver is attached to its device

protected:
	virtual void Activate(bool);
	virtual void Receive(const uchar *, int);

private:
	int m_channelNumber;                    ///< number of the received channel
	std::atomic<bool> m_attached = false;   ///< the receiver is attached (VDR may detach it, if it needs the device)
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

	void ChannelSwitched(int);
	void MainThreadHook(bool);
	void Clear(void);

private:
	static constexpr int PREPARE_DELAY_MS = 500;  ///< delay after a channel switch (the live view gets the tuner first)

	std::mutex m_mutex;                     ///< protects the members below
	cZapReceiver *m_pReceiver = nullptr;    ///< receiver of the next channel
	int m_liveChannel = 0;                  ///< current live channel
	int m_previousChannel = 0;              ///< live channel before the last channel switch
	bool m_preparePending = false;          ///< a channel was switched, the next channel has to be tuned
	cTimeMs m_prepareTimer;                 ///< delays the tuning of the next channel after a channel switch
	int m_direction = 1;                    ///< zap direction (1: up, -1: down)

	void Prepare(int, int);
	static const cChannel *Neighbour(const cChannels *, const cChannel *, int);
};

#endif
