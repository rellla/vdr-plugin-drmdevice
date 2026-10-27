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
#include <string>
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
 * The data of a channel, which a zap receiver needs
 *
 * A copy, so a receiver can be created without the channels lock.
 *
 * @ingroup zapcache
 */
struct sZapChannel {
	int number = 0;                 ///< channel number (0: none)
	std::string name;               ///< channel name (for the log)
	int vpid = 0;                   ///< video pid
	int vtype = 0;                  ///< video stream type
	int ppid = 0;                   ///< pcr pid
	std::vector<int> apids;         ///< audio pids
	std::vector<int> dpids;         ///< dolby pids
	std::vector<int> spids;         ///< subtitle pids

	static sZapChannel FromChannel(const cChannel *);
};

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
	cZapReceiver(const sZapChannel &, cDevice *);
	virtual ~cZapReceiver(void);

	int ChannelNumber(void) const { return m_channelNumber; }                ///< number of the received channel
	cDevice *Device(void) const { return m_attached ? m_pDevice : nullptr; } ///< the receiving device (nullptr, if detached)
	bool HasPid(int) const;
	bool IsTaken(void);
	std::vector<uint8_t> Take(const uint8_t *, int64_t, int *, bool *);

protected:
	virtual void Activate(bool);
	virtual void Receive(const uchar *, int);

private:
	int m_channelNumber;                    ///< number of the received channel
	cDevice *m_pDevice;                     ///< device, which receives the channel
	std::atomic<bool> m_attached = false;   ///< the receiver is attached (VDR may detach it, if it needs the device)
	std::vector<int> m_pids;                ///< received pids
	std::vector<int> m_audioPids;           ///< received audio pids
	int m_vpid;                             ///< video pid (0: radio channel, nothing is buffered)

	std::mutex m_mutex;                     ///< protects the members below (receiver thread and Take())
	cFrameDetector m_frameDetector;         ///< detects the frames of the video pid
	std::vector<uint8_t> m_pending;         ///< packets, which the frame detector needs ahead
	cZapBuffer m_buffer;                    ///< packets since the last I-frame
	bool m_taken = false;                   ///< the buffer was taken, the receiver doesn't buffer anymore

	void AddChannelPid(int);
	int PesStartFlags(const uchar *) const;
};

/**
 * Tunes the neighbour channels of the live channel on free devices ahead
 *
 * After a channel switch the next channel in the direction of the last channel
 * switch is received by a cZapReceiver on a free device. After a switch to
 * another channel (or the first switch after VDR's start) the zap direction is
 * up. VDR prefers a device, which is tuned to the transponder of a channel, so
 * a switch to this channel doesn't have to tune and to wait for the tuner lock.
 *
 * The next channel doesn't take the device, which is tuned to the channel in the
 * opposite direction (usually the device of the last live channel), if another
 * device is free. So a switch back doesn't have to tune.
 *
 * In ZAP_AHEAD_BOTH mode (three tuners: live view, next and previous channel)
 * the channel in the opposite direction is received, too, preferably on the
 * device, which is tuned to it already. The live channel has a receiver on the
 * device of its transfer, too. VDR stops the transfer, before it reports a
 * channel switch, so this receiver keeps the stream and the buffer of the last
 * live channel, a switch back can start with it at once.
 *
 * The receivers of the channels, which are neighbours after a channel switch,
 * too, are kept with their buffers.
 *
 * The switch to this channel starts with the packets since a buffered I-frame,
 * before VDR's transfer receiver plays its first packet (Splice()), so it
 * doesn't have to wait for the next I-frame. It may start with an older I-frame,
 * whose synchronous audio is buffered, too, so the audio doesn't have to be
 * waited for.
 *
 * The receivers are released after a time without key press, so VDR's EPG scan
 * (which starts 60s after the last key press) can use the devices. The next key
 * press tunes the channels again, but not, while a menu is shown.
 *
 * The receiver is started and stopped by the main thread (MainThreadHook()),
 * like VDR's own channel switches. The channel switches are reported by
 * cOutputDevice::ChannelSwitch(), which may be called by another thread (SVDRP).
 *
 * @ingroup zapcache
 */
class cZapCache {
public:
	/** the channels, which are tuned ahead (drmdevice.ZapAhead) */
	enum Mode {
		ZAP_AHEAD_OFF,                     ///< no channel
		ZAP_AHEAD_NEXT,                    ///< the next channel in zap direction (two tuners)
		ZAP_AHEAD_BOTH                     ///< the next channel and the channel in the opposite direction (three tuners)
	};

	cZapCache(void);
	~cZapCache(void);

	static int NumTuners(void);
	static int ReleaseSeconds(int);

	void ChannelSwitchStarted(void);
	void SetMode(int mode) { m_mode = mode; }                     ///< set the current mode (enum Mode)
	void ChannelSwitched(int);
	void MainThreadHook(int, int);
	void Clear(void);

	bool SpliceArmed(void) const { return m_spliceArmed; }   ///< true, if a channel switch may start with the buffered packets
	void Splice(const uchar *, int64_t, const std::function<int(const uchar *, int)> &);

private:
	static constexpr int PREPARE_DELAY_MS = 500;   ///< delay after a channel switch (the live view gets the tuner first)
	static constexpr int SPLICE_TIMEOUT_MS = 3000; ///< max. time from a channel switch until the transfer plays its first packet
	static constexpr int PLAY_RETRIES = 20;        ///< max. number of retries for a buffered packet (like cTransfer)
	static constexpr int PLAY_RETRY_WAIT_MS = 5;   ///< time between two retries
	static constexpr int MAX_RECEIVERS = 2;        ///< zap direction and opposite direction
	static constexpr int RELEASE_TIMES = 5;        ///< number of the release times (drmdevice.ZapAheadRelease)

	std::mutex m_mutex;                      ///< protects the members below
	/** receivers of the next channel and of the channel in the opposite direction */
	cZapReceiver *m_pReceivers[MAX_RECEIVERS] = {};
	int m_liveChannel = 0;                   ///< current live channel
	int m_previousChannel = 0;               ///< live channel before the last channel switch
	bool m_preparePending = false;           ///< a channel was switched, the next channel has to be tuned
	cTimeMs m_prepareTimer;                  ///< delays the tuning of the next channel after a channel switch
	int m_direction = 1;                     ///< zap direction (1: up, -1: down)
	std::atomic<bool> m_spliceArmed = false; ///< a channel switch may start with the buffered packets
	int m_armedFromChannel = 0;              ///< live channel before the channel switch (its packets may still be played)
	cTimeMs m_spliceTimer;                   ///< keeps the receiver of the new live channel until the splice
	cZapReceiver *m_pLive = nullptr;         ///< receiver of the live channel (ZAP_AHEAD_BOTH), it keeps its buffer after a switch
	std::atomic<int> m_mode = ZAP_AHEAD_OFF; ///< current mode (enum Mode)
	time_t m_lastSwitch;                     ///< time of the last channel switch (also by SVDRP)
	bool m_released = false;                 ///< the receivers were released without key press (main thread only)

	bool HasReceivers(void) const;
	cZapReceiver *ReceiverFor(int) const;
	void Prepare(int, int, int);
	static const cChannel *Neighbour(const cChannels *, const cChannel *, int);
	static cDevice *FreeDevice(const cChannel *, const cDevice *);
	static cDevice *TunedDevice(const cChannel *);
	static cZapReceiver *Attach(const cChannel *, cDevice *);
	static cZapReceiver *Keep(cZapReceiver **, int, const cChannel *);
};

#endif
