// SPDX-License-Identifier: AGLP-3.0-or-later

/**
 * @file alsadevice.h
 * ALSA Output Device Header File
 *
 * @copyright 2009 - 2014 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

/**
 * ALSA Output Device
 * @ingroup audio
 *
 * Handles the ALSA Output Device
 */

#ifndef __DRMDEVICE_ALSADEVICE_H
#define __DRMDEVICE_ALSADEVICE_H

#include <atomic>
#include <string>
#include <vector>

#include <alsa/asoundlib.h>

class cDeviceConfig;

/**
 * Alsa Interface
 *
 * Opens and configures the ALSA PCM device (also for pass-through) and the
 * mixer, and provides helpers to convert between frames, bytes, ms and pts.
 *
 * @ingroup audio
 */
class cAlsaDevice {
public:
	/** Create an ALSA device with the given plugin config */
	cAlsaDevice(cDeviceConfig *);

	bool Init(void);
	void Exit(void);
	int Setup(int, int, bool, int);
	int Write(const void *, int);
	void FlushBuffers(bool);
	bool HandleError(int);
	void SetVolume(int);

	int WaitUntilReady(void);
	bool CheckWrittenFrames(int, int);
	int GetHwDelayFrames(void);
	int GetAvailableBufferFrames(bool);
	std::vector<std::string> GetChannelLayoutAsArray(void);

	// getters and setters
	int GetBufferSizeFrames(void) { return m_bufferSizeFrames; }     ///< get the ALSA buffer size in frames
	int GetDownmix(void) { return m_downmix; }                       ///< true, if a stereo downmix is configured
	int GetHwNumChannels(void) { return m_hwNumChannels; }           ///< get the number of channels of the hardware
	int GetHwSampleRate(void) { return m_hwSampleRate; }             ///< get the sample rate of the hardware
	/** true, if the PCM device is set up */
	bool IsRunning(void) { return ((m_pPCMHandle != nullptr) && m_hwSampleRate); }
	bool IsPassthroughActive(void) { return m_passthroughActive; }   ///< true, if the current audio is passed through
	void SetPassthroughMask(int mask) { m_passthroughMask = mask; }  ///< set the codecs, which are passed through
	int GetPassthroughMask(void) const { return m_passthroughMask; } ///< get the codecs, which are passed through
	void SetAutoAES(bool appendAes) { m_appendAES = appendAes; }     ///< automatically append AES to the pass-through device
	void SetDownmix(int downMix) { m_downmix = downMix; }            ///< downmix to stereo

	// calculation helpers
	size_t FramesToBytes(int frames) { return snd_pcm_frames_to_bytes(m_pPCMHandle, frames); } ///< convert frames to bytes
	int BytesToFrames(size_t bytes) { return snd_pcm_bytes_to_frames(m_pPCMHandle, bytes); }   ///< convert bytes to frames
	int64_t PtsToMs(int64_t pts, double timebase) { return pts * timebase * 1000; }            ///< convert a pts to ms
	int64_t MsToPts(int64_t ptsMs, double timebase) { return ptsMs / timebase / 1000; }        ///< convert ms to a pts
	int MsToFrames(int milliseconds) { return (int64_t)milliseconds * m_hwSampleRate / 1000; } ///< convert ms to frames
	int FramesToMs(int frames) { return (int64_t)frames * 1000 / m_hwSampleRate; }             ///< convert frames to ms
	/** convert frames to a pts */
	int64_t FramesToPts(int frames, double timebase) { return MsToPts((int64_t)frames * 1000 / m_hwSampleRate, timebase); }
	double FramesToMsDouble(int frames) { return (double)frames * 1000 / m_hwSampleRate; }     ///< convert frames to ms (without rounding)

private:
	const char *m_pPCMDevice;                 ///< Alsa PCM device name
	snd_pcm_t *m_pPCMHandle = nullptr;        ///< alsa pcm handle

	// mixer
	const char *m_pMixerDevice = nullptr;     ///< mixer device name (not used)
	const char *m_pMixerChannel;              ///< mixer channel name
	snd_mixer_t *m_pMixer = nullptr;          ///< alsa mixer handle
	snd_mixer_elem_t *m_pMixerElem = nullptr; ///< alsa mixer element

	snd_pcm_uframes_t m_bufferSizeFrames = 0; ///< alsa buffer size in frames
	long m_mixerMin = 0;                      ///< minimum volume of the mixer element
	long m_mixerMax = 0;                      ///< maximum volume of the mixer element
	bool m_appendAES;                         ///< flag to automatic append AES
	int m_passthroughMask;                    ///< passthrough mask
	std::atomic<bool> m_passthroughActive = false; ///< set, if passthrough is active
	unsigned int m_hwSampleRate = 0;          ///< hardware sample rate in Hz
	unsigned int m_hwNumChannels = 0;         ///< number of hardware channels
	int m_downmix;                            ///< set stereo downmix
	bool m_useMmap = false;                   ///< use mmap
	std::string m_deviceName;                 ///< name of the used device (without the AES parameters)
	std::string m_openedDevice;               ///< the device string, which was opened (with the AES parameters)

	bool ShouldAppendAES(void) { return m_appendAES && m_passthroughMask; }  ///< true, if AES shall be appended to the device name
	char *OpenDevice(const char *);
	std::string BuildDeviceString(const char *, bool, int, int);
	bool ReopenDevice(const std::string &);
	char *FindDevice(const char *, const char *);
	bool InitDevice(void);
	void InitMixer(void);
};

#endif
