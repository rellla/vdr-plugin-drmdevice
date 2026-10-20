// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spdif.h
 * IEC 61937 Bitstream (Passthrough) Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_SPDIF_H
#define __DRMDEVICE_SPDIF_H

#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

/**
 * Output format of a bitstream
 *
 * @ingroup audiodecoder
 */
struct sPassthroughFormat {
	int sampleRate;        ///< sample rate of the output device
	int channels;          ///< number of channels of the output device
	int dtshdRate;         ///< rate of the DTS-HD bitstream (0: DTS core only)
};

sPassthroughFormat GetPassthroughFormat(AVCodecID, int, int, bool);
bool IsDtsHdProfile(int);
int GetIEC958SampleRateCode(int, int, bool);

/**
 * IEC 61937 Bitstream Muxer
 *
 * Packs the audio packets into IEC 61937 bursts with FFmpeg's spdif muxer.
 *
 * @ingroup audiodecoder
 */
class cSpdifMuxer {
public:
	cSpdifMuxer(void) = default;
	~cSpdifMuxer(void) { Close(); }
	cSpdifMuxer(const cSpdifMuxer &) = delete;
	cSpdifMuxer &operator=(const cSpdifMuxer &) = delete;

	bool Open(AVCodecID, int, int = 0);
	void Close(void);
	bool IsOpen(void) const { return m_pFmtCtx != nullptr; } ///< true, if the muxer is open
	const std::vector<uint8_t> &Mux(const AVPacket *, int64_t *);

private:
	static constexpr int IO_BUFFER_SIZE = 32768;        ///< size of the I/O buffer of the muxer

#if LIBAVFORMAT_VERSION_MAJOR >= 61
	static int WriteCallback(void *, const uint8_t *, int);
#else
	static int WriteCallback(void *, uint8_t *, int);
#endif

	AVFormatContext *m_pFmtCtx = nullptr;               ///< spdif muxer context
	std::vector<uint8_t> m_output;                      ///< output of the muxer (one burst)
	int64_t m_burstPts = AV_NOPTS_VALUE;                ///< pts of the first packet of the next burst
};

#endif
