/**
 * @file test_audiostreams.cpp
 * Unit tests with real audio streams: PES reassembly and spdif passthrough
 *
 * The streams in tests/data were encoded with FFmpeg (a 440Hz sine), see the
 * README.md there.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <endian.h>

#include <alsa/asoundlib.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "../codec_audio.h"
#include "../pes.h"
#include "../spdif.h"

// FFmpeg 6.1 renamed the profiles from FF_PROFILE_* to AV_PROFILE_*
#ifdef AV_PROFILE_DTS_HD_MA
#define DTS_PROFILE(name) AV_PROFILE_##name
#else
#define DTS_PROFILE(name) FF_PROFILE_##name
#endif

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "data"
#endif

namespace {

/** IEC 61937 data types (Pc) */
enum {
    PC_AC3 = 0x01,
    PC_DTS1 = 0x0B,
    PC_DTSHD = 0x11,
    PC_EAC3 = 0x15,
    PC_TRUEHD = 0x16,
};

std::vector<uint8_t> ReadFile(const std::string &name)
{
    std::ifstream file(std::string(TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(file.good());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

AVPacket *MakePacket(const uint8_t *data, int size, int64_t pts)
{
    AVPacket *pkt = av_packet_alloc();
    REQUIRE(av_new_packet(pkt, size) == 0);
    memcpy(pkt->data, data, size);
    pkt->pts = pts;
    return pkt;
}

/**
 * Feed an elementary stream like VDR's PES packets into the reassembly buffer
 *
 * The stream is pushed in TS payload sized chunks, every 10th chunk carries a pts.
 *
 * @param data          the stream
 * @param[out] packets  the reassembled frames
 * @return              number of bytes left in the buffer
 */
size_t Reassemble(const std::vector<uint8_t> &data, std::vector<AVPacket *> &packets)
{
    cReassemblyBufferAudio buffer;
    constexpr size_t CHUNK_SIZE = 184;

    for (size_t pos = 0; pos < data.size(); pos += CHUNK_SIZE) {
        size_t chunk = std::min(CHUNK_SIZE, data.size() - pos);
        int64_t pts = (pos / CHUNK_SIZE) % 10 ? AV_NOPTS_VALUE : (int64_t)pos;
        buffer.Push(&data[pos], chunk, pts);

        while (AVPacket *pkt = buffer.PopAvPacket())
            packets.push_back(pkt);
    }

    return buffer.GetSize();
}

/**
 * Check the reassembly of a stream
 *
 * All frames except the last one (it has no following sync word) are popped, each
 * frame starts with the sync word of the codec. If FFmpeg has a decoder for the codec,
 * each frame must be decodable.
 *
 * @param file           stream file
 * @param codec          codec of the stream
 * @param frameSize      size of all frames (0: variable)
 */
void CheckReassembly(const std::string &file, AVCodecID codec, int frameSize)
{
    std::vector<uint8_t> data = ReadFile(file);
    std::vector<AVPacket *> packets;
    cReassemblyBufferAudio detector;

    size_t left = Reassemble(data, packets);

    REQUIRE(packets.size() >= 5);

    size_t popped = 0;
    for (AVPacket *pkt : packets) {
        REQUIRE(detector.DetectCodecFromSyncWord(pkt->data, pkt->size) == codec);
        if (frameSize)
            REQUIRE(pkt->size == frameSize);
        popped += pkt->size;
    }

    // nothing is lost, only the last frame stays in the buffer
    REQUIRE(popped + left == data.size());
    REQUIRE((int)left == detector.GetFrameSizeForCodec(codec, &data[popped]));

    // the first frame has the pts of its PES packet
    REQUIRE(packets[0]->pts == 0);

    const AVCodec *decoder = avcodec_find_decoder(codec);
    if (decoder) {
        AVCodecContext *ctx = avcodec_alloc_context3(decoder);
        REQUIRE(avcodec_open2(ctx, decoder, nullptr) == 0);
        AVFrame *frame = av_frame_alloc();
        int decoded = 0;

        for (AVPacket *pkt : packets) {
            REQUIRE(avcodec_send_packet(ctx, pkt) == 0);
            while (avcodec_receive_frame(ctx, frame) == 0)
                decoded++;
        }

        // decoders may delay the first frame
        REQUIRE(decoded >= (int)packets.size() - 2);

        av_frame_free(&frame);
        avcodec_free_context(&ctx);
    }

    for (AVPacket *pkt : packets)
        av_packet_free(&pkt);
}

/** a burst of the spdif muxer with its pts */
struct sBurst {
    std::vector<uint8_t> data;
    int64_t pts;
};

/**
 * Pass packets through the spdif muxer
 *
 * The pts of a packet is its index.
 */
std::vector<sBurst> Mux(cSpdifMuxer &muxer, const std::vector<std::pair<const uint8_t *, int>> &frames)
{
    std::vector<sBurst> bursts;

    for (size_t i = 0; i < frames.size(); i++) {
        AVPacket *pkt = MakePacket(frames[i].first, frames[i].second, i);
        int64_t pts = AV_NOPTS_VALUE;
        const std::vector<uint8_t> &burst = muxer.Mux(pkt, &pts);
        if (!burst.empty())
            bursts.push_back({burst, pts});
        av_packet_free(&pkt);
    }

    return bursts;
}

/** frames of an elementary stream, split by the reassembly buffer */
std::vector<std::pair<const uint8_t *, int>> SplitFrames(const std::vector<uint8_t> &data, AVCodecID codec)
{
    std::vector<std::pair<const uint8_t *, int>> frames;
    cReassemblyBufferAudio buffer;

    for (size_t pos = 0; pos < data.size();) {
        int size = buffer.GetCompleteFrameSize(codec, &data[pos], data.size() - pos);
        if (!size)                          // the size of the last frame is unknown (DTS)
            size = data.size() - pos;
        frames.push_back({&data[pos], size});
        pos += size;
    }

    return frames;
}

/**
 * Build DTS-HD frames: append a (synthetic) extension substream to each DTS core frame
 *
 * FFmpeg can't encode DTS-HD. The spdif muxer doesn't parse the substream, it only
 * needs its size.
 */
std::vector<uint8_t> MakeDtsHd(const std::vector<uint8_t> &core, int substreamSize)
{
    std::vector<uint8_t> data;
    std::vector<uint8_t> substream(substreamSize, 0x00);

    // SYNC, UserDefinedBits (8), nExtSSIndex (2), bHeaderSizeType (1) = 0,
    // nuExtSSHeaderSize - 1 (8) = 15, nuExtSSFsize - 1 (16)
    uint64_t bits = ((uint64_t)15 << (48 - 19)) | ((uint64_t)(substreamSize - 1) << (48 - 35));
    substream[0] = 0x64;
    substream[1] = 0x58;
    substream[2] = 0x20;
    substream[3] = 0x25;
    for (int i = 0; i < 6; i++)
        substream[4 + i] = (bits >> (40 - 8 * i)) & 0xFF;

    for (const auto &frame : SplitFrames(core, AV_CODEC_ID_DTS)) {
        data.insert(data.end(), frame.first, frame.first + frame.second);
        data.insert(data.end(), substream.begin(), substream.end());
    }

    return data;
}

/** TrueHD access units of a raw TrueHD stream */
std::vector<std::pair<const uint8_t *, int>> SplitTrueHD(const std::vector<uint8_t> &data)
{
    std::vector<std::pair<const uint8_t *, int>> units;

    // the access unit length is in 16 bit words
    for (size_t pos = 0; pos + 2 <= data.size();) {
        int size = (((data[pos] & 0x0F) << 8) | data[pos + 1]) * 2;
        REQUIRE(size > 0);
        REQUIRE(pos + size <= data.size());
        units.push_back({&data[pos], size});
        pos += size;
    }

    return units;
}

/**
 * Check the bursts of a stream
 *
 * @param bursts          the bursts
 * @param dataType        IEC 61937 data type (Pc)
 * @param burstSize       size of a burst in bytes
 * @param packetsPerBurst number of packets in one burst
 */
void CheckBursts(const std::vector<sBurst> &bursts, int dataType, size_t burstSize, int packetsPerBurst)
{
    REQUIRE(bursts.size() >= 2);

    for (size_t i = 0; i < bursts.size(); i++) {
        const std::vector<uint8_t> &burst = bursts[i].data;
        REQUIRE(burst.size() == burstSize);

        // preamble Pa, Pb and the data type Pc (little endian words)
        REQUIRE(burst[0] == 0x72);
        REQUIRE(burst[1] == 0xF8);
        REQUIRE(burst[2] == 0x1F);
        REQUIRE(burst[3] == 0x4E);
        REQUIRE((burst[4] & 0x7F) == dataType);

        // a burst has the pts of its first packet
        REQUIRE(bursts[i].pts == (int64_t)i * packetsPerBurst);
    }
}

/**
 * Check, that a burst lasts as long as its audio
 *
 * @param format          output format
 * @param burstSize       size of a burst in bytes
 * @param samples         samples of the audio in a burst
 * @param sampleRate      sample rate of the audio
 */
void CheckBurstDuration(const sPassthroughFormat &format, size_t burstSize, int samples, int sampleRate)
{
    int64_t outputFrames = burstSize / (format.channels * 2);
    // outputFrames / format.sampleRate == samples / sampleRate
    REQUIRE(outputFrames * sampleRate == (int64_t)samples * format.sampleRate);
}

} // namespace

TEST_CASE("Audio streams - PES reassembly", "[audiostreams][reassembly]") {
    SECTION("MP2")       { CheckReassembly("mp2_48k.mp2", AV_CODEC_ID_MP2, 576); }
    SECTION("AAC ADTS")  { CheckReassembly("aac_adts_48k.aac", AV_CODEC_ID_AAC, 0); }
    SECTION("AAC LATM")  { CheckReassembly("aac_latm_48k.latm", AV_CODEC_ID_AAC_LATM, 0); }
    SECTION("AC-3")      { CheckReassembly("ac3_48k.ac3", AV_CODEC_ID_AC3, 1792); }
    SECTION("E-AC-3")    { CheckReassembly("eac3_48k.eac3", AV_CODEC_ID_EAC3, 1536); }
    SECTION("DTS 48kHz") { CheckReassembly("dts_48k.dts", AV_CODEC_ID_DTS, 1024); }
    SECTION("DTS 44.1kHz") { CheckReassembly("dts_44k.dts", AV_CODEC_ID_DTS, 0); }
}

TEST_CASE("Audio streams - spdif passthrough", "[audiostreams][spdif]") {
    cSpdifMuxer muxer;

    SECTION("AC-3") {
        std::vector<uint8_t> data = ReadFile("ac3_48k.ac3");
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_AC3, 48000, 0, false);
        REQUIRE(muxer.Open(AV_CODEC_ID_AC3, format.sampleRate));

        std::vector<sBurst> bursts = Mux(muxer, SplitFrames(data, AV_CODEC_ID_AC3));
        CheckBursts(bursts, PC_AC3, 6144, 1);
        CheckBurstDuration(format, 6144, 1536, 48000);
    }

    SECTION("E-AC-3") {
        std::vector<uint8_t> data = ReadFile("eac3_48k.eac3");
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_EAC3, 48000, 0, false);
        REQUIRE(format.sampleRate == 192000);
        REQUIRE(muxer.Open(AV_CODEC_ID_EAC3, format.sampleRate));

        std::vector<sBurst> bursts = Mux(muxer, SplitFrames(data, AV_CODEC_ID_EAC3));
        CheckBursts(bursts, PC_EAC3, 24576, 1);
        CheckBurstDuration(format, 24576, 1536, 48000);
    }

    SECTION("DTS 48kHz") {
        std::vector<uint8_t> data = ReadFile("dts_48k.dts");
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, 48000, 0, false);
        REQUIRE(muxer.Open(AV_CODEC_ID_DTS, format.sampleRate));

        std::vector<sBurst> bursts = Mux(muxer, SplitFrames(data, AV_CODEC_ID_DTS));
        CheckBursts(bursts, PC_DTS1, 2048, 1);
        CheckBurstDuration(format, 2048, 512, 48000);
    }

    SECTION("DTS 44.1kHz is sent with 44.1kHz") {
        std::vector<uint8_t> data = ReadFile("dts_44k.dts");
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, 44100, 0, false);
        REQUIRE(format.sampleRate == 44100);
        REQUIRE(muxer.Open(AV_CODEC_ID_DTS, format.sampleRate));

        std::vector<sBurst> bursts = Mux(muxer, SplitFrames(data, AV_CODEC_ID_DTS));
        CheckBursts(bursts, PC_DTS1, 2048, 1);
        CheckBurstDuration(format, 2048, 512, 44100);
    }

    SECTION("TrueHD") {
        std::vector<uint8_t> data = ReadFile("truehd_48k.thd");
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_TRUEHD, 48000, 0, false);
        REQUIRE(format.sampleRate == 192000);
        REQUIRE(format.channels == 8);
        REQUIRE(muxer.Open(AV_CODEC_ID_TRUEHD, format.sampleRate));

        // a MAT frame holds 24 access units of 40 samples (20ms)
        std::vector<sBurst> bursts = Mux(muxer, SplitTrueHD(data));
        CheckBursts(bursts, PC_TRUEHD, 61440, 24);
        CheckBurstDuration(format, 61440, 24 * 40, 48000);
    }

    SECTION("DTS-HD Master Audio") {
        std::vector<uint8_t> core = ReadFile("dts_48k.dts");
        std::vector<uint8_t> data = MakeDtsHd(core, 4000);
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, 48000, DTS_PROFILE(DTS_HD_MA), true);
        REQUIRE(format.sampleRate == 192000);
        REQUIRE(format.channels == 8);
        REQUIRE(format.dtshdRate == 768000);
        REQUIRE(muxer.Open(AV_CODEC_ID_DTS, format.sampleRate, format.dtshdRate));

        std::vector<std::pair<const uint8_t *, int>> frames = SplitFrames(data, AV_CODEC_ID_DTS);
        REQUIRE(frames[0].second == 1024 + 4000);

        // DTS type IV with the complete frame
        std::vector<sBurst> bursts = Mux(muxer, frames);
        CheckBursts(bursts, PC_DTSHD, 32768, 1);
        CheckBurstDuration(format, 32768, 512, 48000);
        REQUIRE(bursts[0].data[6] + (bursts[0].data[7] << 8) >= 1024 + 4000);
    }

    SECTION("DTS-HD High Resolution Audio") {
        std::vector<uint8_t> core = ReadFile("dts_48k.dts");
        std::vector<uint8_t> data = MakeDtsHd(core, 2000);
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, 48000, DTS_PROFILE(DTS_HD_HRA), true);
        REQUIRE(format.sampleRate == 192000);
        REQUIRE(format.channels == 2);
        REQUIRE(format.dtshdRate == 192000);
        REQUIRE(muxer.Open(AV_CODEC_ID_DTS, format.sampleRate, format.dtshdRate));

        std::vector<sBurst> bursts = Mux(muxer, SplitFrames(data, AV_CODEC_ID_DTS));
        CheckBursts(bursts, PC_DTSHD, 8192, 1);
        CheckBurstDuration(format, 8192, 512, 48000);
    }

    SECTION("DTS-HD without DTS-HD passthrough sends the core") {
        std::vector<uint8_t> core = ReadFile("dts_48k.dts");
        std::vector<uint8_t> data = MakeDtsHd(core, 4000);
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, 48000, DTS_PROFILE(DTS_HD_MA), false);
        REQUIRE(format.sampleRate == 48000);
        REQUIRE(format.dtshdRate == 0);
        REQUIRE(muxer.Open(AV_CODEC_ID_DTS, format.sampleRate, format.dtshdRate));

        std::vector<sBurst> bursts = Mux(muxer, SplitFrames(data, AV_CODEC_ID_DTS));
        CheckBursts(bursts, PC_DTS1, 2048, 1);
        // length code Pd in bits: only the core
        REQUIRE(bursts[0].data[6] + (bursts[0].data[7] << 8) == 1024 * 8);
    }

    SECTION("Closing drops the data of an incomplete burst") {
        std::vector<uint8_t> data = ReadFile("truehd_48k.thd");
        std::vector<std::pair<const uint8_t *, int>> units = SplitTrueHD(data);
        REQUIRE(muxer.Open(AV_CODEC_ID_TRUEHD, 192000));

        // half a MAT frame
        units.resize(12);
        REQUIRE(Mux(muxer, units).empty());

        // after a reopen the next burst starts from scratch with the next packet
        REQUIRE(muxer.Open(AV_CODEC_ID_TRUEHD, 192000));
        units = SplitTrueHD(data);
        std::vector<sBurst> bursts = Mux(muxer, units);
        CheckBursts(bursts, PC_TRUEHD, 61440, 24);
    }

    SECTION("A closed muxer returns nothing") {
        std::vector<uint8_t> data = ReadFile("ac3_48k.ac3");
        AVPacket *pkt = MakePacket(data.data(), 1792, 0);
        int64_t pts = AV_NOPTS_VALUE;
        REQUIRE(muxer.Mux(pkt, &pts).empty());
        av_packet_free(&pkt);
    }
}

TEST_CASE("Passthrough format", "[spdif]") {
    SECTION("AC-3 and DTS use the rate of the stream") {
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_AC3, 44100, 0, false);
        REQUIRE(format.sampleRate == 44100);
        REQUIRE(format.channels == 2);
        REQUIRE(format.dtshdRate == 0);
        REQUIRE(GetPassthroughFormat(AV_CODEC_ID_DTS, 48000, 0, false).sampleRate == 48000);
    }

    SECTION("An unknown rate is 48kHz") {
        REQUIRE(GetPassthroughFormat(AV_CODEC_ID_AC3, 0, 0, false).sampleRate == 48000);
        REQUIRE(GetPassthroughFormat(AV_CODEC_ID_EAC3, 0, 0, false).sampleRate == 192000);
    }

    SECTION("E-AC-3 uses four times the rate") {
        REQUIRE(GetPassthroughFormat(AV_CODEC_ID_EAC3, 48000, 0, false).sampleRate == 192000);
        REQUIRE(GetPassthroughFormat(AV_CODEC_ID_EAC3, 44100, 0, false).sampleRate == 176400);
    }

    SECTION("TrueHD is a high bitrate bitstream") {
        for (int rate : {48000, 96000, 192000}) {
            sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_TRUEHD, rate, 0, false);
            REQUIRE(format.sampleRate == 192000);
            REQUIRE(format.channels == 8);
        }
        for (int rate : {44100, 88200, 176400})
            REQUIRE(GetPassthroughFormat(AV_CODEC_ID_TRUEHD, rate, 0, false).sampleRate == 176400);
    }
}

TEST_CASE("Passthrough format - DTS-HD", "[spdif]") {
    SECTION("DTS-HD profiles with a core") {
        REQUIRE(IsDtsHdProfile(DTS_PROFILE(DTS_HD_MA)));
        REQUIRE(IsDtsHdProfile(DTS_PROFILE(DTS_HD_HRA)));
        REQUIRE_FALSE(IsDtsHdProfile(DTS_PROFILE(DTS)));
        REQUIRE_FALSE(IsDtsHdProfile(DTS_PROFILE(DTS_ES)));
        REQUIRE_FALSE(IsDtsHdProfile(DTS_PROFILE(DTS_EXPRESS)));  // no core
        REQUIRE_FALSE(IsDtsHdProfile(DTS_PROFILE(UNKNOWN)));
    }

    SECTION("Master Audio is a high bitrate bitstream") {
        for (int rate : {48000, 96000, 192000}) {
            sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, rate, DTS_PROFILE(DTS_HD_MA), true);
            REQUIRE(format.sampleRate == 192000);
            REQUIRE(format.channels == 8);
            REQUIRE(format.dtshdRate == 768000);
        }
    }

    SECTION("The 44.1kHz rate family and unknown profiles pass the core through") {
        sPassthroughFormat format = GetPassthroughFormat(AV_CODEC_ID_DTS, 44100, DTS_PROFILE(DTS_HD_MA), true);
        REQUIRE(format.sampleRate == 44100);
        REQUIRE(format.dtshdRate == 0);

        format = GetPassthroughFormat(AV_CODEC_ID_DTS, 0, DTS_PROFILE(UNKNOWN), true);
        REQUIRE(format.sampleRate == 48000);
        REQUIRE(format.channels == 2);
        REQUIRE(format.dtshdRate == 0);
    }
}

TEST_CASE("IEC 60958 sample rate code", "[spdif]") {
    REQUIRE(GetIEC958SampleRateCode(48000, 2, false) == IEC958_AES3_CON_FS_48000);
    REQUIRE(GetIEC958SampleRateCode(48000, 2, true) == IEC958_AES3_CON_FS_48000);
    REQUIRE(GetIEC958SampleRateCode(44100, 2, true) == IEC958_AES3_CON_FS_44100);
    REQUIRE(GetIEC958SampleRateCode(192000, 2, true) == IEC958_AES3_CON_FS_192000);
    REQUIRE(GetIEC958SampleRateCode(176400, 2, true) == IEC958_AES3_CON_FS_176400);
    REQUIRE(GetIEC958SampleRateCode(22050, 2, false) == IEC958_AES3_CON_FS_NOTID);

    SECTION("A high bitrate bitstream is sent with 768kHz") {
        REQUIRE(GetIEC958SampleRateCode(192000, 8, true) == IEC958_AES3_CON_FS_768000);
        REQUIRE(GetIEC958SampleRateCode(176400, 8, true) == IEC958_AES3_CON_FS_NOTID);
    }

    SECTION("8 channel PCM uses its sample rate") {
        REQUIRE(GetIEC958SampleRateCode(192000, 8, false) == IEC958_AES3_CON_FS_192000);
    }
}

TEST_CASE("Pause bursts", "[spdif]") {
    SECTION("A pause burst has the size of the bursts of the stream") {
        std::vector<uint16_t> pause = BuildPauseBurst(6144);
        REQUIRE(pause.size() == 3072);

        const uint8_t *data = reinterpret_cast<const uint8_t *>(pause.data());
        // Pa, Pb, Pc = 3 (pause), Pd = 32 bits, gap length = 1536 IEC 60958 frames (little endian)
        std::vector<uint8_t> header = { 0x72, 0xF8, 0x1F, 0x4E, 0x03, 0x00, 0x20, 0x00, 0x00, 0x06, 0x00, 0x00 };
        REQUIRE(std::vector<uint8_t>(data, data + header.size()) == header);
        for (size_t i = header.size(); i < 6144; i++)
            REQUIRE(data[i] == 0);
    }

    SECTION("The gap length is limited to 16 bit") {
        std::vector<uint16_t> pause = BuildPauseBurst(61440 * 8);
        REQUIRE(le16toh(pause[4]) == 0xFFFF);
    }

    SECTION("Invalid sizes") {
        REQUIRE(BuildPauseBurst(0).empty());
        REQUIRE(BuildPauseBurst(8).empty());
        REQUIRE(BuildPauseBurst(6146).empty());
    }

    SECTION("The pause bursts keep the period of the stream") {
        std::vector<uint16_t> pause = BuildPauseBurst(16);
        const uint8_t *burst = reinterpret_cast<const uint8_t *>(pause.data());
        std::vector<uint8_t> buffer(40, 0xAA);

        // start in the middle of a burst
        FillPauseBursts(buffer.data(), buffer.size(), pause, 10);
        for (size_t i = 0; i < buffer.size(); i++)
            REQUIRE(buffer[i] == burst[(10 + i) % 16]);
    }

    SECTION("Without a pause burst zeros are filled") {
        std::vector<uint8_t> buffer(40, 0xAA);
        FillPauseBursts(buffer.data(), buffer.size(), {}, 0);
        for (uint8_t byte : buffer)
            REQUIRE(byte == 0);
    }
}

/**
 * Build an ELD (version 2) with the given monitor name and short audio descriptors
 */
static std::vector<uint8_t> BuildEld(const std::string &monitorName, const std::vector<std::array<uint8_t, 3>> &sads)
{
	std::vector<uint8_t> eld(20, 0);
	eld[0] = 2 << 3;                                     // ELD version 2
	eld[4] = (3 << 5) | monitorName.size();              // CEA-861-D, monitor name length
	eld[5] = sads.size() << 4;                           // number of SADs
	eld.insert(eld.end(), monitorName.begin(), monitorName.end());
	for (const auto &sad : sads)
		eld.insert(eld.end(), sad.begin(), sad.end());
	while (eld.size() % 4)
		eld.push_back(0);
	eld[2] = (eld.size() - 4) / 4;                       // baseline block length
	return eld;
}

TEST_CASE("Passthrough codecs of the ELD", "[spdif]") {
	// SAD byte 0: audio format code << 3 | (channels - 1)
	const std::array<uint8_t, 3> lpcm = { (1 << 3) | 7, 0x7F, 0x07 };
	const std::array<uint8_t, 3> ac3 = { (2 << 3) | 5, 0x07, 0x50 };
	const std::array<uint8_t, 3> dts = { (7 << 3) | 5, 0x06, 0xC0 };
	const std::array<uint8_t, 3> eac3 = { (10 << 3) | 7, 0x07, 0x01 };
	const std::array<uint8_t, 3> dtshd = { (11 << 3) | 7, 0x7E, 0x03 };
	const std::array<uint8_t, 3> mat = { (12 << 3) | 7, 0x54, 0x01 };

	SECTION("a TV with AC-3 and E-AC-3") {
		std::vector<uint8_t> eld = BuildEld("TV", { lpcm, ac3, eac3 });
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == (CODEC_AC3 | CODEC_EAC3));
	}

	SECTION("an AV receiver with all formats") {
		std::vector<uint8_t> eld = BuildEld("AV RECEIVER", { lpcm, ac3, dts, eac3, dtshd, mat });
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == CODEC_PASSTHROUGH_ALL);
	}

	SECTION("DTS-HD includes the DTS core") {
		std::vector<uint8_t> eld = BuildEld("AVR", { dtshd });
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == (CODEC_DTS | CODEC_DTSHD));
	}

	SECTION("only PCM") {
		std::vector<uint8_t> eld = BuildEld("MONITOR", { lpcm });
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == 0);
	}

	SECTION("an empty ELD (no sink connected)") {
		std::vector<uint8_t> eld(128, 0);
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == 0);
		REQUIRE(GetPassthroughMaskFromEld(nullptr, 0) == 0);
	}

	SECTION("a truncated ELD is ignored") {
		std::vector<uint8_t> eld = BuildEld("TV", { ac3, eac3 });
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size() - 4) == 0);
		// the baseline length limits the SADs
		eld[2] = 4;
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == 0);
	}

	SECTION("a wrong version or monitor name length is ignored") {
		std::vector<uint8_t> eld = BuildEld("TV", { ac3 });
		eld[0] = 1 << 3;
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == 0);
		eld[0] = 2 << 3;
		eld[4] = (3 << 5) | 17;
		REQUIRE(GetPassthroughMaskFromEld(eld.data(), eld.size()) == 0);
	}
}

TEST_CASE("DVD LPCM", "[audiostreams]") {
	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PCM_DVD);
	if (!codec)
		SKIP("FFmpeg has no pcm_dvd decoder");

	AVCodecContext *ctx = avcodec_alloc_context3(codec);
	REQUIRE(ctx);
	ctx->pkt_timebase = { 1, 90000 };
	REQUIRE(avcodec_open2(ctx, codec, nullptr) == 0);

	// the packet starts with the LPCM header after the sub stream header:
	// frame number, 16 bit / 48kHz / 2 channels, dynamic range
	std::vector<uint8_t> data = { 0x00, 0x01, 0x80 };
	for (int i = 0; i < 4; i++) {
		uint16_t left = static_cast<uint16_t>(i * 1000);
		uint16_t right = static_cast<uint16_t>(-i * 1000);
		data.insert(data.end(), { uint8_t(left >> 8), uint8_t(left), uint8_t(right >> 8), uint8_t(right) });
	}

	AVPacket *pkt = av_packet_alloc();
	REQUIRE(av_new_packet(pkt, data.size()) == 0);
	memcpy(pkt->data, data.data(), data.size());
	pkt->pts = 90000;
	REQUIRE(avcodec_send_packet(ctx, pkt) == 0);

	AVFrame *frame = av_frame_alloc();
	REQUIRE(avcodec_receive_frame(ctx, frame) == 0);
	REQUIRE(frame->sample_rate == 48000);
	REQUIRE(frame->ch_layout.nb_channels == 2);
	REQUIRE(frame->nb_samples == 4);
	REQUIRE(frame->pts == 90000);
	const int16_t *samples = reinterpret_cast<const int16_t *>(frame->data[0]);
	REQUIRE(samples[2] == 1000);
	REQUIRE(samples[3] == -1000);

	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&ctx);
}
