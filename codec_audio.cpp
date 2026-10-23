// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file codec_audio.cpp
 * Audio Decoder
 *
 * This file defines cAudioDecoder, which has all the functions
 * to decode audio data. It's the audio interface to ffmpeg.
 *
 * @copyright 2009 - 2015 by Johns.  All Rights Reserved.
 * @copyright 2018 by zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "audio.h"
#include "codec_audio.h"
#include "config.h"
#include "logger.h"
#include "spdif.h"
#include "misc.h"

/**
 * Build a passthrough mask logging string
 *
 * @param mask    passthrough bitmask
 *
 * @return a string like " (passthrough mask: AC3 E-AC-3 DTS TrueHD DTS-HD)"
 */
static std::string GetPassthroughMaskString(int mask)
{
	std::string passthrough = "";

	if (mask) {
		passthrough += " (passthrough mask:";
		if (mask & CODEC_AC3)    passthrough += " AC3";
		if (mask & CODEC_EAC3)   passthrough += " E-AC-3";
		if (mask & CODEC_DTS)    passthrough += " DTS";
		if (mask & CODEC_TRUEHD) passthrough += " TrueHD";
		if (mask & CODEC_DTSHD)  passthrough += " DTS-HD";
		passthrough += ")";
	}

	return passthrough;
}

/**
 * Create a new audio decoder for the given audio context
 *
 * @param audio    audio context
 */
cAudioDecoder::cAudioDecoder(cAudioStream *audio)
	: m_pAudio(audio),
	  m_passthroughMask(m_pAudio->GetPassthroughMask() & CODEC_PASSTHROUGH_ALL)
{
	if (!(m_pFrame = av_frame_alloc()))
		LOGFATAL("audiocodec: %s: can't allocate audio decoder frame buffer", __FUNCTION__);

	LOGDEBUG2(L_CODEC, "audiocodec: create %s%s", __FUNCTION__, GetPassthroughMaskString(m_passthroughMask).c_str());
}

cAudioDecoder::~cAudioDecoder(void)
{
	Close();

	av_frame_free(&m_pFrame);
}

/**
 * Open and initiate the audio decoder
 *
 * @param codecId       audio codec id
 * @param par           audio codec parameters
 * @param timebase      timebase
 *
 * @todo FIXME: errors shouldn't be fatal, maybe just disable audio
 */
void cAudioDecoder::Open(AVCodecID codecId, AVCodecParameters *par, AVRational timebase)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	// the mediaplayer opens the decoder for every file and audio track without closing it
	CloseCodec();

	const AVCodec *codec;

	m_codecId = codecId;

	// the sink may have been switched on or off meanwhile (automatic passthrough)
	m_passthroughMask = m_pAudio->GetPassthroughMask() & CODEC_PASSTHROUGH_ALL;

	switch (codecId) {
	case AV_CODEC_ID_AC3:
		if (!(codec = avcodec_find_decoder_by_name("ac3_fixed")))
			LOGFATAL("audiocodec: %s: codec ac3_fixed ID %#06x not found", __FUNCTION__, codecId);
		break;
	case AV_CODEC_ID_AAC:
		if (!(codec = avcodec_find_decoder_by_name("aac_fixed")))
			LOGFATAL("audiocodec: %s: codec aac_fixed ID %#06x not found", __FUNCTION__, codecId);
		break;
	default:
		if (!(codec = avcodec_find_decoder(codecId))) {
			LOGFATAL("audiocodec: %s: codec %s ID %#06x not found", __FUNCTION__,
			avcodec_get_name(codecId), codecId);
		}
		break;
	}

	if (!(m_pAudioCtx = avcodec_alloc_context3(codec)))
		LOGFATAL("audiocodec: %s: can't allocate audio codec context", __FUNCTION__);

	m_pAudioCtx->pkt_timebase = timebase;

	if (par && ((avcodec_parameters_to_context(m_pAudioCtx, par)) < 0))
		LOGERROR("audiocodec: %s: insert parameters to context failed!", __FUNCTION__);

	if (avcodec_open2(m_pAudioCtx, m_pAudioCtx->codec, NULL) < 0)
		LOGFATAL("audiocodec: %s: can't open audio codec", __FUNCTION__);

	LOGDEBUG2(L_CODEC, "audiocodec: %s: Codec %s found%s", __FUNCTION__, m_pAudioCtx->codec->long_name, GetPassthroughMaskString(m_passthroughMask).c_str());

	m_currentSampleRate = 0;
	m_currentHwSampleRate = 0;
	m_currentNumChannels = 0;
	m_currentHwNumChannels = 0;
	m_currentPassthroughMask = 0;
	m_currentPassthrough = false;
	m_currentDtshdRate = 0;
	m_passthroughFailed = false;
	m_dtshdFailed = false;
}

/**
 * Close the audio decoder
 */
void cAudioDecoder::Close(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	CloseCodec();
}

/**
 * Close the codec and the spdif muxer
 *
 * Must be called with the decoder mutex.
 */
void cAudioDecoder::CloseCodec(void)
{
	if (!m_pAudioCtx)
		return;

	m_spdif.Close();

	LOGDEBUG2(L_CODEC, "audiocodec: %s", __FUNCTION__);

	avcodec_free_context(&m_pAudioCtx);

	m_codecId = AV_CODEC_ID_NONE;
	m_lastPts = AV_NOPTS_VALUE;
	m_streamInfo = {};
	m_pAudio->Config()->SetAudioStreamInfo({});
}

/**
 * Test, if passthrough audio should be tried
 *
 * To enable passthrough, the current codec must be enabled in the setup
 *
 * Currently supported: AC3, E-AC-3, DTS, TrueHD, DTS-HD (Master Audio and High Resolution Audio)
 *
 * @return             true, if the data should be passed through
 */
bool cAudioDecoder::ShouldTryPassthrough(void)
{
	if (m_passthroughFailed)
		return false;

	return (m_passthroughMask & CODEC_AC3    && m_pAudioCtx->codec_id == AV_CODEC_ID_AC3) ||
	       (m_passthroughMask & CODEC_EAC3   && m_pAudioCtx->codec_id == AV_CODEC_ID_EAC3) ||
	       (m_passthroughMask & CODEC_DTS    && m_pAudioCtx->codec_id == AV_CODEC_ID_DTS) ||
	       (m_passthroughMask & CODEC_DTSHD  && m_pAudioCtx->codec_id == AV_CODEC_ID_DTS &&
	        IsDtsHdProfile(m_pAudioCtx->profile) && !m_dtshdFailed) ||
	       (m_passthroughMask & CODEC_TRUEHD && m_pAudioCtx->codec_id == AV_CODEC_ID_TRUEHD);
}

/**
 * Handle audio format changes and setup audio, if format changed
 *
 * @retval 0     if new audio was correctly set up,
 *               otherwise return value of cAudioStream::Setup()
 */
int cAudioDecoder::CheckUpdateFormat(bool passthrough)
{
	if (m_currentPassthrough     == passthrough &&
	    m_currentPassthroughMask == m_passthroughMask &&
	    m_currentNumChannels     == m_currentHwNumChannels &&
	    m_currentSampleRate      == m_currentHwSampleRate)
		return 0;

	m_currentHwSampleRate = m_pAudioCtx->sample_rate;
	m_currentHwNumChannels = m_pAudioCtx->ch_layout.nb_channels;

	if (passthrough) {
		m_spdif.Close();
		// The bitstream is sent with the sample rate of the stream. It is only known
		// from the codec parameters (mediaplayer), the PES data isn't decoded.
		// The DTS-HD profile is also only known from the codec parameters.
		bool dtshd = (m_passthroughMask & CODEC_DTSHD) && !m_dtshdFailed;
		sPassthroughFormat format = GetPassthroughFormat(m_pAudioCtx->codec_id, m_pAudioCtx->sample_rate, m_pAudioCtx->profile, dtshd);
		m_currentHwSampleRate = format.sampleRate;
		m_currentHwNumChannels = format.channels;
		m_currentDtshdRate = format.dtshdRate;
	}

	int err = 0;
	if ((err = m_pAudio->Setup(m_pAudioCtx->pkt_timebase, m_currentHwSampleRate, m_currentHwNumChannels, passthrough)) < 0) {
		LOGERROR("audiocodec: %s: format change update error", __FUNCTION__);
		m_currentHwSampleRate = 0;
		m_currentHwNumChannels = 0;
		m_currentSampleRate = -1; // set up again with the next call
		return err;
	}

	// remember for next update check
	m_currentSampleRate = m_currentHwSampleRate;
	m_currentNumChannels = m_currentHwNumChannels;
	m_currentPassthroughMask = m_passthroughMask;
	m_currentPassthrough = passthrough;

	UpdateStreamInfo(passthrough, m_currentHwSampleRate, m_currentHwNumChannels);

	// audio was newly set up (err == 0)
	if (!err)
		LOGDEBUG2(L_SOUND, "audiocodec: %s: format changed %s %dHz *%d channels%s", __FUNCTION__,
			av_get_sample_fmt_name(m_pAudioCtx->sample_fmt), m_currentHwSampleRate, m_currentHwNumChannels,
			GetPassthroughMaskString(m_passthroughMask).c_str());

	return 0;
}

/**
 * Publish the properties of the played stream for the statistics, if they changed
 *
 * @param passthrough        true, if the stream is passed through
 * @param outputSampleRate   sample rate of the audio device
 * @param outputChannels     channels of the audio device
 */
void cAudioDecoder::UpdateStreamInfo(bool passthrough, int outputSampleRate, int outputChannels)
{
	const char *codec = avcodec_get_name(m_pAudioCtx->codec_id);
	const char *profile = avcodec_profile_name(m_pAudioCtx->codec_id, m_pAudioCtx->profile);

	// called with every decoded frame, so compare without copying the strings
	if (m_streamInfo.codec == codec && m_streamInfo.profile == (profile ? profile : "") &&
	    m_streamInfo.sampleRate == m_pAudioCtx->sample_rate &&
	    m_streamInfo.channels == m_pAudioCtx->ch_layout.nb_channels &&
	    m_streamInfo.passthrough == passthrough &&
	    m_streamInfo.outputSampleRate == outputSampleRate &&
	    m_streamInfo.outputChannels == outputChannels &&
	    m_streamInfo.device == m_pAudio->GetDeviceName())
		return;

	m_streamInfo.codec = codec;
	m_streamInfo.profile = profile ? profile : "";
	m_streamInfo.sampleRate = m_pAudioCtx->sample_rate;
	m_streamInfo.channels = m_pAudioCtx->ch_layout.nb_channels;
	m_streamInfo.passthrough = passthrough;
	m_streamInfo.outputSampleRate = outputSampleRate;
	m_streamInfo.outputChannels = outputChannels;
	m_streamInfo.device = m_pAudio->GetDeviceName();
	m_pAudio->Config()->SetAudioStreamInfo(m_streamInfo);
}

/**
 * Forward an audio packet either to the decoder or passthrough
 *
 * @param avpkt        audio packet to decode
 */
void cAudioDecoder::Decode(const AVPacket *avpkt)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (ShouldTryPassthrough())
		Passthrough(avpkt);
	else
		DecodePCM(avpkt);
}

/**
 * Passthrough audio data
 *
 * Build spdif headers depending on the codec and send the
 * data to the audio device.
 *
 * @param avpkt         undecoded audio packet
 *
 * @retval 0            codec is not supported for passthrough, sth. went wrong or we need more data to finish a spdif burst packet
 * @retval 1            spdif burst was enqueued
 */
int cAudioDecoder::Passthrough(const AVPacket *avpkt)
{
	int err = CheckUpdateFormat(true);

	// e.g. the device doesn't offer 8 channels with 192kHz
	if (err && m_currentDtshdRate && (m_passthroughMask & CODEC_DTS)) {
		LOGWARNING("audiocodec: %s: passthrough of DTS-HD is not possible, pass the DTS core through", __FUNCTION__);
		m_dtshdFailed = true;
		err = CheckUpdateFormat(true);
	}

	if (err) {
		// e.g. the device doesn't offer the needed sample rate (yet)
		LOGWARNING("audiocodec: %s: passthrough of %s is not possible, decode it until the next codec change", __FUNCTION__, avcodec_get_name(m_pAudioCtx->codec_id));
		m_passthroughFailed = true;
		m_spdif.Close();
		DecodePCM(avpkt);
		return 0;
	}

	if (!m_spdif.IsOpen() && !m_spdif.Open(m_pAudioCtx->codec_id, m_currentHwSampleRate, m_currentDtshdRate)) {
		LOGERROR("audiocodec: %s: failed to open spdif muxer for codec %s @ %dHz!", __FUNCTION__, avcodec_get_name(m_pAudioCtx->codec_id), m_currentHwSampleRate);
		return 0;
	}

	m_pAudio->SetTimebase(m_pAudioCtx->pkt_timebase);

	int64_t pts = AV_NOPTS_VALUE;
	const auto &burst = m_spdif.Mux(avpkt, &pts);

	if (burst.empty())
		return 0;

	m_pAudio->EnqueueSpdif(reinterpret_cast<const uint16_t *>(burst.data()), burst.size(), pts);
	return 1;
}

/**
 * Decode an audio packet
 *
 * @param avpkt        audio packet to decode
 */
void cAudioDecoder::DecodePCM(const AVPacket *avpkt)
{
	int retSend, retRec;
	AVFrame *frame;

	frame = m_pFrame;
	av_frame_unref(frame);

	do {
		retSend = avcodec_send_packet(m_pAudioCtx, avpkt);
		if (retSend < 0 && retSend != AVERROR(EAGAIN))
			LOGERROR("audiocodec: %s: avcodec_send_packet error: %s", __FUNCTION__, av_err2str(retSend));

		do {
			retRec = avcodec_receive_frame(m_pAudioCtx, frame);

			if (retRec < 0) {
				if (retRec != AVERROR(EAGAIN))
					LOGERROR("audiocodec: %s: avcodec_receive_frame error: %s", __FUNCTION__, av_err2str(retRec));
				continue;
			}

			if (m_lastPts == AV_NOPTS_VALUE && avpkt->pts == AV_NOPTS_VALUE) {
				// the first AVPacket has no valid PTS, if its PES packet has been truncated while searching for the sync word
				av_frame_unref(frame);
				continue;
			}

			// update audio clock and remember last PTS or guess the next PTS
			if (frame->pts != AV_NOPTS_VALUE) {
				m_lastPts = frame->pts;
			} else if (m_lastPts != AV_NOPTS_VALUE) {
				frame->pts = m_lastPts +
					av_rescale_q(frame->nb_samples, (AVRational){1, frame->sample_rate}, m_pAudioCtx->pkt_timebase);
				m_lastPts = frame->pts;
			}

			if (CheckUpdateFormat(false)) {
				LOGERROR("audiocodec: %s: unsupported format!", __FUNCTION__);
				av_frame_unref(frame);
				return;
			}

			m_pAudio->Filter(frame, m_pAudioCtx);

			// The audio device of a decoded stream is set up by the filter graph
			// (cAudioStream::InitFilter()), CheckUpdateFormat() only sets it up after
			// a passthrough. So publish the stream for the statistics here.
			UpdateStreamInfo(false, m_pAudio->GetHwSampleRate(), m_pAudio->GetHwNumChannels());

		} while (retRec == 0);
	} while (retSend == AVERROR(EAGAIN));
}

/**
 * Flush the audio decoder buffers
 *
 * Also resets the last PTS and Codec ID
 */
void cAudioDecoder::FlushBuffers(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	LOGDEBUG2(L_CODEC, "audiocodec: %s", __FUNCTION__);
	if (m_pAudioCtx)
		avcodec_flush_buffers(m_pAudioCtx);

	// the muxer may hold data of a burst, which isn't complete yet (e.g. TrueHD)
	m_spdif.Close();

	m_lastPts = AV_NOPTS_VALUE;
	m_codecId = AV_CODEC_ID_NONE;
}

/**
 * Try the passthrough again
 *
 * If the passthrough of a codec failed (e.g. the receiver was off), the codec is
 * decoded until the next codec change. This allows a new try with the next packet.
 * With the automatic passthrough, the codecs of the sink are read again.
 */
void cAudioDecoder::RetryPassthrough(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_passthroughFailed || m_dtshdFailed)
		LOGDEBUG2(L_SOUND, "audiocodec: %s: try the passthrough again", __FUNCTION__);

	m_passthroughMask = m_pAudio->GetPassthroughMask() & CODEC_PASSTHROUGH_ALL;

	m_passthroughFailed = false;
	m_dtshdFailed = false;
}

/**
 * Update the codecs, which are passed through, from the audio stream
 *
 * Called after the passthrough setting of the audio stream was changed.
 */
void cAudioDecoder::UpdatePassthroughMask(void)
{
	m_passthroughMask = m_pAudio->GetPassthroughMask() & CODEC_PASSTHROUGH_ALL;
	m_passthroughFailed = false; // try again with the new settings
	m_dtshdFailed = false;
}
