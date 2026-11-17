// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file devicestate.cpp
 * Output Device: State Transitions
 *
 * This file defines the actions of cOutputDevice, which the state machine
 * triggers.
 *
 * @copyright 2011 - 2015 by Johns.  All Rights Reserved.
 * @copyright 2018 - 2019 by zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "audio.h"
#include "codec_audio.h"
#include "device.h"
#include "ambient.h"
#include "grab.h"
#include "hardwaredevice.h"
#include "logger.h"
#include "osdprovider.h"
#include "pip.h"
#include "videorender.h"
#include "videostream.h"

/*********************************************************************
 * cOutputDevice state transitioning functions
 ********************************************************************/

/**
 * With this wrapper function, the device can directly act
 * as an event reveiver.
 *
 * @param event           event to be executed
 */
void cOutputDevice::TriggerEvent(const Event &event)
{
	m_pStateMachine->OnEventReceived(event);
}

/**
 * Stop pip and detach the device
 *
 * The pip handler stays locked until the device is detached, so another
 * thread (e.g. an SVDRP command) can't start pip in between.
 */
void cOutputDevice::TriggerDetachEvent(void)
{
	m_pPipHandler->Detach([this] { TriggerEvent(DetachEvent{}); });
}

/**
 * Actions to be performed when leaving a state
 *
 * These are only executed when the state actually changes.
 * E.g. a state transition PLAY -> PLAY does not trigger this.
 *
 * @param state         state being left
 */
void cOutputDevice::LeaveState(State state)
{
	switch (state) {
		case PLAY:
			m_pRender->SchedulePlaybackStartAtPtsMs(AV_NOPTS_VALUE);
			m_pRender->SetPlaybackPaused(true);
			m_pAudio->SetPaused(true);
			m_pAudio->ResetHwDelayBaseline();
			break;
		case BUFFERING:
			m_pAudio->SetHwDelayBaseline();
			m_pRender->SetDisplayOneFrameThenPause(false);
			break;
		case TRICK_SPEED:
			// The filter thread needs to be restarted for interlaced streams to be rendered with deinterlacer again. It is started lazily.
			m_pVideoStream->CancelFilterThread();
			m_pRender->SetTrickSpeed(0, false, false);
			m_pAudio->SetTrickSpeed(0, false, false);
			m_pRender->ResetFrameCounter();
			m_pRender->ClearDecoderToDisplayQueue();
			m_pVideoStream->ResetFilterThreadNeededCheck();
			m_pVideoStream->SetDeinterlacerDeactivated(false);
			m_pRender->SetPlaybackPaused(true);
			m_pRender->ResetBufferReuseStrategy();
			m_pVideoStream->ResetTrickSpeedFramesSentCounter();
			break;
		case STOP:
			m_receivedAudio = false;
			m_receivedVideo = false;
			m_receivedValidAudio = false;
			m_receivedValidVideo = false;
			break;
		case DETACHED:
			m_pAudio = new cAudioStream(this);
			m_pAudio->SetPassthroughMask(GetPassthroughSetting());  // the passthrough may have been toggled
			m_pRender = new cVideoRender(this);
			m_pGrab = new cGrabber(m_pRender);
			m_pVideoStream = new cMainVideoStream(m_pRender, m_pHardwareDevice->GetQuirks(), m_pRender->GetMainOutputBuffer(), m_pConfig, std::bind(&cVideoRender::PushMainFrame, m_pRender, std::placeholders::_1));
			m_pAudioDecoder = new cAudioDecoder(m_pAudio);
			int err = m_pRender->Init(); // starts display thread
			if (err < 0) { // drm init failed -> force detached mode
				DeleteDecoderAndOutput();
				LOGERROR("device: display init failed -> start in detached mode");
				m_forceDetached = true;
				break;
			}
			m_pVideoStream->StartDecoder(); // starts decoding thread
			m_pPipStream = new cPipVideoStream(m_pRender, m_pHardwareDevice->GetQuirks(), m_pRender->GetPipOutputBuffer(), m_pConfig, std::bind(&cVideoRender::PushPipFrame, m_pRender, std::placeholders::_1));
			m_pPipStream->StartDecoder(); // starts decoding thread
			// Audio is init lazily (includes starting thread)
			m_pAmbient->SetDetached(false);

			break;
	}
}

/**
 * Actions to be performed when entering a state
 *
 * These are only executed when the state actually changes.
 * E.g. a state transition PLAY -> PLAY does not trigger this.
 *
 * @param state         state being entered
 */
void cOutputDevice::EnterState(State state)
{
	switch (state) {
		case BUFFERING:
			m_pAudio->ResetHwDelayBaseline();
			if (FastChannelSwitchInTransferMode()) {
				m_pRender->SetPlaybackPaused(false);
				m_pRender->SetDisplayOneFrameThenPause(true);
			}
			break;
		case PLAY:
			// revert the video playback start release which was done in BUFFERING if we are audio-only
			if (m_playbackMode == AUDIO_ONLY && FastChannelSwitchInTransferMode()) {
				m_pRender->SetPlaybackPaused(true);
				m_pRender->SetDisplayOneFrameThenPause(false);
			}

			if (m_playbackMode != VIDEO_ONLY)
				m_pAudio->SetPaused(false);

			if (m_playbackMode != AUDIO_ONLY) {
				m_pVideoStream->SetDeinterlacerDeactivated(false);
				m_pRender->SetStillpicture(false);
				m_pRender->SetPlaybackPaused(false);
			}
			break;
		case TRICK_SPEED:
			// The filter thread needs to be restarted for interlaced streams
			// to be rendered without deinterlacer in trick speed mode.
			// It is started lazily.
			m_pVideoStream->CancelFilterThread();
			m_pRender->SetPlaybackPaused(false);
			m_pVideoStream->SetDeinterlacerDeactivated(true);
			m_pRender->ResetBufferReuseStrategy();
			m_pAudio->ResetTrickSpeedPts();
			break;
		case STOP:
			if (m_forceDetached) {
				m_pStateMachine->SetState(DETACHED);
				break;
			}

			FlushAudio();

			m_pVideoStream->CancelFilterThread();
			m_pRender->DisplayBlackFrame();
			m_pRender->Reset();
			m_playbackMode = NONE;

			m_videoReassemblyBuffer.Reset();
			ResetVideoDamage();
			m_pVideoStream->ClearVdrCoreToDecoderQueue();
			m_pRender->ClearDecoderToDisplayQueue();
			m_pRender->ResetDecodingStrategy();
			m_pRender->ResetBufferReuseStrategy();
			m_pVideoStream->CloseDecoder();
			m_audioJitterTracker.Reset();
			m_videoJitterTracker.Reset();

			break;
		case DETACHED:
			// resume the previously stopped threads
			m_pVideoStream->Resume();
			m_pRender->Resume();

			// now do the detach
			m_pPipStream->Exit();
			delete m_pPipStream;
			m_pPipStream = nullptr;

#ifdef USE_GLES
			// The opengl thread was probably locked before cmdCopyBufferToOutputFb().
			// 1) set running to false
			// 2) continue the thread (which will skip the waiting cmd->Execute())
			// 3) do the real thread cancel and cleanup
			// We need to keep this order to prevent a deadlock!
			// (There is no osd provider, if we are not the primary device.)
			if (m_pOsdProvider) {
				m_pOsdProvider->RequestStopOpenGlThread();
				m_pOsdProvider->UnlockOpenGlThread();
				m_pOsdProvider->StopOpenGlThread();
			}
#endif
			m_pRender->Exit(); // render must be stopped before videostream!
			m_pVideoStream->Exit();
			m_pAudio->Exit(); // audio must be stopped after renderer!

			DeleteDecoderAndOutput();
			m_pAmbient->SetDetached(true);

			break;
	}
}

/**
 * Delete the decoders and the output objects, which only exist in attached state
 *
 * The pointers are reset, so that functions which can be called in detached state
 * (e.g. from the setup menu) can test them.
 */
void cOutputDevice::DeleteDecoderAndOutput(void)
{
	delete m_pAudioDecoder; // includes a Close()
	m_pAudioDecoder = nullptr;
	delete m_pVideoStream;
	m_pVideoStream = nullptr;
	delete m_pGrab;
	m_pGrab = nullptr;
	delete m_pRender;
	m_pRender = nullptr;
	delete m_pAudio;
	m_pAudio = nullptr;
}

/**
 * Pause the rendering and decoder thread.
 */
void cOutputDevice::HaltVideoThreads(void)
{
	m_pRender->Halt();
	m_pVideoStream->Halt();
}

/**
 * Resume the rendering and decoder thread.
 */
void cOutputDevice::ResumeVideoThreads(void)
{
	m_pVideoStream->Resume();
	m_pRender->Resume();
}

#ifdef USE_GLES
/**
 * Pause the OpenGL worker thread.
 *
 * @return true, if the thread was paused (it must be resumed with ResumeOpenGlThread())
 */
bool cOutputDevice::HaltOpenGlThread(void)
{
	return m_pOsdProvider && m_pOsdProvider->LockOpenGlThread();
}

/**
 * Resume the OpenGL worker thread.
 */
void cOutputDevice::ResumeOpenGlThread(void)
{
	if (m_pOsdProvider)
		m_pOsdProvider->UnlockOpenGlThread();
}
#endif

/**
 * Returns true, the a detached plugin start was forced.
 */
bool cOutputDevice::IsDetachForced(void)
{
	return m_forceDetached;
}

/**
 * Init the audio lazily.
 *
 * @param setVolume           if true, reset the current volume
 *
 * @retval 0 if audio init succeeded
 * @retval -1 on error
 */
int cOutputDevice::InitAudio(bool setVolume)
{
	if (m_pAudio->LazyInit())
		return -1;

	if (setVolume)
		m_pAudio->SetVolume((m_volume * 1000) / 255);

	return 0;
}

/**
 * Reset the video deinterlace filter
 */
void cOutputDevice::ResetVideoFilter(void)
{
	m_pRender->ResetFrameCounter();
	m_pVideoStream->ResetFilterThreadNeededCheck();
}

/**
 * Set the trickspeed in the renderer
 *
 * @param speed           trickspeed factor
 * @param active          true, if trickspeed is active
 * @param forward         true, if this is forward trickspeed
 */
void cOutputDevice::SetTrickSpeed(double speed, bool active, bool forward)
{
	m_pRender->SetTrickSpeed(speed, active, forward);
	m_pAudio->SetTrickSpeed(speed, active, forward);
}

/**
 * Schedules the playback start
 *
 * Drops audio data if necessary to start in sync and schedules
 * the playback start at the common pts.
 *
 * @retval true                  playback started
 * @retval false                 playback was not started
 */
bool cOutputDevice::SchedulePlaybackStart(void)
{
	bool receivedAudio = m_pAudio->HasInputPts();
	bool receivedVideo = m_pVideoStream->HasInputPts();

	if (receivedAudio && receivedVideo) {
		m_playbackMode = AUDIO_AND_VIDEO;
		// store the first PTSes beforehand, because dropping samples/frames will change the output of GetFirst*PtsMsToPlay()
		int64_t firstAudioPtsMs = GetFirstAudioPtsMsToPlay();
		int64_t firstVideoPtsMs = GetFirstVideoPtsMsToPlay();

		// don't drop old audio, if we want to wait for it in fast channel switch mode including audio playback
		if (!FastChannelSwitchAudioInTransferMode())
			m_pAudio->DropSamplesOlderThanPtsMs(firstAudioPtsMs);

		// A switch, which started with the buffer of the zap cache, starts the video with the
		// heard audio, if the audio plays already (fast audio channel switch mode): the frames
		// before are skipped instead of being dropped by the A/V sync afterwards. Without the
		// buffer, no frame is skipped.
		bool startWithAudio = FastChannelSwitchAudioInTransferMode() && !m_pAudio->IsPaused() && m_zapCacheSwitch;
		m_pRender->SchedulePlaybackStartAtPtsMs(firstVideoPtsMs, startWithAudio);
	} else if (receivedAudio) {
		LOGDEBUG("device: audio only detected");
		m_playbackMode = AUDIO_ONLY;
		m_pAudio->DropSamplesOlderThanPtsMs(m_pAudio->GetOutputPtsMs());
	} else if (receivedVideo) {
		LOGDEBUG("device: video only detected");
		m_playbackMode = VIDEO_ONLY;
		m_pRender->SchedulePlaybackStartAtPtsMs(m_pRender->GetOutputPtsMs());
	} else {
		// Sometimes a DeviceClear() can jump in between signalling the ThresholdReachedEvent
		// and progressing it, e.g. the video thread signals the ThresholdReached, VDR sends a DeviceClear()
		// and OnEventReceived wants to process the ThresholdReachedEvent with empty buffers.
		LOGDEBUG("device: buffering threshold reached and no a/v available, keep BUFFERING state");
		return false;
	}

	return true;
}

/**
 * Schedule aa a/v resync in the render thread at the given pts
 *
 * @param pts            resync, when the rendered video frame reaches this pts
 */
void cOutputDevice::ScheduleResyncAtPtsMs(int64_t pts)
{
	m_pRender->ScheduleResyncAtPtsMs(pts);
}

/**
 * Resume playback from pause state
 *
 * Other than ResumePlayback() this also syncs a/v (by dropping audio)
 */
void cOutputDevice::ResumeFromPause(void)
{
	// resume from pause
	int audioBehindVideoByMs;

	switch (m_playbackMode) {
		case AUDIO_ONLY:
			m_pAudio->SetHwDelayBaseline();
			m_pAudio->SetPaused(false);
			break;
		case AUDIO_AND_VIDEO:
			audioBehindVideoByMs = m_pRender->GetOutputPtsMs() - m_pAudio->GetOutputPtsMs() - GetVideoAudioDelayMs();
			m_pAudio->SetHwDelayBaseline();
			if (audioBehindVideoByMs > 0) {
				m_pAudio->DropSamplesOlderThanPtsMs(m_pAudio->GetOutputPtsMs() + audioBehindVideoByMs);
				m_pAudio->SetPaused(false);
			} else
				m_pRender->SetScheduleAudioResume(true);

			// fallthrough
		case VIDEO_ONLY:
			m_pRender->SetStillpicture(false);
			m_pRender->SetPlaybackPaused(false);
			break;
		case NONE:
			LOGFATAL("STATE MACHINE: play event in PLAY state with NONE playback mode. This is a bug.");
			break;
	}
}

/**
 * Pause the playback
 *
 * @param schedulePause               if true, pause at the current audio pts
 *                                    otherwise pause immediately
 */
void cOutputDevice::PausePlayback(bool schedulePause)
{
	if (schedulePause && m_playbackMode == AUDIO_AND_VIDEO)
		m_pRender->ScheduleVideoPlaybackPauseAt(m_pAudio->GetOutputPtsMs() - GetVideoAudioDelayMs());
	else
		m_pRender->SetPlaybackPaused(true);

	m_pAudio->SetPaused(true);
	m_pAudio->ResetHwDelayBaseline();
}

/**
 * Resume playback again
 */
void cOutputDevice::ResumePlayback(void)
{
	m_pRender->SetPlaybackPaused(false);
}
