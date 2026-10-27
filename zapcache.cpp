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
#include <ctime>

#include <vdr/channels.h>
#include <vdr/config.h>
#include <vdr/device.h>
#include <vdr/remote.h>
#include <vdr/skins.h>
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
 * @param channel    channel to receive
 * @param device     device, which receives the channel
 */
cZapReceiver::cZapReceiver(const sZapChannel &channel, cDevice *device)
	: cReceiver(NULL, MINPRIORITY),
	  m_channelNumber(channel.number),
	  m_pDevice(device),
	  m_vpid(channel.vpid)
{
	AddChannelPid(channel.vpid);
	AddChannelPid(channel.ppid);
	for (int pid : channel.apids) {
		AddChannelPid(pid);
		m_audioPids.push_back(pid);
	}
	for (int pid : channel.dpids) {
		AddChannelPid(pid);
		m_audioPids.push_back(pid);
	}
	for (int pid : channel.spids)
		AddChannelPid(pid);

	if (m_vpid)
		m_frameDetector.SetPid(m_vpid, channel.vtype);

	LOGDEBUG("zapcache: receive channel %d (%s) on device %d", m_channelNumber, channel.name.c_str(), device->DeviceNumber() + 1);
}

/**
 * Copy the data of a channel (the caller holds the channels lock)
 */
sZapChannel sZapChannel::FromChannel(const cChannel *channel)
{
	sZapChannel c;

	c.number = channel->Number();
	c.name = channel->Name();
	c.vpid = channel->Vpid();
	c.vtype = channel->Vtype();
	c.ppid = channel->Ppid();
	for (const int *pid = channel->Apids(); *pid; pid++)
		c.apids.push_back(*pid);
	for (const int *pid = channel->Dpids(); *pid; pid++)
		c.dpids.push_back(*pid);
	for (const int *pid = channel->Spids(); *pid; pid++)
		c.spids.push_back(*pid);

	return c;
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
 * Get the state of the buffer for the statistics
 *
 * @param[out] frames   number of the buffered frames
 * @param[out] ready    true, if an I-frame is buffered
 */
void cZapReceiver::Info(int *frames, bool *ready)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	*frames = m_buffer.Frames();
	*ready = m_buffer.Ready();
}

/**
 * Check, if the buffer was taken by a channel switch (the receiver doesn't buffer anymore)
 */
bool cZapReceiver::IsTaken(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	return m_taken;
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
			int flags = PesStartFlags(packet);
			if (!i && frameStart)
				flags |= cZapBuffer::FRAME_START;
			if (!i && independentFrame)
				flags |= cZapBuffer::INDEPENDENT_FRAME;
			m_buffer.Put(packet, flags);
		}

		pos += n;
	}

	m_pending.erase(m_pending.begin(), m_pending.begin() + pos);
}

/**
 * Get the PES start flags of a TS packet for the buffer
 *
 * @param packet    TS packet of the channel
 *
 * @return          cZapBuffer::VIDEO_PES_START, cZapBuffer::AUDIO_PES_START or 0
 */
int cZapReceiver::PesStartFlags(const uchar *packet) const
{
	if (!TsPayloadStart(packet))
		return 0;

	int pid = TsPid(packet);
	if (pid == m_vpid)
		return cZapBuffer::VIDEO_PES_START;
	if (std::find(m_audioPids.begin(), m_audioPids.end(), pid) != m_audioPids.end())
		return cZapBuffer::AUDIO_PES_START;

	return 0;
}

/**
 * Take the buffered packets, which precede a given packet, and stop buffering
 *
 * Must be called by the receiver thread of the device (see cZapCache::Splice()),
 * so the receiver doesn't get a packet meanwhile.
 *
 * @param next          TS packet, which is played next
 * @param audioAhead    see cZapBuffer::Take()
 * @param[out] frames   number of the taken frames
 * @param[out] synced   true, if the packets start with synchronous audio
 *
 * @return              the packets from an I-frame up to the next packet
 */
std::vector<uint8_t> cZapReceiver::Take(const uint8_t *next, int64_t audioAhead, int *frames, bool *synced)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_taken = true;

	// The packets, which wait for the frame detector, belong to the last frame. Their
	// frames aren't detected, but their PES starts update the pts of the newest video
	// and audio (for the I-frame with synchronous audio).
	for (size_t pos = 0; pos + TS_SIZE <= m_pending.size(); pos += TS_SIZE)
		m_buffer.Put(&m_pending[pos], PesStartFlags(&m_pending[pos]));
	m_pending.clear();

	return m_buffer.Take(next, audioAhead, frames, synced);
}

/*****************************************************************************
 * cZapCache class
 ****************************************************************************/

cZapCache::cZapCache(void)
	: m_lastSwitch(time(NULL))
{
}

cZapCache::~cZapCache(void)
{
	Clear();
}

/**
 * Get the time without key press, after which the receivers are released
 *
 * @param release   release time (drmdevice.ZapAheadRelease): 0 = never, 1 = 20s,
 *                  2 = 50s (before VDR's EPG scan starts), 3 = 5min, 4 = 30min
 *
 * @return          the time in seconds (0: never)
 */
int cZapCache::ReleaseSeconds(int release)
{
	static const int seconds[RELEASE_TIMES] = { 0, 20, 50, 5 * 60, 30 * 60 };

	return seconds[std::clamp(release, 0, RELEASE_TIMES - 1)];
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
	m_spliceArmed = HasReceivers();
	m_spliceTimer.Set(SPLICE_TIMEOUT_MS);

	// statistics
	m_measuring = m_mode != ZAP_AHEAD_OFF;
	if (m_measuring)
		m_switches++;
	m_switchCached = false;
	m_pictureReported = false;
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
	m_lastSwitch = time(NULL);
	m_preparePending = true;
	m_prepareTimer.Set(PREPARE_DELAY_MS);

	// a switch to the same channel: Splice() can't tell its packets from the ones before
	if (channelNum == m_armedFromChannel)
		m_spliceArmed = false;

	// the new channel isn't buffered
	if (!ReceiverFor(channelNum))
		m_spliceArmed = false;
}

/**
 * Check, if a channel is received ahead (the caller holds the mutex)
 */
bool cZapCache::HasReceivers(void) const
{
	for (cZapReceiver *receiver : m_pReceivers) {
		if (receiver)
			return true;
	}

	return m_pLive != nullptr;
}

/**
 * Get the receiver of a channel (the caller holds the mutex)
 *
 * @param channelNum     number of the channel
 *
 * @return               the receiver (nullptr, if the channel isn't received ahead)
 */
cZapReceiver *cZapCache::ReceiverFor(int channelNum) const
{
	for (cZapReceiver *receiver : m_pReceivers) {
		if (receiver && receiver->ChannelNumber() == channelNum)
			return receiver;
	}

	if (m_pLive && m_pLive->ChannelNumber() == channelNum)
		return m_pLive;

	return nullptr;
}

/**
 * Stop the receivers of the neighbour channels
 */
void cZapCache::Clear(void)
{
	cZapReceiver *receivers[MAX_RECEIVERS + 1];
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (int i = 0; i < MAX_RECEIVERS; i++) {
			receivers[i] = m_pReceivers[i];
			m_pReceivers[i] = nullptr;
		}
		receivers[MAX_RECEIVERS] = m_pLive;
		m_pLive = nullptr;
	}

	// detached outside the mutex, cDevice::Detach() waits for the receiver thread of the device
	for (cZapReceiver *receiver : receivers)
		delete receiver;
}

/**
 * Tune the neighbour channels after a channel switch
 *
 * Must be called by the main thread (cPlugin::MainThreadHook()).
 *
 * @param mode          channels to tune (enum Mode, ZAP_AHEAD_OFF, if the option is
 *                      off or the live view isn't in transfer mode)
 * @param releaseSeconds  release the receivers after this time without key press
 *                      or channel switch (0: never)
 */
void cZapCache::MainThreadHook(int mode, int releaseSeconds)
{
	m_mode = mode;

	if (mode == ZAP_AHEAD_OFF) {
		bool hasReceivers;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_preparePending = false;
			m_spliceArmed = false;
			hasReceivers = HasReceivers();
		}
		if (hasReceivers)
			Clear();
		m_released = false;
		return;
	}

	// release the devices, so VDR's EPG scan can use them
	if (releaseSeconds) {
		time_t lastActivity;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			lastActivity = std::max(cRemote::LastActivity(), m_lastSwitch);
		}
		if (time(NULL) - lastActivity >= releaseSeconds) {
			if (!m_released) {
				bool hasReceivers;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_preparePending = false;
					m_spliceArmed = false;
					hasReceivers = HasReceivers();
				}
				if (hasReceivers) {
					Clear();
					LOGDEBUG("zapcache: released the devices after %ds without key press", releaseSeconds);
				}
				m_released = true;
			}
			return;
		}
	}

	// a key was pressed after the release, tune the channels again, but not
	// for the keys of a menu (the next channel switch would tune them anyway)
	if (m_released) {
		if (dynamic_cast<cSkinDisplayMenu *>(cSkinDisplay::Current()))
			return;
		m_released = false;
		std::lock_guard<std::mutex> lock(m_mutex);
		m_preparePending = true;
		m_prepareTimer.Set(0);
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

	Prepare(liveChannel, previousChannel, mode);
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
 * Get a device for a channel, which is tuned to its transponder or free
 *
 * A free device has no receiver and may be tuned without disturbing the live
 * view (like the devices of VDR's EPG scan).
 *
 * @param channel       channel to receive
 * @param avoid         device, which is only taken, if no other device is free
 *                      (it is tuned to the channel in the opposite direction)
 *
 * @return              the device (nullptr, if there is none)
 */
cDevice *cZapCache::FreeDevice(const cChannel *channel, const cDevice *avoid)
{
	if (cDevice *device = TunedDevice(channel))
		return device;

	cDevice *fallback = nullptr;
	for (int i = 0; i < cDevice::NumDevices(); i++) {
		cDevice *device = cDevice::GetDevice(i);
		if (device && device->ProvidesTransponder(channel) && device->MaySwitchTransponder(channel)) {
			if (device != avoid)
				return device;
			fallback = device;
		}
	}

	return fallback;
}

/**
 * Get a device, which is tuned to the transponder of a channel
 *
 * @param channel       channel to receive
 *
 * @return              the device (nullptr, if there is none)
 */
cDevice *cZapCache::TunedDevice(const cChannel *channel)
{
	for (int i = 0; i < cDevice::NumDevices(); i++) {
		cDevice *device = cDevice::GetDevice(i);
		if (device && device->IsTunedToTransponder(channel))
			return device;
	}

	return nullptr;
}

/**
 * Get the number of the devices, which can receive channels (tuners)
 *
 * The devices are counted with every call, a plugin may add devices at runtime.
 */
int cZapCache::NumTuners(void)
{
	int numTuners = 0;

	for (int i = 0; i < cDevice::NumDevices(); i++) {
		cDevice *device = cDevice::GetDevice(i);
		if (device && device->NumProvidedSystems() > 0)
			numTuners++;
	}

	return numTuners;
}

/**
 * Tune a channel on a device and attach a zap receiver for it
 *
 * @param channel       channel to receive
 * @param device        device for the channel
 *
 * @return              the receiver (nullptr, if the channel can't be received)
 */
cZapReceiver *cZapCache::Attach(const cChannel *channel, cDevice *device)
{
	if (!device->IsTunedToTransponder(channel) && !device->SwitchChannel(channel, false)) {
		LOGDEBUG("zapcache: can't tune channel %d (%s) on device %d", channel->Number(), channel->Name(), device->DeviceNumber() + 1);
		return nullptr;
	}

	cZapReceiver *receiver = new cZapReceiver(sZapChannel::FromChannel(channel), device);
	if (!device->AttachReceiver(receiver)) {
		LOGDEBUG("zapcache: can't attach the receiver for channel %d to device %d", channel->Number(), device->DeviceNumber() + 1);
		delete receiver;
		return nullptr;
	}

	return receiver;
}

/**
 * Keep the receiver of a channel, if it still buffers it
 *
 * @param receivers     the old receivers, the kept one is removed from them
 * @param count         number of the old receivers
 * @param channel       channel, which needs a receiver (may be nullptr)
 *
 * @return              the kept receiver (nullptr, if there is none)
 */
cZapReceiver *cZapCache::Keep(cZapReceiver **receivers, int count, const cChannel *channel)
{
	if (!channel)
		return nullptr;

	for (int i = 0; i < count; i++) {
		cZapReceiver *receiver = receivers[i];
		if (receiver && receiver->ChannelNumber() == channel->Number() && receiver->Device() && !receiver->IsTaken()) {
			receivers[i] = nullptr;
			LOGDEBUG("zapcache: keep the receiver of channel %d (%s)", channel->Number(), channel->Name());
			return receiver;
		}
	}

	return nullptr;
}

/**
 * Tune the neighbour channels of the live channel
 *
 * The receivers of channels, which are still neighbours, are kept with their
 * buffers. The other ones are stopped, before the missing channels are tuned,
 * so their devices are free again.
 *
 * In ZAP_AHEAD_BOTH mode the live channel gets a receiver, too, on the device of
 * its transfer. VDR stops the transfer, before it reports a channel switch, so
 * only this receiver keeps the stream of the last live channel and its buffer.
 * It becomes the receiver of the channel in the opposite direction.
 *
 * @param liveChannel       current live channel
 * @param previousChannel   live channel before the last switch
 * @param mode              channels to tune (enum Mode)
 */
void cZapCache::Prepare(int liveChannel, int previousChannel, int mode)
{
	cZapReceiver *old[MAX_RECEIVERS + 1];
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (int i = 0; i < MAX_RECEIVERS; i++) {
			old[i] = m_pReceivers[i];
			m_pReceivers[i] = nullptr;
		}
		old[MAX_RECEIVERS] = m_pLive;
		m_pLive = nullptr;
	}

	cZapReceiver *receivers[MAX_RECEIVERS] = {};
	cZapReceiver *liveReceiver = nullptr;

	{
		LOCK_CHANNELS_READ;
		const cChannel *live = Channels->GetByNumber(liveChannel);
		const cChannel *next = nullptr;
		const cChannel *opposite = nullptr;

		if (live) {
			// a switch to the next or previous channel sets the zap direction, a switch
			// to another channel and the first switch after VDR's start zap up
			const cChannel *previous = Channels->GetByNumber(previousChannel);
			if (previous && Neighbour(Channels, previous, -1) == live)
				m_direction = -1;
			else
				m_direction = 1;

			next = Neighbour(Channels, live, m_direction);
			if (next && !next->Vpid() && !next->Apid(0) && !next->Dpid(0))
				next = nullptr;

			opposite = mode == ZAP_AHEAD_BOTH ? Neighbour(Channels, live, -m_direction) : nullptr;
			if (opposite == next || (opposite && !opposite->Vpid() && !opposite->Apid(0) && !opposite->Dpid(0)))
				opposite = nullptr;

			receivers[0] = Keep(old, MAX_RECEIVERS + 1, next);
			receivers[1] = Keep(old, MAX_RECEIVERS + 1, opposite);
			if (mode == ZAP_AHEAD_BOTH)
				liveReceiver = Keep(old, MAX_RECEIVERS + 1, live);
		}

		// the other receivers aren't needed anymore, their devices may be needed below
		for (cZapReceiver *receiver : old)
			delete receiver;

		// The device, which is tuned to the channel in the opposite direction (usually
		// the device of the last live channel), is only tuned to the next channel, if
		// no other device is free. So a switch back doesn't have to tune.
		if (next && !receivers[0]) {
			const cChannel *back = Neighbour(Channels, live, -m_direction);
			if (cDevice *device = FreeDevice(next, back && back != next ? TunedDevice(back) : nullptr))
				receivers[0] = Attach(next, device);
			else
				LOGDEBUG("zapcache: no free device for channel %d (%s)", next->Number(), next->Name());
		}

		// the channel in the opposite direction preferably on a device, which is tuned to it already
		if (opposite && !receivers[1]) {
			if (cDevice *device = FreeDevice(opposite, nullptr))
				receivers[1] = Attach(opposite, device);
			else
				LOGDEBUG("zapcache: no free device for channel %d (%s)", opposite->Number(), opposite->Name());
		}

		// the live channel on the device of its transfer
		if (mode == ZAP_AHEAD_BOTH && live && !liveReceiver) {
			cDevice *device = cTransferControl::ReceiverDevice();
			if (device && device->IsTunedToTransponder(live))
				liveReceiver = Attach(live, device);
		}
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	for (int i = 0; i < MAX_RECEIVERS; i++)
		m_pReceivers[i] = receivers[i];
	m_pLive = liveReceiver;
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
 * @param data          TS packet, which the transfer plays next
 * @param audioAhead    start with an I-frame, whose synchronous audio is buffered
 *                      this much (in 90kHz ticks) ahead (cZapBuffer::NEWEST_I_FRAME:
 *                      start with the newest I-frame)
 * @param play          plays a TS packet (cDevice::PlayTs())
 */
void cZapCache::Splice(const uchar *data, int64_t audioAhead, const std::function<int(const uchar *, int)> &play)
{
	std::vector<uint8_t> packets;
	int frames = 0;
	bool synced = false;
	int channelNum;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (!m_spliceArmed)
			return;

		// a packet of the old channel
		int currentChannel = cDevice::CurrentChannel();
		if (currentChannel == m_armedFromChannel)
			return;

		cZapReceiver *receiver = ReceiverFor(currentChannel);
		if (!receiver) {
			m_spliceArmed = false;
			return;
		}

		// the PAT and PMT of the transfer
		if (!receiver->HasPid(TsPid(data)))
			return;

		m_spliceArmed = false;
		channelNum = currentChannel;

		cDevice *device = receiver->Device();
		if (!device || device != cTransferControl::ReceiverDevice()) {
			LOGDEBUG("zapcache: the transfer of channel %d doesn't run on the device of the zap receiver", channelNum);
			return;
		}

		packets = receiver->Take(data, audioAhead, &frames, &synced);
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

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_switchCached = true;
		m_cached++;
		if (synced)
			m_synced++;
	}

	LOGDEBUG("zapcache: channel %d started with %d buffered frames (%zu packets)%s", channelNum, frames, packets.size() / TS_SIZE,
		audioAhead == cZapBuffer::NEWEST_I_FRAME ? "" : synced ? " and synchronous audio" : ", the audio isn't buffered yet");
}

/**
 * Report the time of a channel switch for the statistics
 *
 * Called by the renderer for the first picture (in the fast channel switch modes)
 * and for the start of the playback. Without a fast channel switch mode, the
 * playback starts with the first picture.
 *
 * @param start         false: the first picture was shown, true: the playback started
 * @param ms            time since the start of the channel switch in ms
 */
void cZapCache::ReportSwitchTime(bool start, int ms)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (!m_measuring)
		return;

	int cached = m_switchCached ? 1 : 0;

	if (!start || !m_pictureReported) {
		m_pictureSumMs[cached] += ms;
		m_pictureCount[cached]++;
		m_pictureReported = true;
	}

	if (start) {
		m_startSumMs[cached] += ms;
		m_startCount[cached]++;
		m_measuring = false;
	}
}

/**
 * Reset the statistics of the zap cache (the current switch isn't measured anymore)
 */
void cZapCache::ResetStats(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_switches = 0;
	m_cached = 0;
	m_synced = 0;
	m_measuring = false;
	for (int i = 0; i < 2; i++) {
		m_pictureSumMs[i] = 0;
		m_pictureCount[i] = 0;
		m_startSumMs[i] = 0;
		m_startCount[i] = 0;
	}
}

/**
 * Get the statistics of the zap cache
 *
 * @param[out] stats    the statistics
 */
void cZapCache::GetStats(sStats *stats)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	stats->receivers.clear();
	for (cZapReceiver *receiver : m_pReceivers) {
		if (!receiver)
			continue;
		cDevice *device = receiver->Device();
		sStats::sReceiver r = { receiver->ChannelNumber(), device ? device->DeviceNumber() + 1 : 0, 0, false };
		receiver->Info(&r.frames, &r.ready);
		stats->receivers.push_back(r);
	}

	stats->switches = m_switches;
	stats->cached = m_cached;
	stats->synced = m_synced;
	for (int i = 0; i < 2; i++) {
		stats->pictureMs[i] = m_pictureCount[i] ? m_pictureSumMs[i] / m_pictureCount[i] : 0;
		stats->startMs[i] = m_startCount[i] ? m_startSumMs[i] / m_startCount[i] : 0;
	}
}
