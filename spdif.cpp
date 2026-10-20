// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spdif.cpp
 * IEC 61937 Bitstream (Passthrough)
 *
 * This file defines cSpdifMuxer, which packs the audio packets into IEC 61937
 * bursts, and the output format of the bitstreams.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>
#include <cstring>
#include <endian.h>

#include <alsa/asoundlib.h>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/mem.h>
}

#include "logger.h"
#include "misc.h"
#include "spdif.h"

/**
 * @addtogroup audiodecoder
 * @{
 */

// FFmpeg 6.1 renamed the profiles from FF_PROFILE_* to AV_PROFILE_*
#ifdef AV_PROFILE_DTS_HD_MA
#define DTS_PROFILE(name) AV_PROFILE_##name  ///< DTS profile (FFmpeg >= 6.1)
#else
#define DTS_PROFILE(name) FF_PROFILE_##name  ///< DTS profile (FFmpeg < 6.1)
#endif

/** sample rate, if the rate of the stream is unknown (e.g. undecoded PES data from VDR) */
static constexpr int DEFAULT_SAMPLE_RATE = 48000;
/** number of channels of a high bitrate (HBR) bitstream */
static constexpr int HBR_CHANNELS = 8;
/** IEC 60958 frame rate of a high bitrate (HBR) bitstream (8 channels with 192kHz) */
static constexpr int HBR_FRAME_RATE = 768000;

/** @} */

/**
 * Check, if a DTS stream has a DTS-HD part, which can be passed through
 *
 * DTS-HD Master Audio (lossless) and DTS-HD High Resolution Audio have a DTS
 * core and a DTS-HD extension. DTS Express has no core, it can't be passed
 * through.
 *
 * @param profile   profile of the DTS stream
 * @return          true, if the stream is DTS-HD with a core
 */
bool IsDtsHdProfile(int profile)
{
	switch (profile) {
		case DTS_PROFILE(DTS_HD_HRA):
		case DTS_PROFILE(DTS_HD_MA):
#if defined(AV_PROFILE_DTS_HD_MA_X) || defined(FF_PROFILE_DTS_HD_MA_X)
		case DTS_PROFILE(DTS_HD_MA_X):
		case DTS_PROFILE(DTS_HD_MA_X_IMAX):
#endif
			return true;
		default:
			return false;
	}
}

/**
 * Get the output format of a bitstream
 *
 * - AC-3, DTS: 2 channels with the sample rate of the stream
 * - E-AC-3: 2 channels with four times the sample rate of the stream (IEC 61937-3)
 * - TrueHD: high bitrate bitstream with 8 channels and 192kHz, or 176.4kHz for the
 *   44.1kHz rate family (IEC 61937-9)
 * - DTS-HD Master Audio: high bitrate bitstream with 8 channels and 192kHz (IEC 61937-5)
 * - DTS-HD High Resolution Audio: 2 channels with 192kHz (IEC 61937-5)
 *
 * DTS-HD is only passed through with a core of the 48kHz rate family, otherwise
 * only the DTS core is passed through.
 *
 * @param codecId       codec of the stream
 * @param sampleRate    sample rate of the stream (0, if unknown: 48kHz is used)
 * @param profile       profile of the stream (only used for DTS)
 * @param dtshd         true, if the DTS-HD part of a DTS stream shall be passed through
 *
 * @return the output format
 */
sPassthroughFormat GetPassthroughFormat(AVCodecID codecId, int sampleRate, int profile, bool dtshd)
{
	if (sampleRate <= 0)
		sampleRate = DEFAULT_SAMPLE_RATE;

	bool rateFamily48k = sampleRate % 44100 != 0;

	switch (codecId) {
		case AV_CODEC_ID_EAC3:
			return {sampleRate * 4, 2, 0};
		case AV_CODEC_ID_TRUEHD:
			return {rateFamily48k ? 192000 : 176400, HBR_CHANNELS, 0};
		case AV_CODEC_ID_DTS:
			if (dtshd && rateFamily48k && IsDtsHdProfile(profile)) {
				if (profile == DTS_PROFILE(DTS_HD_HRA))
					return {192000, 2, 192000};

				return {192000, HBR_CHANNELS, HBR_FRAME_RATE};
			}
			return {sampleRate, 2, 0};
		default:
			return {sampleRate, 2, 0};
	}
}

/**
 * Get the sample rate code of the IEC 60958 channel status (AES3 byte)
 *
 * A high bitrate bitstream (8 channels with 192kHz) is sent with 768kHz.
 *
 * @param sampleRate    sample rate of the output device
 * @param channels      number of channels of the output device
 * @param passthrough   true, if a bitstream is sent
 *
 * @return the IEC958_AES3_CON_FS_* code
 */
int GetIEC958SampleRateCode(int sampleRate, int channels, bool passthrough)
{
	if (passthrough && channels == HBR_CHANNELS)
		return sampleRate == 192000 ? IEC958_AES3_CON_FS_768000 : IEC958_AES3_CON_FS_NOTID;

	switch (sampleRate) {
		case 32000:  return IEC958_AES3_CON_FS_32000;
		case 44100:  return IEC958_AES3_CON_FS_44100;
		case 48000:  return IEC958_AES3_CON_FS_48000;
		case 88200:  return IEC958_AES3_CON_FS_88200;
		case 96000:  return IEC958_AES3_CON_FS_96000;
		case 176400: return IEC958_AES3_CON_FS_176400;
		case 192000: return IEC958_AES3_CON_FS_192000;
		default:     return IEC958_AES3_CON_FS_NOTID;
	}
}

/**
 * Build an IEC 61937 pause burst
 *
 * The pause is filled with IEC 61937 pause data-bursts (data-type 3), which are intended to fill
 * gaps in a non-PCM stream and keep the receiver locked. Each burst has the size of a normal burst,
 * so the repetition period of the stream doesn't change. The burst payload carries the gap length,
 * which is the length of the burst in IEC 60958 frames, the rest of the burst is stuffed with zeros.
 *
 * @param size     size of a burst of the stream in bytes
 *
 * @return         the pause burst (16 bit little endian words), empty if the size is invalid
 */
std::vector<uint16_t> BuildPauseBurst(int size)
{
	constexpr int IEC61937_PREAMBLE1 = 0xF872;
	constexpr int IEC61937_PREAMBLE2 = 0x4E1F;
	constexpr int IEC61937_PAUSE = 0x03;            // data-type of the burst info Pc
	constexpr int IEC61937_PAUSE_PAYLOAD_BITS = 32; // length code Pd: gap length and a reserved word
	constexpr int IEC61937_HEADER_SIZE = 8;         // Pa, Pb, Pc and Pd
	constexpr int IEC60958_FRAME_SIZE = 4;          // two 16 bit subframes

	// the burst needs the header and the payload
	if (size < IEC61937_HEADER_SIZE + IEC61937_PAUSE_PAYLOAD_BITS / 8 || size % IEC60958_FRAME_SIZE)
		return {};

	std::vector<uint16_t> burst(size / 2, 0);
	int gapLength = std::min(size / IEC60958_FRAME_SIZE, 0xFFFF);

	burst[0] = htole16(IEC61937_PREAMBLE1);
	burst[1] = htole16(IEC61937_PREAMBLE2);
	burst[2] = htole16(IEC61937_PAUSE);
	burst[3] = htole16(IEC61937_PAUSE_PAYLOAD_BITS);
	burst[4] = htole16(gapLength);
	// burst[5] (reserved) and the stuffing are already zero

	return burst;
}

/**
 * Fill a buffer with pause bursts
 *
 * The pause bursts start at the given position in a burst, so they keep the
 * period of the bursts of the stream.
 *
 * @param buffer       buffer to fill
 * @param size         size of the buffer in bytes
 * @param pauseBurst   the pause burst (see BuildPauseBurst()), zeros are filled, if it's empty
 * @param phase        position in the burst, where the buffer starts
 */
void FillPauseBursts(uint8_t *buffer, int size, const std::vector<uint16_t> &pauseBurst, int phase)
{
	int burstSize = pauseBurst.size() * 2;
	if (!burstSize || phase < 0 || phase >= burstSize) {
		memset(buffer, 0, size);
		return;
	}

	const uint8_t *pause = reinterpret_cast<const uint8_t *>(pauseBurst.data());
	while (size > 0) {
		int bytes = std::min(size, burstSize - phase);
		memcpy(buffer, pause + phase, bytes);
		buffer += bytes;
		size -= bytes;
		phase = 0;
	}
}

/*****************************************************************************
 * cSpdifMuxer
 ****************************************************************************/

/**
 * Callback of the spdif muxer
 *
 * This is called, whenever FFmpeg flushes data from the muxer to the AVIOContext.
 * A flush is forced after every packet.
 */
#if LIBAVFORMAT_VERSION_MAJOR >= 61
int cSpdifMuxer::WriteCallback(void *opaque, const uint8_t *buffer, int bufferSize)
#else
int cSpdifMuxer::WriteCallback(void *opaque, uint8_t *buffer, int bufferSize)
#endif
{
	auto &output = *static_cast<std::vector<uint8_t> *>(opaque);
	output.insert(output.end(), buffer, buffer + bufferSize);

	return bufferSize;
}

/**
 * Open the spdif muxer
 *
 * @param codecId       codec of the stream
 * @param sampleRate    sample rate of the stream
 * @param dtshdRate     rate of the DTS-HD bitstream (0: only the DTS core)
 *
 * @retval true         the muxer is open
 * @retval false        on any error
 */
bool cSpdifMuxer::Open(AVCodecID codecId, int sampleRate, int dtshdRate)
{
	Close();

	if (avformat_alloc_output_context2(&m_pFmtCtx, nullptr, "spdif", nullptr) < 0 || !m_pFmtCtx) {
		LOGERROR("spdif: %s: failed to allocate the spdif muxer", __FUNCTION__);
		m_pFmtCtx = nullptr;
		return false;
	}

	// the AVIOContext may reallocate the buffer, so it has to be allocated with av_malloc()
	uint8_t *ioBuffer = static_cast<uint8_t *>(av_malloc(IO_BUFFER_SIZE));
	if (ioBuffer)
		m_pFmtCtx->pb = avio_alloc_context(ioBuffer, IO_BUFFER_SIZE, 1, &m_output, nullptr, &cSpdifMuxer::WriteCallback, nullptr);

	if (!m_pFmtCtx->pb) {
		LOGERROR("spdif: %s: failed to allocate the I/O context", __FUNCTION__);
		av_free(ioBuffer);
		Close();
		return false;
	}

	AVStream *stream = avformat_new_stream(m_pFmtCtx, nullptr);
	if (!stream) {
		Close();
		return false;
	}

	stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
	stream->codecpar->codec_id = codecId;
	stream->codecpar->sample_rate = sampleRate;
	av_channel_layout_default(&stream->codecpar->ch_layout, 2);
	stream->time_base = AVRational{1, sampleRate};

	AVDictionary *options = nullptr;
	if (dtshdRate)
		av_dict_set_int(&options, "dtshd_rate", dtshdRate, 0);

	int err = avformat_write_header(m_pFmtCtx, &options);
	av_dict_free(&options);

	if (err < 0) {
		LOGERROR("spdif: %s: failed to write the header: %s", __FUNCTION__, av_err2str(err));
		Close();
		return false;
	}

	LOGDEBUG2(L_CODEC, "spdif: %s: opened spdif muxer for %s @ %dHz%s", __FUNCTION__,
		avcodec_get_name(codecId), sampleRate, dtshdRate ? " (DTS-HD)" : "");

	return true;
}

/**
 * Close the spdif muxer and free the resources
 *
 * Data of a burst, which isn't complete yet, is dropped.
 */
void cSpdifMuxer::Close(void)
{
	if (m_pFmtCtx) {
		LOGDEBUG2(L_CODEC, "spdif: %s: close spdif muxer", __FUNCTION__);
		if (m_pFmtCtx->pb) {
			av_freep(&m_pFmtCtx->pb->buffer);
			avio_context_free(&m_pFmtCtx->pb);
		}
		avformat_free_context(m_pFmtCtx);
		m_pFmtCtx = nullptr;
	}

	m_output.clear();
	m_burstPts = AV_NOPTS_VALUE;
}

/**
 * Pack an audio packet into IEC 61937 bursts
 *
 * The muxer may collect several packets for one burst (e.g. TrueHD, E-AC-3 with
 * less than 6 blocks per frame). The burst gets the pts of its first packet,
 * which may be a packet of the previous burst (TrueHD).
 *
 * @param avpkt        input packet
 * @param[out] pts     pts of the burst (only set, if a burst is returned)
 *
 * @return             the burst, empty if the muxer needs more packets or on an error
 */
const std::vector<uint8_t> &cSpdifMuxer::Mux(const AVPacket *avpkt, int64_t *pts)
{
	m_output.clear();

	if (!m_pFmtCtx || !avpkt || !avpkt->data || avpkt->size <= 0)
		return m_output;

	AVPacket *pkt = av_packet_alloc();
	if (!pkt || av_packet_ref(pkt, avpkt) < 0) {
		LOGERROR("spdif: %s: can't reference the packet", __FUNCTION__);
		av_packet_free(&pkt);
		return m_output;
	}

	bool firstOfBurst = m_burstPts == AV_NOPTS_VALUE;
	if (firstOfBurst)
		m_burstPts = pkt->pts;

	pkt->stream_index = 0;
	int err = av_write_frame(m_pFmtCtx, pkt);
	av_packet_free(&pkt);

	if (err < 0) {
		LOGERROR("spdif: %s: av_write_frame failed: %s", __FUNCTION__, av_err2str(err));
		if (firstOfBurst)
			m_burstPts = AV_NOPTS_VALUE;
		m_output.clear();
		return m_output;
	}

	// always flush to see, if a burst is ready
	avio_flush(m_pFmtCtx->pb);

	if (!m_output.empty()) {
		*pts = m_burstPts;
		// The packet, which completes a MAT frame, is split: its rest starts the next MAT frame
		m_burstPts = m_pFmtCtx->streams[0]->codecpar->codec_id == AV_CODEC_ID_TRUEHD ? avpkt->pts : AV_NOPTS_VALUE;
	}

	return m_output;
}
