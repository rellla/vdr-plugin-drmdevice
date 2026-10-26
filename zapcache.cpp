// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zapcache.cpp
 * Zap Cache
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>

#include <vdr/channels.h>
#include <vdr/config.h>
#include <vdr/device.h>
#include <vdr/transfer.h>

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
 * @param device     device, which receives the channel
 */
cZapReceiver::cZapReceiver(const cChannel *channel, cDevice *device)
	: cReceiver(NULL, MINPRIORITY),
	  m_channelNumber(channel->Number()),
	  m_pDevice(device),
	  m_vpid(channel->Vpid())
{
	AddChannelPid(channel->Vpid());
	AddChannelPid(channel->Ppid());
	for (const int *pid = channel->Apids(); *pid; pid++)
		AddChannelPid(*pid);
	for (const int *pid = channel->Dpids(); *pid; pid++)
		AddChannelPid(*pid);
	for (const int *pid = channel->Spids(); *pid; pid++)
		AddChannelPid(*pid);

	if (m_vpid)
		m_frameDetector.SetPid(m_vpid, channel->Vtype());

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
 * Add a pid of the channel, if it isn't added already
 *
 * @param pid       pid to receive (0: none)
 */
void cZapReceiver::AddChannelPid(int pid)
{
	if (!pid || HasPid(pid))
		return;

	AddPid(pid);
	m_pids.push_back(pid);
}

/**
 * Check, if a pid of the channel is received
 *
 * @param pid       pid of a TS packet
 */
bool cZapReceiver::HasPid(int pid) const
{
	return std::find(m_pids.begin(), m_pids.end(), pid) != m_pids.end();
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
 * The frame detector needs some packets ahead to determine the type of a frame,
 * so the packets wait in m_pending, until they are analyzed. Then they are put
 * into the buffer, which starts with the last I-frame.
 *
 * @param data      TS packets
 * @param length    length of the data (a multiple of TS_SIZE)
 */
void cZapReceiver::Receive(const uchar *data, int length)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_taken || !m_vpid)
		return;

	m_pending.insert(m_pending.end(), data, data + length);

	size_t pos = 0;
	while (m_pending.size() - pos >= MIN_TS_PACKETS_FOR_FRAME_DETECTOR * TS_SIZE) {
#if APIVERSNUM >= 30011
		int n = m_frameDetector.Analyze(&m_pending[pos], m_pending.size() - pos, false);
		bool previousErrors;
		bool missingFrames;
		bool frameStart = m_frameDetector.NewFrame(previousErrors, missingFrames);
#else
		int n = m_frameDetector.Analyze(&m_pending[pos], m_pending.size() - pos);
		bool frameStart = m_frameDetector.NewFrame();
#endif
		if (n <= 0)
			break;

		// lost the TS packet borders, start again with the next I-frame
		if (n % TS_SIZE) {
			m_buffer.Reset();
			pos = m_pending.size();
			break;
		}

		// a new frame starts in the first analyzed packet
		bool independentFrame = frameStart && m_frameDetector.IndependentFrame();
		for (int i = 0; i < n; i += TS_SIZE) {
			const uchar *packet = &m_pending[pos + i];
			bool videoPesStart = TsPid(packet) == m_vpid && TsPayloadStart(packet);
			m_buffer.Put(packet, videoPesStart, !i && frameStart, !i && independentFrame);
		}

		pos += n;
	}

	m_pending.erase(m_pending.begin(), m_pending.begin() + pos);
}

/**
 * Take the buffered packets, which precede a given packet, and stop buffering
 *
 * Must be called by the receiver thread of the device (see cZapCache::Splice()),
 * so the receiver doesn't get a packet meanwhile.
 *
 * @param next          TS packet, which is played next
 * @param[out] frames   number of the buffered frames
 *
 * @return              the packets from the last I-frame up to the next packet
 */
std::vector<uint8_t> cZapReceiver::Take(const uint8_t *next, int *frames)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_taken = true;

	// the packets, which wait for the frame detector, belong to the last frame
	for (size_t pos = 0; pos + TS_SIZE <= m_pending.size(); pos += TS_SIZE)
		m_buffer.Put(&m_pending[pos], false, false, false);
	m_pending.clear();

	*frames = m_buffer.Frames();

	return m_buffer.Take(next);
}

/*****************************************************************************
 * cZapCache class
 ****************************************************************************/

cZapCache::~cZapCache(void)
{
	Clear();
}

/**
 * Report the start of a channel switch of the live view
 *
 * Arms the splice, if there is a receiver: the first packet of the new channel
 * decides, if the switch starts with its buffered packets.
 */
void cZapCache::ChannelSwitchStarted(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_armedFromChannel = cDevice::CurrentChannel();
	m_spliceArmed = m_pReceiver != nullptr;
	m_spliceTimer.Set(SPLICE_TIMEOUT_MS);
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

	// a switch to the same channel: Splice() can't tell its packets from the ones before
	if (channelNum == m_armedFromChannel)
		m_spliceArmed = false;

	// the new channel isn't buffered
	if (!m_pReceiver || m_pReceiver->ChannelNumber() != channelNum)
		m_spliceArmed = false;
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
			m_spliceArmed = false;
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

		// the receiver of the new live channel is needed, until the transfer plays its first packet
		if (m_spliceArmed && !m_spliceTimer.TimedOut())
			return;

		m_preparePending = false;
		m_spliceArmed = false;
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

/**
 * Start a channel switch with the buffered packets of the new channel
 *
 * Called by cOutputDevice::PlayTs() with every packet, while the splice is
 * armed. The first packet of the new channel, which the zap receiver gets,
 * too, decides:
 *
 * - The transfer of the new channel runs on the device of the zap receiver:
 *   the packets since the last I-frame, which precede the packet, are played
 *   first. The transfer receiver and the zap receiver get the same packets
 *   from the receiver thread of the device one after another, so the played
 *   stream has no gap and no duplicate packet.
 * - Otherwise the switch starts normally.
 *
 * The PAT and PMT of the transfer are played before (cTransfer::Activate()), the
 * packets of the old channel may still be played after the start of the switch.
 *
 * @param data      TS packet, which the transfer plays next
 * @param play      plays a TS packet (cDevice::PlayTs())
 */
void cZapCache::Splice(const uchar *data, const std::function<int(const uchar *, int)> &play)
{
	std::vector<uint8_t> packets;
	int frames = 0;
	int channelNum;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!m_spliceArmed)
			return;

		if (!m_pReceiver) {
			m_spliceArmed = false;
			return;
		}

		// the PAT and PMT of the transfer
		if (!m_pReceiver->HasPid(TsPid(data)))
			return;

		// a packet of the old channel
		int currentChannel = cDevice::CurrentChannel();
		if (currentChannel == m_armedFromChannel)
			return;

		m_spliceArmed = false;
		channelNum = m_pReceiver->ChannelNumber();
		if (currentChannel != channelNum)
			return;

		cDevice *device = m_pReceiver->Device();
		if (!device || device != cTransferControl::ReceiverDevice()) {
			LOGDEBUG("zapcache: the transfer of channel %d doesn't run on the device of the zap receiver", channelNum);
			return;
		}

		packets = m_pReceiver->Take(data, &frames);
	}

	if (packets.empty()) {
		LOGDEBUG("zapcache: no I-frame of channel %d buffered", channelNum);
		return;
	}

	for (size_t pos = 0; pos < packets.size(); pos += TS_SIZE) {
		int retries = PLAY_RETRIES;
		while (play(&packets[pos], TS_SIZE) <= 0) {
			if (!--retries) {
				LOGWARNING("zapcache: channel %d: buffered packet not accepted, %zu of %zu packets played", channelNum, pos / TS_SIZE, packets.size() / TS_SIZE);
				return;
			}
			cCondWait::SleepMs(PLAY_RETRY_WAIT_MS);
		}
	}

	LOGDEBUG("zapcache: channel %d started with %d buffered frames (%zu packets)", channelNum, frames, packets.size() / TS_SIZE);
}
