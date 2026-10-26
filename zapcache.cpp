// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zapcache.cpp
 * Zap Cache
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <vdr/channels.h>
#include <vdr/config.h>
#include <vdr/device.h>

#include "logger.h"
#include "zapcache.h"

/*****************************************************************************
 * cZapReceiver class
 ****************************************************************************/

/**
 * Create a receiver for the pids of a channel
 *
 * The receiver gets the same pids as VDR's transfer receiver. It doesn't pass
 * a channel to cReceiver, so no CAM is started for it.
 *
 * @param channel    channel to receive (only used in the constructor)
 * @param device     device, which receives the channel (for the log)
 */
cZapReceiver::cZapReceiver(const cChannel *channel, cDevice *device)
	: cReceiver(NULL, MINPRIORITY),
	  m_channelNumber(channel->Number())
{
	AddPid(channel->Vpid());
	if (channel->Ppid() != channel->Vpid())
		AddPid(channel->Ppid());
	AddPids(channel->Apids());
	AddPids(channel->Dpids());
	AddPids(channel->Spids());

	LOGDEBUG("zapcache: tune channel %d (%s) ahead on device %d", m_channelNumber, channel->Name(), device->DeviceNumber() + 1);
}

/**
 * Detach the receiver
 */
cZapReceiver::~cZapReceiver(void)
{
	Detach();
}

/**
 * Called, when the receiver is attached or detached
 *
 * VDR detaches the receiver (e.g. from the main thread in cDevice::GetDevice()),
 * if it needs the device for another channel.
 *
 * @param on       true: attached, false: detached
 */
void cZapReceiver::Activate(bool on)
{
	m_attached = on;
}

/**
 * Receive the data of the channel
 *
 * The data isn't used, the receiver only keeps the tuner locked and the pids
 * streaming.
 */
void cZapReceiver::Receive([[maybe_unused]] const uchar *data, [[maybe_unused]] int length)
{
}

/*****************************************************************************
 * cZapCache class
 ****************************************************************************/

cZapCache::~cZapCache(void)
{
	Clear();
}

/**
 * Report a channel switch of the live view
 *
 * @param channelNum     number of the new live channel
 */
void cZapCache::ChannelSwitched(int channelNum)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_previousChannel = m_liveChannel;
	m_liveChannel = channelNum;
	m_preparePending = true;
	m_prepareTimer.Set(PREPARE_DELAY_MS);
}

/**
 * Stop the receiver of the next channel
 */
void cZapCache::Clear(void)
{
	cZapReceiver *receiver;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		receiver = m_pReceiver;
		m_pReceiver = nullptr;
	}

	// detached outside the mutex, cDevice::Detach() waits for the receiver thread of the device
	delete receiver;
}

/**
 * Tune the next channel after a channel switch
 *
 * Must be called by the main thread (cPlugin::MainThreadHook()).
 *
 * @param enabled       true, if the next channel should be tuned (the option is
 *                      enabled and the live view is in transfer mode)
 */
void cZapCache::MainThreadHook(bool enabled)
{
	if (!enabled) {
		bool hasReceiver;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_preparePending = false;
			hasReceiver = m_pReceiver;
		}
		if (hasReceiver)
			Clear();
		return;
	}

	int liveChannel;
	int previousChannel;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_preparePending || !m_prepareTimer.TimedOut())
			return;

		m_preparePending = false;
		liveChannel = m_liveChannel;
		previousChannel = m_previousChannel;
	}

	// the receiver of the last prepared channel isn't needed anymore
	Clear();

	Prepare(liveChannel, previousChannel);
}

/**
 * Get the next or previous channel in the channel list
 *
 * Group separators are skipped. The channel list wraps around, if this is enabled
 * in VDR's setup.
 *
 * @param channels      locked channel list
 * @param channel       channel to start from
 * @param direction     1: next channel, -1: previous channel
 *
 * @return              the neighbour channel (nullptr, if there is none)
 */
const cChannel *cZapCache::Neighbour(const cChannels *channels, const cChannel *channel, int direction)
{
	const cChannel *c = channel;

	while (true) {
		c = direction > 0 ? channels->Next(c) : channels->Prev(c);
		if (!c && Setup.ChannelsWrap)
			c = direction > 0 ? channels->First() : channels->Last();
		if (!c || c == channel)
			return nullptr;
		if (!c->GroupSep())
			return c;
	}
}

/**
 * Tune the next channel in zap direction on a free device
 *
 * @param liveChannel       current live channel
 * @param previousChannel   live channel before the last switch
 */
void cZapCache::Prepare(int liveChannel, int previousChannel)
{
	LOCK_CHANNELS_READ;
	const cChannel *live = Channels->GetByNumber(liveChannel);
	if (!live)
		return;

	// a switch to the next or previous channel sets the zap direction, other switches keep it
	if (const cChannel *previous = Channels->GetByNumber(previousChannel)) {
		if (Neighbour(Channels, previous, 1) == live)
			m_direction = 1;
		else if (Neighbour(Channels, previous, -1) == live)
			m_direction = -1;
	}

	const cChannel *next = Neighbour(Channels, live, m_direction);
	if (!next || (!next->Vpid() && !next->Apid(0) && !next->Dpid(0)))
		return;

	// a device, which is tuned to the transponder already or may be tuned without disturbing a receiver
	cDevice *device = cDevice::GetDeviceForTransponder(next, MINPRIORITY);
	if (!device) {
		LOGDEBUG("zapcache: no free device for channel %d (%s)", next->Number(), next->Name());
		return;
	}

	if (!device->IsTunedToTransponder(next) && !device->SwitchChannel(next, false)) {
		LOGDEBUG("zapcache: can't tune channel %d (%s) on device %d", next->Number(), next->Name(), device->DeviceNumber() + 1);
		return;
	}

	cZapReceiver *receiver = new cZapReceiver(next, device);
	if (!device->AttachReceiver(receiver)) {
		LOGDEBUG("zapcache: can't attach the receiver for channel %d to device %d", next->Number(), device->DeviceNumber() + 1);
		delete receiver;
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_pReceiver = receiver;
}
