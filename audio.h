// SPDX-License-Identifier: AGLP-3.0-or-later

/**
 * @file audio.h
 * Audio Interface Header File
 *
 * @copyright 2009 - 2014 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

/**
 * Audio Interface
 * @defgroup audio Audio Module
 *
 * Handles the audio stream
 */

#ifndef __DRMDEVICE_AUDIO_H
#define __DRMDEVICE_AUDIO_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
}

#include <vdr/thread.h>

#include "alsadevice.h"
#include "audioprocessor.h"
#include "filllevel.h"
#include "pidcontroller.h"
#include "ringbuffer.h"
#include "statemachine.h"

class cDeviceConfig;
class cOutputDevice;

/**
 * Audio Interface
 *
 * Receives the decoded audio frames (or the spdif bursts for pass-through),
 * filters them (channel mapping, equalizer, downmix, resampling), applies the
 * software audio processing and queues the samples into a ringbuffer. The
 * audio thread writes the ringbuffer to ALSA and compensates the clock drift
 * between the audio hardware and the stream.
 *
 * @ingroup audio
 */
class cAudioStream : public cThread {
public:
	cAudioStream(cOutputDevice *);

	int LazyInit(void);
	void Exit(void);
	int Setup(AVRational, int , int , bool);
	void SetPaused(bool);
	bool IsPaused(void) { return m_paused; }            ///< true, if audio is paused
	void Filter(AVFrame *, AVCodecContext *);
	void EnqueueSpdif(const uint16_t *, int, int64_t pts);
	bool IsBufferFull(void) { return m_pRingbuffer.FreeBytes() <= AUDIO_MIN_BUFFER_FREE; }  ///< true, if the ringbuffer has no space for more data
	/** true, if the ringbuffer and the hardware buffer are empty */
	bool IsBufferEmpty(void) { return m_pRingbuffer.UsedBytes() == 0 && m_alsa.GetHwDelayFrames() == 0; }

	void FlushBuffers(void);
	int GetUsedRingbufferBytes(void);
	int GetUsedRingbufferMs(void);
	int64_t GetHardwareOutputPtsMs(void);
	int64_t GetHardwareOutputDelayMs(void);
	int64_t GetHardwareOutputPtsTimebaseUnits(void);
	bool HasInputPts(void) { return m_inputPts != AV_NOPTS_VALUE; }                        ///< true, if data with a pts was queued
	int64_t GetInputPtsMs(void) { return m_alsa.PtsToMs(m_inputPts, av_q2d(m_timebase)); } ///< get the pts of the end of the queued data in ms
	int64_t GetOutputPtsMs(void);
	int GetAvResyncBorderMs(void) { return AV_SYNC_BORDER_MS; }  ///< get the a/v difference, which triggers a resync

	void SetTrickSpeed(double, bool, bool);
	void AdvanceTrickSpeedPts(int64_t);
	int64_t GetTrickSpeedPtsTimebaseUnits(void) { return m_trickSpeedPts; } ///< get the pts of the audio-only trick speed
	void ResetTrickSpeedPts(void) { m_trickSpeedPts = AV_NOPTS_VALUE; }     ///< forget the trick speed pts
	bool IsForwardTrickSpeed(void) { return m_forwardTrickspeed; }          ///< true, if trick speed plays forward
	bool IsSlowTrickSpeed(void) { return m_trickspeedFactor <= 1.0 ; }      ///< true, if trick speed is slow motion

	void SetVolume(int);
	void SetSoftvol(bool softVolume) { m_softVolume = softVolume; }  ///< use the software volume instead of the hardware mixer

	void SetNormalize(bool, int);
	void SetCompression(bool, int);
	void SetEqualizer(bool, int[18]);
	void SetStereoDescent(int);

	void SetPassthroughMask(int mask) { m_alsa.SetPassthroughMask(mask); }     ///< set the codecs, which are passed through (CODEC_* bit mask of codec_audio.h)
	void SetAutoAES(bool appendAes) { m_alsa.SetAutoAES(appendAes); }          ///< automatically append AES to the pass-through device
	void SetTimebase(AVRational timebase) { m_timebase = timebase; }           ///< set the timebase of the pts values
	void SetDownmix(int downMix) { m_alsa.SetDownmix(downMix); }               ///< downmix to stereo
	int GetPassthroughMask(void) { return m_alsa.GetPassthroughMask(); }       ///< get the codecs, which are passed through (read from the ELD in auto mode)
	int GetEldPassthroughMask(void) const { return m_alsa.GetEldPassthroughMask(); } ///< codecs of the last read ELD (-1: not read yet)
	const std::string &GetDeviceName(void) const { return m_alsa.GetDeviceName(); }  ///< name of the used alsa device
	cDeviceConfig *Config(void) { return m_pConfig; }                          ///< get the config

	void DropSamplesOlderThanPtsMs(int64_t);
	void ClockDriftCompensation(void);
	void ResetHwDelayBaseline(void);
	void SetHwDelayBaseline(void);

protected:
	virtual void Action(void);

private:
	constexpr static int AUDIO_MIN_BUFFER_FREE = 3072 * 8 * 8;   ///< Minimum free space in audio buffer 8 packets for 8 channels
	constexpr static int AV_SYNC_BORDER_MS = 5000;               ///< absolute max a/v difference in ms which should trigger a resync
	constexpr static int BYTES_PER_SAMPLE = 2;                   ///< number of bytes per sample
	constexpr static int64_t PTS_WRAP = 1LL << 33;               ///< wraparound mod for a 33-bit PTS
	constexpr static double MAX_TRICKSPEED_STEP_DELAY_MS = 3000; ///< upper bound for a single AdvanceTrickSpeedPts(), in case of large/ erratic pts jumps

	cOutputDevice *m_pDevice;               ///< pointer to device
	cDeviceConfig *m_pConfig;               ///< pointer to config
	cAlsaDevice m_alsa;                     ///< alsa device
	cBufferFillLevelLowPassFilter m_fillLevel;                  ///< low pass filter for the buffer fill level
	cPidController m_pidController{3, 0.005, 0, 1000};          ///< PID controller for clock drift compensation with tuning values coming from educated guesses
	std::chrono::steady_clock::time_point m_lastPidInvocation;  ///< last time the PID controller was invoked
	int m_packetCounter = 0;                ///< packet counter for logging

	// common audio
	bool m_initialized = false;             ///< class initialized
	std::mutex m_mutex;                     ///< mutex for thread safety
	std::mutex m_pauseMutex;                ///< mutex for a safe thread pausing
	std::mutex m_queueMutex;                ///< mutex for queue safety
	std::vector<Event> m_eventQueue;        ///< event queue for incoming events
	std::atomic<double> m_pitchPpm = 0;     ///< pitch adjustment in ppm. Positive values are faster
	int m_pitchAdjustFrameCounter = 0;      ///< counter for pitch adjustment frames

	int m_volume = 0;                       ///< current volume (0 .. 1000)
	int m_stereoDescent;                    ///< volume descent for stereo
	AVRational m_timebase;                  ///< AVCodecContext pkts_timebase

	int64_t m_inputPts = AV_NOPTS_VALUE;    ///< pts clock (last pts in ringbuffer)
	std::atomic<bool> m_paused = true;      ///< audio is paused
	std::atomic<int64_t> m_trickSpeedPts = AV_NOPTS_VALUE; ///< pts of the last received (undecoded) packet during audio-only trickspeed
	std::atomic<double> m_trickspeedFactor = 0;   ///< current trickspeed factor for AdvanceTrickSpeedPts()
	std::atomic<bool> m_trickspeed = false;       ///< true, if trickspeed is active
	std::atomic<bool> m_forwardTrickspeed = true; ///< current trickspeed direction

	bool m_softVolume;                      ///< flag to use soft volume
	int m_spdifBurstSize = 0;               ///< size of the current spdif burst
	std::vector<uint16_t> m_pauseBurst;     ///< holds the burst data itself
	std::vector<uint8_t> m_amplifyBuffer;   ///< soft volume is applied to a copy of the ringbuffer data
	int m_spdifReadPhase = 0;               ///< position of the next ringbuffer byte in its spdif burst
	int m_hwBaseline = 0;                   ///< saves the hw delay (pause bursts) once a real audio frame to correctly do the AV-Sync
	bool m_firstRealAudioReceived = false;  ///< false, as long as no real audio was sent - used to trigger the baseline set

	void Enqueue(const uint16_t *, int, int64_t);
	void EnqueueFrame(AVFrame *);
	bool SendAudio(int);
	bool SendPause(void);
	void FillPause(uint8_t *, int);
	void AdvanceSpdifReadPhase(int);
	void RebuildPauseBurst(int);
	void Stop(void);
	void FlushAlsaBuffers(void);
	void DropAlsaBuffers(void);
	bool CyclicCall(void);
	void ProcessEvents(void);

	// audio manipulation
	cAudioProcessor m_audioProcessor;       ///< software audio processing (normalizer, compressor, volume)
	bool m_useNormalizer;                   ///< flag to use volume normalize
	bool m_useCompressor;                   ///< flag to use compress volume
	bool m_useEqualizer;                    ///< flag to use equalizer

	// filter
	int m_filterChanged = 0;                            ///< filter has changed
	int m_filterReady = 0;                              ///< filter is ready
	AVFilterGraph *m_pFilterGraph = nullptr;            ///< audio filter graph
	AVFilterContext *m_pBuffersrcCtx = nullptr;         ///< source (input) of the filter graph
	AVFilterContext *m_pBuffersinkCtx = nullptr;        ///< sink (output) of the filter graph
	int InitFilter(AVCodecContext *);
	AVFrame *FilterGetFrame(void);
	int CheckForFilterReady(AVCodecContext *);
	std::string BuildChannelMapFilter(const AVChannelLayout &);

	// ring buffer variables
	/** ring buffer size (frames of 1 to 8 channels fit): ~4s 8ch 16bit 48kHz, ~1s TrueHD (8ch 16bit 192kHz) */
	static constexpr unsigned RINGBUFFER_SIZE = 3 * 5 * 7 * 8 * 2 * 2000;
	cRingbuffer m_pRingbuffer{RINGBUFFER_SIZE};                           ///< sample ring buffer

	int64_t GetOutputPtsMsInternal(void);
};

#endif
