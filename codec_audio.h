// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file codec_audio.h
 * Audio Decoder Header File
 *
 * @copyright 2009 - 2013, 2015 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_CODEC_AUDIO_H
#define __DRMDEVICE_CODEC_AUDIO_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <vdr/remux.h>

#include "config.h"
#include "spdif.h"

class cAudioStream;

/**
 * @defgroup audiodecoder Audio Decoder
 * FFmpeg Based Audio Decoder Frontend
 */

/**
 * Bits used for the passthrough mask
 *
 * 0x01 and 0x02 are kept unused for compatibility with an existing setup.conf
 *
 * @ingroup audiodecoder
 */
enum PassthroughMask {
	CODEC_AC3    = (1 << 2), ///< AC-3 bit mask
	CODEC_EAC3   = (1 << 3), ///< E-AC-3 bit mask
	CODEC_DTS    = (1 << 4), ///< DTS bit mask
	CODEC_TRUEHD = (1 << 5), ///< TrueHD bit mask
	CODEC_DTSHD  = (1 << 6), ///< DTS-HD bit mask
	/** all codecs, which can be passed through */
	CODEC_PASSTHROUGH_ALL = CODEC_AC3 | CODEC_EAC3 | CODEC_DTS | CODEC_TRUEHD | CODEC_DTSHD,
	/** pass the codecs through, which the sink reports in its ELD (instead of the codec bits) */
	CODEC_PASSTHROUGH_AUTO = (1 << 7),
};

/**
 * Audio Decoder
 *
 * FFmpeg Based Audio Decoder Frontend
 *
 * Handles:
 * - Audio packet decoding using FFmpeg
 * - SPDIF passthrough
 * - Format changes
 * - Audio frame delivery to cAudioStream
 *
 * @ingroup audiodecoder
 */
class cAudioDecoder {
public:
	cAudioDecoder(cAudioStream *);
	~cAudioDecoder(void);
	void Open(AVCodecID, AVCodecParameters * = nullptr, AVRational = { .num = 1, .den = PTSTICKS });
	void Close(void);
	void Decode(const AVPacket *);
	void FlushBuffers(void);
	void UpdatePassthroughMask(void);
	void RetryPassthrough(void);
	AVCodecID GetCodecId() const { return m_codecId; }  ///< get the current codec id

private:
	cAudioStream *m_pAudio;                     ///< audio module
	AVCodecContext *m_pAudioCtx = nullptr;      ///< ffmpeg audio codec context
	AVCodecID m_codecId = AV_CODEC_ID_NONE;     ///< current codec id
	AVFrame *m_pFrame;                          ///< decoded ffmpeg audio frame
	int64_t m_lastPts = AV_NOPTS_VALUE;         ///< last seen PTS
	std::atomic<int> m_passthroughMask;         ///< codecs, which are passed through
	int m_currentPassthroughMask = 0;           ///< current passthrough mask
	int m_currentSampleRate = 0;                ///< current sample rate
	int m_currentNumChannels = 0;               ///< current number of channels
	int m_currentHwSampleRate = 0;              ///< current hw sample rate
	int m_currentHwNumChannels = 0;             ///< current number of hw channels
	bool m_currentPassthrough = false;          ///< true, if the audio is currently set up for passthrough
	std::atomic<bool> m_passthroughFailed = false; ///< true, if the passthrough of the current codec failed (decode it instead)
	std::atomic<bool> m_dtshdFailed = false;    ///< true, if the DTS-HD passthrough failed (pass the DTS core through instead)
	int m_currentDtshdRate = 0;                 ///< rate of the current DTS-HD bitstream (0: no DTS-HD)
	sAudioStreamInfo m_streamInfo;              ///< stream properties, which were published for the statistics
	std::mutex m_mutex;                         ///< decoder mutex

	cSpdifMuxer m_spdif;                        ///< spdif muxer


	bool ShouldTryPassthrough(void);
	int Passthrough(const AVPacket *);
	void DecodePCM(const AVPacket *);
	int CheckUpdateFormat(bool);
	void UpdateStreamInfo(bool, int, int);
};

#endif
