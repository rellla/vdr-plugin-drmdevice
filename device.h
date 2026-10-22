// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file device.h
 * Output Device Header File
 *
 * @copyright 2011 - 2015 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_OUTPUT_DEVICE_H
#define __DRMDEVICE_OUTPUT_DEVICE_H

#include <atomic>
#include <mutex>

extern "C"
{
#include <libavcodec/avcodec.h>
}

#include <vdr/device.h>
#include <vdr/osd.h>
#include <vdr/status.h>
#include <vdr/thread.h>

#include "config.h"
#include "hardwaredevice.h"
#include "jittertracker.h"
#include "pes.h"
#include "statemachine.h"

class cAudioDecoder;
class cAudioStream;
class cDeviceOsdProvider;
class cDvbSpuDecoder;
class cGrabber;
class cOutputDevice;
class cPipHandler;
class cPipReceiver;
class cSpuDecoder;
class cStateMachine;
class cVideoRender;
class cVideoStream;

/**
 * Output Device Implementation
 * @defgroup device Device
 */

/**
 * @addtogroup device
 * @{
 */

/**
 * Streams, which are currently played
 */
enum PlaybackMode {
	NONE,                  ///< nothing has been received yet
	AUDIO_AND_VIDEO,       ///< audio and video
	AUDIO_ONLY,            ///< audio only (e.g. radio)
	VIDEO_ONLY             ///< video only
};

/** @} */

/**
 * Output Device Implementation
 *
 * The VDR output device: receives the audio and video data from VDR, holds the
 * audio and video streams, the renderer, the osd provider and the pip handler
 * and controls them by the state machine (cStateMachine).
 *
 * The decoders, streams and the renderer only exist in attached state.
 *
 * @ingroup device
 */
class cOutputDevice : public cDevice, public cStatus {
public:
	cOutputDevice(cDeviceConfig *);
	virtual ~cOutputDevice(void);

	//
	// VDR cPlugin interface (wrapped by cPluginDrmDevice)
	//
	bool Initialize(void);
	int Start(void);
	void Stop(void);

	//
	// VDR cDevice interface
	//
protected:
	virtual void MakePrimaryDevice(bool);
public:
	virtual cString DeviceName(void) const { return "drmdevice"; } ///< name of the device
	virtual bool HasDecoder(void) const;

	// SPU facilities
	virtual cSpuDecoder * GetSpuDecoder(void);

	// Image grab facilities
	virtual uchar *GrabImage(int &, bool, int, int, int);

	// Video format facilities
	virtual void SetVideoDisplayFormat(eVideoDisplayFormat);
	virtual void SetVideoFormat(bool);
	virtual void GetVideoSize(int &, int &, double &);
	virtual void GetOsdSize(int &, int &, double &);

	// Audio facilities
protected:
	virtual void SetVolumeDevice(int);

	// Player facilities
	virtual bool CanReplay(void) const;
	virtual bool SetPlayMode(ePlayMode);
	virtual int PlayVideo(const uchar *, int);
	virtual int PlayAudio(const uchar *, int, uchar);
public:
	virtual int64_t GetSTC(void);
	virtual cRect CanScaleVideo(const cRect &, int);
	virtual void ScaleVideo(const cRect & = cRect::Null);
	virtual void TrickSpeed(int, bool);
	virtual void Clear(void);
	virtual void Play(void);
	virtual void Freeze(void);
	virtual void StillPicture(const uchar *, int);
	virtual bool Poll(cPoller &, int = 0);
	virtual bool Flush(int = 0);
#if APIVERSNUM >= 30014
	virtual bool DrainDevice(void);
#endif

	//
	// VDR cStatus interface
	//
protected:
	virtual void ChannelSwitch(const cDevice *, int, bool);
	virtual void Replaying(const cControl *, const char *, const char *, bool);

	//
	// cOutputDevice public API
	//
public:
	cDeviceConfig *Config(void) { return m_pConfig; }          ///< get the plugin config
	cVideoStream *VideoStream(void) { return m_pVideoStream; } ///< get the main video stream (nullptr, if detached)
	cVideoRender *Render(void) { return m_pRender; }           ///< get the renderer (nullptr, if detached)
	cAudioStream *Audio(void) { return m_pAudio; }             ///< get the audio stream (nullptr, if detached)

	// Playback, display and decoder
	void SetDisableDeint(void);
	void SetDecoderFallbackToSw(bool);
	void SetEnableHdr(bool);
	void SetChannelSwitchMode(ChannelSwitchMode mode) { m_channelSwitchMode = mode; }  ///< set the channel switch mode
	void SetDisplayMode(int);
	/** true, if audio starts before a/v sync in transfer mode */
	bool FastChannelSwitchAudioInTransferMode(void) { return m_channelSwitchMode == CHANNEL_SWITCH_FAST_AUDIO && Transferring(); }
	/** true, if a fast channel switch mode is used in transfer mode */
	bool FastChannelSwitchInTransferMode(void) { return m_channelSwitchMode != CHANNEL_SWITCH_AVSYNC && Transferring(); }
	bool CheckPlaybackStartConditions(void);
	bool CheckAudioPlaybackStartConditions(void);
	bool IsVideoOnlyPlayback(void) { return m_playbackMode == VIDEO_ONLY; }  ///< true, if only video is played
	bool IsAudioOnlyPlayback(void) { return m_playbackMode == AUDIO_ONLY; }  ///< true, if only audio is played

	// Zoom
	int GetZoom(void);
	int GetZoomLevel(void) const { return m_zoomLevel; }      ///< get the active zoom level (0 = off)
	bool SetZoomLevel(int);
	int NextZoomLevel(void);
	cString ZoomLevelString(void);

	// Osd
#ifdef USE_GLES
	int MaxSizeGPUImageCache(void);
	int OglOsdIsDisabled(void);
	void SetDisableOglOsd(void);
	void SetEnableOglOsd(void);
#endif
	void OsdClose(void);
	void OsdDraw(int, int, int, int, int, const uint8_t *, int, int);
	void SetOsdSize(int, int);
	void SetScreenSize(int, int);

	// Audio
	int GetVideoAudioDelayMs(void) { return m_pConfig->ConfigVideoAudioDelayMs; }             ///< get the configured video/audio delay
	int GetMinBufferFillLevelThresholdMs(void) { return MIN_BUFFER_FILL_LEVEL_THRESHOLD_MS; } ///< get the minimum buffer fill level to start playback
	void SetPassthroughOn(bool);
	bool IsPassthroughOn(void) const { return m_passthroughOn; }       ///< true, if the passthrough is switched on
	void TogglePassthrough(void) { SetPassthroughOn(!m_passthroughOn); } ///< switch the passthrough on or off (not stored)
	cString PassthroughString(void);
	void ResetChannelId(void);

	// Logging, statistics
	void GetStats(int *, int *, int *);

	// Mediaplayer
	void SetAudioCodec(enum AVCodecID, AVCodecParameters *, AVRational);
	void SetVideoCodec(enum AVCodecID, AVCodecParameters *, AVRational);
	void SetMediaStreams(bool, bool);
	int PlayAudioPkts(AVPacket *);
	int PlayVideoPkts(AVPacket *);
	void StillPicturePkt(AVPacket *);

	// Detach/ attach
	void Detach(void);
	void Attach(void);
	bool IsDetached(void) const;
	void ResetOsdProvider(void) { m_pOsdProvider = nullptr; } ///< forget the osd provider (deleted by VDR)
	/** true, if the osd provider exists */
	bool IsOsdProviderSet(void) const { return m_pOsdProvider != nullptr; }
	void SetStartDetached(void) { m_forceDetached = true; }   ///< start the plugin in detached state
	bool IsDraining(void) { return m_draining; }              ///< true, if the device waits for empty buffers

	// Pip
	int PlayPipVideo(const uchar *, int);
	void SetDrmCanDisplayPip(bool canDisplay) { m_drmCanDisplayPip = canDisplay; }     ///< set, if the drm device has a plane for pip
	bool UsePip(void) { return m_drmCanDisplayPip && !m_disablePip && m_pPipHandler; } ///< true, if pip can be used
	void ResetPipStream(void);
	void ToggleRenderPipPosition(void) { m_pipUseAlt = !m_pipUseAlt; }  ///< toggle the pip position (normal/alternative)
	// wrapper functions
	void SetDisablePip(void) { m_disablePip = true; }   ///< disable pip (workaround option)
	bool PipIsEnabled(void);
	void PipEnable(void);
	void PipDisable(void);
	void PipToggle(void);
	void PipChannelChange(int);
	void PipChannelSwap(bool);
	void PipSwapPosition(void);
	void PipSetSize(void);
	void SetRenderPipSize(void);
	void SetRenderPipActive(bool);

	// state transitioning functions
	void TriggerEvent(const Event&);
	bool VideoArrivedInAudioOnlyPlayback(void);
	bool TryResetPipStream(void);

	void LeaveState(State);
	void EnterState(State);
	void HandleStillPicture(const StillPictureEvent &);
	void HandleDisplayModeChange(const sDrmMode &);
	void HaltVideoThreads(void);
	void ResumeVideoThreads(void);
#ifdef USE_GLES
	bool HaltOpenGlThread(void);
	void ResumeOpenGlThread(void);
#endif
	bool IsDetachForced(void);
	void SetDetachForced(void) { m_forceDetached = true; }  ///< stay detached (until Attach())
	int InitAudio(bool);
	void ResetVideoFilter(void);
	void SetTrickSpeed(double, bool, bool);
	bool SchedulePlaybackStart(void);
	void ScheduleResyncAtPtsMs(int64_t);
	void ResumeFromPause(void);
	void PausePlayback(bool);
	void ResumePlayback(void);

private:
	static constexpr int MIN_BUFFER_FILL_LEVEL_THRESHOLD_MS = 450; ///< min buffering threshold in ms

	std::unique_ptr<cStateMachine> m_pStateMachine;     ///< the state machine

	bool m_initialized = false;                     ///< true, if the plugin had a successful Initialize()
	std::atomic<bool> m_draining = false;           ///< true, if the device is in draining mode (waiting for empty buffers)
	std::mutex m_eventMutex;                        ///< mutex to protect event queue
	bool m_needsMakePrimary = false;                ///< true, if device should be made a primary device after attach
	cDvbSpuDecoder *m_pSpuDecoder = nullptr;        ///< pointer to spu decoder
	cDeviceConfig *m_pConfig;                       ///< pointer to cDeviceConfig object
	cVideoRender *m_pRender = nullptr;              ///< pointer to cVideoRender object (nullptr, if detached)
	cVideoStream *m_pVideoStream = nullptr;         ///< pointer to main video stream (nullptr, if detached)
	cAudioStream *m_pAudio = nullptr;               ///< pointer to cAudioStream object (nullptr, if detached)
	cAudioDecoder *m_pAudioDecoder = nullptr;       ///< pointer to cAudioDecoder object
	cDeviceOsdProvider *m_pOsdProvider = nullptr;   ///< pointer to cDeviceOsdProvider object
	cHardwareDevice *m_pHardwareDevice = nullptr;   ///< pointer to hardware device description
	cEventHandler *m_pEventHandler = nullptr;       ///< event handler thread
	cReassemblyBufferVideo m_videoReassemblyBuffer; ///< video pes reassembly buffer
	cReassemblyBufferAudio m_audioReassemblyBuffer; ///< audio pes reassembly buffer
	cJitterTracker m_audioJitterTracker{"audio"};   ///< audio jitter tracker
	cJitterTracker m_videoJitterTracker{"video"};   ///< video jitter tracker
	/** current channel switch mode */
	std::atomic<ChannelSwitchMode> m_channelSwitchMode = CHANNEL_SWITCH_AVSYNC;
	bool m_logPlaybackStart = true;                 ///< log the duration until the playback started

	std::atomic<PlaybackMode> m_playbackMode = NONE; ///< current playback mode
	std::atomic<int> m_zoomLevel = 0; ///< active zoom level (0 = off, reset with every new content)
	std::atomic<bool> m_passthroughOn = false; ///< passthrough is switched on (setup or toggle)
	int m_audioChannelID = -1;       ///< current audio channel ID
	cGrabber *m_pGrab = nullptr;     ///< pointer to grabber object (nullptr, if detached)

	cVideoStream *m_pPipStream = nullptr; ///< pointer to pip video stream (nullptr, if detached)
	cReassemblyBufferVideo m_pipReassemblyBuffer; ///< pip pes reassembly buffer
	cPipHandler *m_pPipHandler = nullptr; ///< pointer to pip handler (nullptr, if detached)
	std::mutex m_sizeMutex;          ///< mutex to lock screen size (which is accessed by different threads)
	std::atomic<bool> m_receivedAudio = false; ///< flag if audio packets have been received
	std::atomic<bool> m_receivedVideo = false; ///< flag if video packets have been received
	std::atomic<bool> m_receivedValidAudio = false; ///< flag if valid audio packets have been received
	std::atomic<bool> m_receivedValidVideo = false; ///< flag if valid video packets have been received
	bool m_pipUseAlt = false;        ///< use alternative pip position
	bool m_drmCanDisplayPip = true;  ///< true, if the drm device is able to display a pip video
	bool m_disablePip = false;       ///< true, if pip was disabled by the user
	int m_volume = 0;                ///< track the volume in the device (for attach)

	int m_osdWidth = 0;              ///< osd width
	int m_osdHeight = 0;             ///< osd height
	int m_screenWidth = 0;           ///< screen width
	int m_screenHeight = 0;          ///< screen height

	bool m_forceDetached = false; ///< start the plugin in detached state
	bool m_externalPlayerActive = false; ///< true, if we detached for an external player

	int PlayVideoInternal(cVideoStream *, cReassemblyBufferVideo *, const uchar *, int, bool, bool);
	void FlushAudio(void);
	void DeleteDecoderAndOutput(void);
	void ResetPipStreamLocked(void);
	int64_t GetFirstAudioPtsMsToPlay();
	int64_t GetFirstVideoPtsMsToPlay();
	int GetBufferFillLevelThresholdMs();
	int GetPassthroughSetting(void);
	void SetDrain(bool);
};

#endif
