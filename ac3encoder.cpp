// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ac3encoder.cpp
 * AC-3 Encoder
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <cstdlib>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

#include "ac3encoder.h"
#include "logger.h"
#include "misc.h"

/**
 * Check, if a sample rate can be encoded to AC-3
 *
 * @param sampleRate    sample rate in Hz
 */
bool cAc3Encoder::CanEncode(int sampleRate)
{
	return sampleRate == 48000 || sampleRate == 44100 || sampleRate == 32000;
}

/**
 * Open the encoder
 *
 * @param sampleRate    sample rate of the audio
 * @param timebase      timebase of the pts values of the frames and packets
 *
 * @return true, if the encoder was opened
 */
bool cAc3Encoder::Open(int sampleRate, AVRational timebase)
{
	Close();

	if (!CanEncode(sampleRate)) {
		LOGWARNING("ac3encoder: %s: AC-3 doesn't support %d Hz", __FUNCTION__, sampleRate);
		return false;
	}

	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
	if (!codec) {
		LOGWARNING("ac3encoder: %s: FFmpeg has no AC-3 encoder", __FUNCTION__);
		return false;
	}

	m_pCtx = avcodec_alloc_context3(codec);
	m_pFrame = av_frame_alloc();
	m_pPacket = av_packet_alloc();
	if (!m_pCtx || !m_pFrame || !m_pPacket) {
		LOGERROR("ac3encoder: %s: out of memory", __FUNCTION__);
		Close();
		return false;
	}

	m_pCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	m_pCtx->sample_rate = sampleRate;
	m_pCtx->bit_rate = BIT_RATE;
	m_pCtx->time_base = timebase;
	av_channel_layout_default(&m_pCtx->ch_layout, 6);

	int err = avcodec_open2(m_pCtx, codec, nullptr);
	if (err < 0) {
		LOGERROR("ac3encoder: %s: can't open the encoder: %s", __FUNCTION__, av_err2str(err));
		Close();
		return false;
	}

	m_pFifo = av_audio_fifo_alloc(m_pCtx->sample_fmt, m_pCtx->ch_layout.nb_channels, m_pCtx->frame_size * 2);
	if (!m_pFifo) {
		LOGERROR("ac3encoder: %s: can't allocate the fifo", __FUNCTION__);
		Close();
		return false;
	}

	LOGDEBUG2(L_SOUND, "ac3encoder: %s: %d Hz, %d kbit/s", __FUNCTION__, sampleRate, BIT_RATE / 1000);

	return true;
}

/**
 * Close the encoder
 */
void cAc3Encoder::Close(void)
{
	avcodec_free_context(&m_pCtx);
	swr_free(&m_pSwr);
	av_channel_layout_uninit(&m_swrInLayout);
	m_swrInFormat = AV_SAMPLE_FMT_NONE;
	if (m_pFifo) {
		av_audio_fifo_free(m_pFifo);
		m_pFifo = nullptr;
	}
	av_frame_free(&m_pFrame);
	av_packet_free(&m_pPacket);
	m_basePts = AV_NOPTS_VALUE;
	m_encodedSamples = 0;
}

/**
 * Drop the collected samples and the delayed packets (e.g. on a channel switch)
 */
void cAc3Encoder::Flush(void)
{
	if (!m_pCtx)
		return;

	avcodec_flush_buffers(m_pCtx);
	Reset(AV_NOPTS_VALUE);
	if (m_pSwr)
		swr_init(m_pSwr);   // drops the buffered samples
}

/**
 * Convert a number of samples into the timebase of the pts values
 */
int64_t cAc3Encoder::SamplesToPts(int64_t samples) const
{
	return av_rescale_q(samples, AVRational{1, m_pCtx->sample_rate}, m_pCtx->time_base);
}

/**
 * Drop the collected samples and count the samples from a new pts
 *
 * The pts of the samples are computed from the number of samples since this
 * pts, so the rounding doesn't add up (e.g. 1536 samples at 44.1 kHz).
 *
 * @param pts       pts of the next sample (AV_NOPTS_VALUE: not known yet)
 */
void cAc3Encoder::Reset(int64_t pts)
{
	av_audio_fifo_reset(m_pFifo);
	m_basePts = pts;
	m_encodedSamples = 0;
}

/**
 * Set up the converter for the format of a frame
 *
 * @param frame     decoded frame
 *
 * @return true, if the converter is ready
 */
bool cAc3Encoder::SetupConverter(const AVFrame *frame)
{
	if (m_pSwr && m_swrInFormat == frame->format && !av_channel_layout_compare(&m_swrInLayout, &frame->ch_layout))
		return true;

	swr_free(&m_pSwr);
	av_channel_layout_uninit(&m_swrInLayout);

	int err = swr_alloc_set_opts2(&m_pSwr, &m_pCtx->ch_layout, m_pCtx->sample_fmt, m_pCtx->sample_rate,
		&frame->ch_layout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr);
	if (err < 0 || (err = swr_init(m_pSwr)) < 0) {
		LOGERROR("ac3encoder: %s: can't set up the converter: %s", __FUNCTION__, av_err2str(err));
		swr_free(&m_pSwr);
		return false;
	}

	av_channel_layout_copy(&m_swrInLayout, &frame->ch_layout);
	m_swrInFormat = frame->format;

	char layout[64];
	av_channel_layout_describe(&frame->ch_layout, layout, sizeof(layout));
	LOGDEBUG2(L_SOUND, "ac3encoder: %s: convert %s %s to 5.1", __FUNCTION__, layout,
		av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)));

	return true;
}

/**
 * Encode a decoded frame
 *
 * The samples are collected, until an AC-3 frame is complete. Each encoded
 * packet is passed to the output function, its pts is the pts of its first
 * sample.
 *
 * @param frame     decoded frame (the sample rate of the encoder)
 * @param output    function, which gets the encoded packets
 *
 * @return false on an error
 */
bool cAc3Encoder::Encode(const AVFrame *frame, const std::function<void(const AVPacket *)> &output)
{
	if (!m_pCtx || !frame || frame->sample_rate != m_pCtx->sample_rate || !SetupConverter(frame))
		return false;

	// a pts jump (e.g. a new stream): drop the collected samples and start at the new pts
	if (frame->pts != AV_NOPTS_VALUE && m_basePts != AV_NOPTS_VALUE) {
		int64_t expected = m_basePts + SamplesToPts(m_encodedSamples + av_audio_fifo_size(m_pFifo));
		int64_t maxDeviation = av_rescale_q(MAX_PTS_DEVIATION_MS, AVRational{1, 1000}, m_pCtx->time_base);
		if (std::abs(frame->pts - expected) > maxDeviation) {
			LOGDEBUG2(L_SOUND, "ac3encoder: %s: pts jump, drop %d samples", __FUNCTION__, av_audio_fifo_size(m_pFifo));
			Reset(frame->pts);
		}
	}

	if (m_basePts == AV_NOPTS_VALUE) {
		if (frame->pts == AV_NOPTS_VALUE)
			return true;  // drop the samples, until a pts is known
		Reset(frame->pts);
	}

	// convert into the fifo (the sample rate isn't changed, so the number of samples stays the same)
	int samples = swr_get_out_samples(m_pSwr, frame->nb_samples);
	if (av_audio_fifo_realloc(m_pFifo, av_audio_fifo_size(m_pFifo) + samples) < 0) {
		LOGERROR("ac3encoder: %s: can't grow the fifo", __FUNCTION__);
		return false;
	}

	AVFrame *converted = av_frame_alloc();
	if (!converted) {
		LOGERROR("ac3encoder: %s: out of memory", __FUNCTION__);
		return false;
	}
	converted->format = m_pCtx->sample_fmt;
	converted->sample_rate = m_pCtx->sample_rate;
	converted->nb_samples = samples;
	av_channel_layout_copy(&converted->ch_layout, &m_pCtx->ch_layout);

	int err = av_frame_get_buffer(converted, 0);
	if (err >= 0)
		err = swr_convert(m_pSwr, converted->data, samples, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
	if (err < 0) {
		LOGERROR("ac3encoder: %s: can't convert the frame: %s", __FUNCTION__, av_err2str(err));
		av_frame_free(&converted);
		return false;
	}

	av_audio_fifo_write(m_pFifo, reinterpret_cast<void **>(converted->data), err);
	av_frame_free(&converted);

	return EncodeFrames(output);
}

/**
 * Encode the complete AC-3 frames of the fifo
 *
 * @param output    function, which gets the encoded packets
 *
 * @return false on an error
 */
bool cAc3Encoder::EncodeFrames(const std::function<void(const AVPacket *)> &output)
{
	const int frameSize = m_pCtx->frame_size;

	while (av_audio_fifo_size(m_pFifo) >= frameSize) {
		av_frame_unref(m_pFrame);
		m_pFrame->format = m_pCtx->sample_fmt;
		m_pFrame->sample_rate = m_pCtx->sample_rate;
		m_pFrame->nb_samples = frameSize;
		av_channel_layout_copy(&m_pFrame->ch_layout, &m_pCtx->ch_layout);

		int err = av_frame_get_buffer(m_pFrame, 0);
		if (err < 0) {
			LOGERROR("ac3encoder: %s: can't allocate the frame: %s", __FUNCTION__, av_err2str(err));
			return false;
		}

		av_audio_fifo_read(m_pFifo, reinterpret_cast<void **>(m_pFrame->data), frameSize);
		m_pFrame->pts = m_basePts + SamplesToPts(m_encodedSamples);
		m_encodedSamples += frameSize;

		if ((err = avcodec_send_frame(m_pCtx, m_pFrame)) < 0) {
			LOGERROR("ac3encoder: %s: can't encode the frame: %s", __FUNCTION__, av_err2str(err));
			return false;
		}

		while ((err = avcodec_receive_packet(m_pCtx, m_pPacket)) >= 0) {
			output(m_pPacket);
			av_packet_unref(m_pPacket);
		}

		if (err != AVERROR(EAGAIN)) {
			LOGERROR("ac3encoder: %s: can't receive the packet: %s", __FUNCTION__, av_err2str(err));
			return false;
		}
	}

	return true;
}
