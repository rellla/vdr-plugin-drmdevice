// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ac3encoder.h
 * AC-3 Encoder Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_AC3ENCODER_H
#define __DRMDEVICE_AC3ENCODER_H

#include <cstdint>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

/**
 * AC-3 Encoder
 *
 * Encodes decoded multichannel audio to AC-3, so it can be passed through to a
 * receiver, which only decodes AC-3 (e.g. via S/PDIF or the ARC of a TV).
 * The frames are converted to 5.1 (more channels are downmixed) and collected,
 * until an AC-3 frame (1536 samples) is complete.
 *
 * @ingroup audio
 */
class cAc3Encoder {
public:
	cAc3Encoder(void) = default;
	~cAc3Encoder(void) { Close(); }
	cAc3Encoder(const cAc3Encoder &) = delete;
	cAc3Encoder &operator=(const cAc3Encoder &) = delete;

	static bool CanEncode(int);
	bool Open(int, AVRational);
	void Close(void);
	bool IsOpen(void) const { return m_pCtx != nullptr; }                      ///< true, if the encoder is open
	int GetSampleRate(void) const { return m_pCtx ? m_pCtx->sample_rate : 0; } ///< sample rate of the encoder (0, if closed)
	void Flush(void);
	bool Encode(const AVFrame *, const std::function<void(const AVPacket *)> &);

private:
	static constexpr int BIT_RATE = 640000;             ///< bit rate of the AC-3 stream (maximum of AC-3)
	static constexpr int MAX_PTS_DEVIATION_MS = 100;    ///< a larger pts jump drops the collected samples

	AVCodecContext *m_pCtx = nullptr;                   ///< encoder context
	SwrContext *m_pSwr = nullptr;                       ///< converter to the format of the encoder
	AVChannelLayout m_swrInLayout{};                    ///< channel layout of the converter input
	int m_swrInFormat = AV_SAMPLE_FMT_NONE;             ///< sample format of the converter input
	AVAudioFifo *m_pFifo = nullptr;                     ///< collected samples
	AVFrame *m_pFrame = nullptr;                        ///< frame for the encoder
	AVPacket *m_pPacket = nullptr;                      ///< encoded packet
	int64_t m_basePts = AV_NOPTS_VALUE;                 ///< pts of the first sample after the last reset
	int64_t m_encodedSamples = 0;                       ///< samples encoded since the last reset

	bool SetupConverter(const AVFrame *);
	bool EncodeFrames(const std::function<void(const AVPacket *)> &);
	int64_t SamplesToPts(int64_t) const;
	void Reset(int64_t);
};

#endif
