// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file test_ac3encoder.cpp
 * Unit tests for cAc3Encoder
 *
 * Synthetic multichannel frames are encoded, the packets are checked and
 * decoded again.
 */

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

#include "ac3encoder.h"

namespace {

constexpr AVRational TIMEBASE = { 1, 90000 };
constexpr int SAMPLE_RATE = 48000;
constexpr int AC3_FRAME_SAMPLES = 1536;
constexpr int64_t AC3_FRAME_TICKS = AC3_FRAME_SAMPLES * 90000 / SAMPLE_RATE;   // 2880

/**
 * Create a frame with a sine on every channel
 */
AVFrame *MakeFrame(int channels, AVSampleFormat format, int samples, int64_t pts, int sampleRate = SAMPLE_RATE)
{
	AVFrame *frame = av_frame_alloc();
	frame->format = format;
	frame->sample_rate = sampleRate;
	frame->nb_samples = samples;
	frame->pts = pts;
	av_channel_layout_default(&frame->ch_layout, channels);
	REQUIRE(av_frame_get_buffer(frame, 0) == 0);

	for (int c = 0; c < channels; c++) {
		for (int i = 0; i < samples; i++) {
			double value = 0.25 * std::sin(2 * M_PI * (440 + 110 * c) * i / SAMPLE_RATE);
			if (format == AV_SAMPLE_FMT_FLTP)
				reinterpret_cast<float *>(frame->extended_data[c])[i] = static_cast<float>(value);
			else if (format == AV_SAMPLE_FMT_S16)
				reinterpret_cast<int16_t *>(frame->data[0])[i * channels + c] = static_cast<int16_t>(value * 32767);
		}
	}

	return frame;
}

/** Encoded packets */
struct sPackets {
	std::vector<int64_t> pts;
	std::vector<std::vector<uint8_t>> data;
};

/**
 * Encode frames of a number of samples, starting with a pts
 */
void EncodeFrames(cAc3Encoder &encoder, sPackets &packets, int channels, AVSampleFormat format,
	int frames, int samples, int64_t pts, int sampleRate = SAMPLE_RATE)
{
	for (int n = 0; n < frames; n++) {
		AVFrame *frame = MakeFrame(channels, format, samples, pts + n * samples * 90000LL / sampleRate, sampleRate);
		REQUIRE(encoder.Encode(frame, [&packets](const AVPacket *pkt) {
			packets.pts.push_back(pkt->pts);
			packets.data.emplace_back(pkt->data, pkt->data + pkt->size);
		}));
		av_frame_free(&frame);
	}
}

/**
 * Decode an AC-3 packet and return the number of channels (0 on an error)
 */
int DecodedChannels(const std::vector<uint8_t> &data)
{
	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AC3);
	AVCodecContext *ctx = avcodec_alloc_context3(codec);
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	int channels = 0;

	if (avcodec_open2(ctx, codec, nullptr) == 0 && av_new_packet(pkt, data.size()) == 0) {
		std::copy(data.begin(), data.end(), pkt->data);
		if (avcodec_send_packet(ctx, pkt) == 0 && avcodec_receive_frame(ctx, frame) == 0 && frame->nb_samples == AC3_FRAME_SAMPLES)
			channels = frame->ch_layout.nb_channels;
	}

	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&ctx);
	return channels;
}

}

TEST_CASE("cAc3Encoder", "[ac3encoder]") {
	if (!avcodec_find_encoder(AV_CODEC_ID_AC3))
		SKIP("FFmpeg has no AC-3 encoder");

	cAc3Encoder encoder;
	sPackets packets;

	SECTION("only the sample rates of AC-3 can be encoded") {
		REQUIRE(cAc3Encoder::CanEncode(48000));
		REQUIRE(cAc3Encoder::CanEncode(44100));
		REQUIRE(cAc3Encoder::CanEncode(32000));
		REQUIRE_FALSE(cAc3Encoder::CanEncode(96000));
		REQUIRE_FALSE(encoder.Open(96000, TIMEBASE));
		REQUIRE_FALSE(encoder.IsOpen());
	}

	SECTION("5.1 frames of another size are encoded into AC-3 frames") {
		REQUIRE(encoder.Open(SAMPLE_RATE, TIMEBASE));
		REQUIRE(encoder.GetSampleRate() == SAMPLE_RATE);

		// 30 AAC frames of 1024 samples = 20 AC-3 frames
		EncodeFrames(encoder, packets, 6, AV_SAMPLE_FMT_FLTP, 30, 1024, 900000);

		// the encoder delays one frame
		REQUIRE(packets.pts.size() >= 18);
		REQUIRE(packets.pts.size() <= 20);
		for (size_t i = 0; i < packets.data.size(); i++)
			REQUIRE(packets.data[i].size() == 640000 / 8 * AC3_FRAME_SAMPLES / SAMPLE_RATE);   // 2560 bytes
		for (size_t i = 1; i < packets.pts.size(); i++)
			REQUIRE(packets.pts[i] - packets.pts[i - 1] == AC3_FRAME_TICKS);

		// the pts of the first packet is the pts of the first sample (minus the encoder padding)
		REQUIRE(packets.pts[0] <= 900000);
		REQUIRE(packets.pts[0] >= 900000 - AC3_FRAME_TICKS);

		REQUIRE(DecodedChannels(packets.data[0]) == 6);
	}

	SECTION("7.1 is downmixed to 5.1, interleaved samples are converted") {
		REQUIRE(encoder.Open(SAMPLE_RATE, TIMEBASE));
		EncodeFrames(encoder, packets, 8, AV_SAMPLE_FMT_FLTP, 10, 1536, 0);
		EncodeFrames(encoder, packets, 3, AV_SAMPLE_FMT_S16, 10, 1536, 10 * AC3_FRAME_TICKS);

		REQUIRE(packets.pts.size() >= 18);
		REQUIRE(DecodedChannels(packets.data.front()) == 6);
		REQUIRE(DecodedChannels(packets.data.back()) == 6);
		for (size_t i = 1; i < packets.pts.size(); i++)
			REQUIRE(packets.pts[i] - packets.pts[i - 1] == AC3_FRAME_TICKS);
	}

	SECTION("a pts jump starts at the new pts") {
		REQUIRE(encoder.Open(SAMPLE_RATE, TIMEBASE));
		EncodeFrames(encoder, packets, 6, AV_SAMPLE_FMT_FLTP, 5, 1000, 0);
		size_t before = packets.pts.size();

		EncodeFrames(encoder, packets, 6, AV_SAMPLE_FMT_FLTP, 10, 1536, 9000000);
		REQUIRE(packets.pts.size() > before + 5);
		REQUIRE(packets.pts.back() > 9000000);
		REQUIRE(packets.pts.back() - packets.pts[packets.pts.size() - 2] == AC3_FRAME_TICKS);
	}

	SECTION("a flush drops the collected samples") {
		REQUIRE(encoder.Open(SAMPLE_RATE, TIMEBASE));
		EncodeFrames(encoder, packets, 6, AV_SAMPLE_FMT_FLTP, 1, 1000, 0);
		encoder.Flush();
		packets = {};

		EncodeFrames(encoder, packets, 6, AV_SAMPLE_FMT_FLTP, 4, 1536, 450000);
		REQUIRE_FALSE(packets.pts.empty());
		REQUIRE(packets.pts[0] <= 450000);
		REQUIRE(packets.pts[0] >= 450000 - AC3_FRAME_TICKS);
	}

	SECTION("the pts don't drift, if a frame isn't a whole number of ticks") {
		// 1536 samples at 44.1 kHz are 3134.69 ticks
		REQUIRE(encoder.Open(44100, TIMEBASE));
		EncodeFrames(encoder, packets, 6, AV_SAMPLE_FMT_FLTP, 600, 1152, 0, 44100);

		REQUIRE(packets.pts.size() >= 400);
		for (size_t i = 1; i < packets.pts.size(); i++) {
			int64_t exact = packets.pts[0] + static_cast<int64_t>(i) * AC3_FRAME_SAMPLES * 90000 / 44100;
			REQUIRE(std::abs(packets.pts[i] - exact) <= 1);
		}
	}

	SECTION("a frame of another sample rate isn't encoded") {
		REQUIRE(encoder.Open(SAMPLE_RATE, TIMEBASE));
		AVFrame *frame = MakeFrame(6, AV_SAMPLE_FMT_FLTP, 1024, 0);
		frame->sample_rate = 44100;
		REQUIRE_FALSE(encoder.Encode(frame, [](const AVPacket *) {}));
		av_frame_free(&frame);
	}
}
