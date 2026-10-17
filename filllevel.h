// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file filllevel.h
 * Low-pass Filter for Audio Buffer Fill Level Measurement Header File
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_FILLLEVEL_H
#define __DRMDEVICE_FILLLEVEL_H

#include <cstdint>
#include <mutex>

/**
 * Fill Level Low Pass Filter
 *
 * @ingroup misc
 */
class cBufferFillLevelLowPassFilter {
public:
	void Reset();
	void ResetFramesCounters();
	int64_t GetFramesReceived() { std::lock_guard<std::mutex> lock(m_mutex); return m_receivedFrames; } ///< get the number of received frames
	int64_t GetFramesPlayed() { std::lock_guard<std::mutex> lock(m_mutex); return m_writtenToAlsaFrames; } ///< get the number of frames written to ALSA
	void ReceivedFrames(int count) { std::lock_guard<std::mutex> lock(m_mutex); m_receivedFrames += count; } ///< count received frames
	void WroteFrames(int count) { std::lock_guard<std::mutex> lock(m_mutex); m_writtenToAlsaFrames += count; } ///< count frames written to ALSA
	double GetBufferFillLevelFramesAvg() { std::lock_guard<std::mutex> lock(m_mutex); return m_bufferFillLevelFramesAvg; } ///< get the average fill level in frames
	void SetMinBufferSizeFrames(int size) { std::lock_guard<std::mutex> lock(m_mutex); m_minBufferSizeFrames = size; } ///< set the minimum target fill level
	void UpdateAvgBufferFillLevel(int);
	bool IsSettled() { std::lock_guard<std::mutex> lock(m_mutex); return m_state == SETTLED; }

private:
	enum State {
		UNINITIALIZED,
		SETTLING,
		SETTLED,
	};

	constexpr static double FLOATING_AVERAGE_ALPHA_SETTLING = 0.02;
	constexpr static double FLOATING_AVERAGE_ALPHA_NORMAL = 0.002;
	constexpr static int SETTLING_DURATION_PACKETS = 30 / 0.16; // 30 seconds with one packet every 160ms

	std::mutex m_mutex;                                 ///< protects the members (accessed by different threads)
	State m_state = UNINITIALIZED;                      ///< current state
	// 64 bit, a 32 bit counter overflows after about 12 hours at 48kHz
	int64_t m_receivedFrames = 0;              ///< frames received from the decoder
	int64_t m_writtenToAlsaFrames = 0;         ///< frames written to ALSA
	double m_bufferFillLevelFramesAvg = 0;              ///< average fill level in frames
	double m_bufferFillLevelFramesTargetValue = 0;      ///< fixed target fill level in frames
	int m_frameCounter = 0;                             ///< number of updates while settling
	double m_floatingAverageAlpha = FLOATING_AVERAGE_ALPHA_NORMAL; ///< current filter coefficient
	int m_minBufferSizeFrames = 0;                      ///< minimum target fill level in frames

	const char* StateToString(State d);
};

#endif
