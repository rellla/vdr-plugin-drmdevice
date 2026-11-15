// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file devicepip.cpp
 * Output Device: Picture in Picture
 *
 * This file defines the pip control of cOutputDevice.
 *
 * @copyright 2011 - 2015 by Johns.  All Rights Reserved.
 * @copyright 2018 - 2019 by zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <mutex>

#include "device.h"
#include "logger.h"
#include "pip.h"
#include "videorender.h"
#include "videostream.h"

/*********************************************************************
 * cOutputDevice public API - pip
 ********************************************************************/

/**
 * Play a video packet of the pip videostream
 *
 * @param data    A complete PES packet with optionally fragmented payload
 * @param size    the length of the PES packet including header
 *
 * The caller must ensure, that PlayPipVideo() is not called in detached state.
 * (CanReplay() and HasDecoder() return false in this state and we are not
 * the primary device.)
 */
int cOutputDevice::PlayPipVideo(const uchar *data, int size)
{
//	LOGDEBUG("device: %s: %p %d", __FUNCTION__, data, size);

	// This is a bit hacky:
	// Because we do no sync with the pip stream, we simply drop data
	// if the input buffer is full -> this prevents us from having a buffer overflow
	// caused by the pip stream.
	if (m_pPipStream->IsInputBufferFull())
		return size;

	return PlayVideoInternal(m_pPipStream, &m_pipReassemblyBuffer, data, size, false, false);
}

/**
 * Resets pip stream and render pipeline
 */
void cOutputDevice::ResetPipStream(void)
{
	auto lock = m_pStateMachine->Lock();

	ResetPipStreamLocked();
}

/**
 * Resets pip stream and render pipeline, if the state machine isn't locked
 *
 * This is used by the pip receiver, which runs in VDR's receiver thread. While
 * the receiver thread is in cPipReceiver::Receive(), VDR holds its receiver mutex.
 * The state machine detaches the pip receiver on a detach, which needs this mutex.
 * So the receiver thread must not wait for the state machine.
 *
 * @retval true     the pip stream was reset
 * @retval false    the state machine is locked (e.g. a state change is in progress)
 */
bool cOutputDevice::TryResetPipStream(void)
{
	std::unique_lock<std::mutex> lock = m_pStateMachine->TryLock();
	if (!lock.owns_lock())
		return false;

	ResetPipStreamLocked();

	return true;
}

/**
 * Resets pip stream and render pipeline, the caller has to lock the state machine
 */
void cOutputDevice::ResetPipStreamLocked(void)
{
	if (!m_pPipStream)   // detached
		return;

	m_pPipStream->Halt();

	m_pPipStream->CancelFilterThread();
	m_pipReassemblyBuffer.Reset();
	m_pPipStream->ClearVdrCoreToDecoderQueue();
	m_pPipStream->CloseDecoder();

	m_pRender->Halt();

	m_pRender->ClearPipDecoderToDisplayQueue();
	m_pRender->ResetPipDecodingStrategy();
	m_pRender->ResetPipBufferReuseStrategy();

	m_pRender->Resume();
	m_pPipStream->Resume();
}

/**
 * Returns true, if pip is currently enabled
 */
bool cOutputDevice::PipIsEnabled(void)
{
	auto lock = m_pStateMachine->Lock();

	return m_pPipHandler->IsEnabled();
}

/** Enable pip (see cPipHandler::Enable()) */
void cOutputDevice::PipEnable(void) { m_pPipHandler->Enable(); };
/** Show the given channel in the pip window (see cPipHandler::EnableChannel()) */
bool cOutputDevice::PipEnableChannel(int channelNum) { return m_pPipHandler->EnableChannel(channelNum); };
/** Disable pip (see cPipHandler::Disable()) */
void cOutputDevice::PipDisable(void) { m_pPipHandler->Disable(); };
/** Toggle pip (see cPipHandler::Toggle()) */
void cOutputDevice::PipToggle(void) { m_pPipHandler->Toggle(); };
/** Switch the pip channel up or down (see cPipHandler::ChannelChange()) */
void cOutputDevice::PipChannelChange(int dir) { m_pPipHandler->ChannelChange(dir); };
/** Swap the pip and the main channel (see cPipHandler::ChannelSwap()) */
void cOutputDevice::PipChannelSwap(bool closePip) { m_pPipHandler->ChannelSwap(closePip); };
/** Swap the pip position (see cPipHandler::SwapPosition()) */
void cOutputDevice::PipSwapPosition(void) { m_pPipHandler->SwapPosition(); };
/** Apply the configured pip size (see cPipHandler::SetSize()) */
void cOutputDevice::PipSetSize(void) { m_pPipHandler->SetSize(); };

/*
 * Wrapper functions for cVideoRender and cPipHandler
 */
/** Apply the pip size and position of the config to the renderer (attached state only) */
void cOutputDevice::SetRenderPipSize(void)
{
	auto lock = m_pStateMachine->Lock();

	if (!m_pRender)   // detached
		return;

	m_pRender->Halt();
	m_pRender->SetPipSize(m_pipUseAlt);
	m_pRender->Resume();
};

/** Show or hide the pip window in the renderer (attached state only) */
void cOutputDevice::SetRenderPipActive(bool active)
{
	auto lock = m_pStateMachine->Lock();

	if (!m_pRender)   // detached
		return;

	m_pRender->Halt();
	m_pRender->SetPipActive(active);
	m_pRender->Resume();
};
