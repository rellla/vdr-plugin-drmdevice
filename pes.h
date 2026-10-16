// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pes.h
 * PES Packet Parser Header File
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_PES_H
#define __DRMDEVICE_PES_H

#include <cstdint>
#include <map>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
}

/**
 * PES Packet Parser
 *
 * This class parses PES (Packetized Elementary Stream) packets
 * to extract header information, PTS, and payload data.
 *
 * @ingroup misc
 */
class cPes {
public:
	cPes(const uint8_t *, int, bool);
	bool IsValid(void);
	bool HasPts(void);
	int64_t GetPts(void);
	const uint8_t *GetPayload(void);
	int GetPayloadSize(void);
	int GetPacketLength(void);
	uint8_t GetStreamId(void) { return m_data[3]; }    ///< get the stream id (4th byte of the packet)

protected:
	virtual bool IsStreamIdValid(void) = 0;           ///< true, if the stream id is valid for this packet type
	void Init(void);
	bool IsHeaderValid(void);

	bool m_valid = false;      ///< flag indicating if the PES packet is valid
	const uint8_t *m_data;     ///< pointer to the raw PES packet data
	int m_size;                ///< size of the PES packet
	const char *m_identifier;  ///< identifier string for logging

	// According to H.222.0 03/2017 Table 2-21 ("PES packet") packet_start_code_prefix
	// And also according to H.264/HEVC payload
	static constexpr uint32_t PES_PACKET_START_CODE_PREFIX = 0x00'0001;  ///< start code of every PES packet
	static constexpr uint32_t PES_PACKET_START_CODE_PREFIX_LEN = 3;      ///< length of the start code in bytes
};

/**
 * Video PES Packet Parser
 *
 * Specialized parser for video PES packets with stream IDs in the range 0xE0-0xEF.
 *
 * @ingroup misc
 */
class cPesVideo : public cPes {
public:
	/** Parse a video PES packet of the given size */
	cPesVideo(const uint8_t *data, int size) : cPes(data, size, false) { cPes::Init(); }
private:
	bool IsStreamIdValid(void) override { return (GetStreamId() & 0xF0) == 0xE0; } ///< video stream IDs are in the range 0xE0-0xEF
};

/**
 * Audio PES Packet Parser
 *
 * Specialized parser for audio PES packets with stream IDs in the range 0xC0-0xCF,
 * or private stream ID 0xBD which may contain audio data.
 *
 * @ingroup misc
 */
class cPesAudio : public cPes {
public:
	/** Parse an audio PES packet of the given size */
	cPesAudio(const uint8_t *data, int size) : cPes(data, size, true) { cPes::Init(); }
	bool IsAudioStreamId(void) { return (GetStreamId() & 0xF0) == 0xC0; } ///< audio stream IDs are in the range 0xC0-0xCF
private:
	bool IsStreamIdValid(void) override { return IsAudioStreamId() || IsPrivateStreamId(); } ///< audio or private stream id
	bool IsPrivateStreamId(void) { return GetStreamId() == 0xBD; }                           ///< private stream 1 (e.g. AC-3)
};

/**
 * PTS Tracking Buffer
 *
 * Manages a byte buffer along with a map of PTS (Presentation Time Stamp) values
 * associated with specific positions in the buffer. This is used for maintaining
 * temporal information when reassembling fragmented streams.
 *
 * @ingroup misc
 */
class cPtsTrackingBuffer {
public:
	/** Create an empty buffer, the identifier is used for logging */
	cPtsTrackingBuffer(const char *identifier) : m_identifier(identifier) {}
	void Push(const uint8_t *, int, int64_t);
	void Erase(size_t);
	int64_t GetPts(void);
	const uint8_t *Peek(void) { return m_data.data(); }           ///< get the buffered data (nullptr, if empty)
	void Reset(void) { m_data.clear(); m_pts.clear(); }           ///< drop all data and pts values
	int GetSize(void) { return m_data.size(); }                   ///< get the number of buffered bytes
	const char *GetIdentifier(void) { return m_identifier; }      ///< get the identifier
private:
	const char *m_identifier;            ///< identifier string for logging
	std::map<size_t, int64_t> m_pts;     ///< Map of buffer positions to PTS values
	std::vector<uint8_t> m_data;         ///< Byte buffer
};

/**
 * Base Class for Stream Reassembly Buffers
 *
 * Reassembles fragmented elementary streams into complete AVPackets.
 * Handles codec detection and PTS tracking across fragments.
 *
 * @ingroup misc
 */
class cReassemblyBuffer {
public:
	/** Append a fragment (PES payload) with its pts to the buffer */
	virtual void Push(const uint8_t *data, int size, int64_t pts) { m_buffer.Push(data, size, pts); }
	/** Get the next complete frame as AVPacket (nullptr, if none is available yet) */
	virtual AVPacket *PopAvPacket(void) = 0;
	bool IsEmpty(void) { return m_buffer.GetSize() == 0; }        ///< true, if nothing is buffered
	size_t GetSize(void) { return m_buffer.GetSize(); }           ///< get the number of buffered bytes
	void Reset(void);
	AVCodecID GetCodec(void) { return m_codec; }                  ///< get the detected codec
protected:
	/** Create an empty buffer, the identifier is used for logging */
	cReassemblyBuffer(const char *identifier) : m_buffer(identifier) {}
	AVPacket *PopAvPacket(int);
	AVCodecID m_codec = AV_CODEC_ID_NONE;          ///< detected codec ID
	cPtsTrackingBuffer m_buffer;                   ///< fragmentation buffer
	int64_t m_lastPoppedPts = AV_NOPTS_VALUE;      ///< PTS of the last popped AVPacket
};

/**
 * Video Stream Reassembly Buffer
 *
 * Reassembles video elementary streams (MPEG2, H.264, HEVC) by detecting
 * frame start codes and codec headers.
 *
 * @ingroup misc
 */
class cReassemblyBufferVideo : public cReassemblyBuffer {
public:
	cReassemblyBufferVideo(void) : cReassemblyBuffer("vid") {}
	/** Get the whole buffer as one AVPacket (a video PES packet contains at most one frame) */
	AVPacket *PopAvPacket(void) override { return cReassemblyBuffer::PopAvPacket(m_buffer.GetSize()); }
	bool ParseCodecHeader(const uint8_t *, int);
	bool HasLeadingZero(const uint8_t *, int);
private:
	static constexpr uint32_t VIDEO_FRAME_START_CODE = 0x00'0001;  ///< start code of a video frame
	static constexpr int VIDEO_FRAME_START_CODE_LEN = 3;           ///< length of the start code in bytes

	static constexpr uint8_t MPEG2_STREAM_TYPE = 0xB3;  ///< MPEG2 sequence header start code
	static constexpr uint8_t H264_STREAM_TYPE = 0x09;   ///< H.264 access unit delimiter (NAL unit type 9)
	static constexpr uint8_t HEVC_STREAM_TYPE = 0x46;   ///< HEVC access unit delimiter (NAL unit type 35)
};

/**
 * Information about a detected audio sync word
 *
 * @ingroup misc
 */
struct SyncWordInfo {
	AVCodecID codecId;     ///< Detected codec ID
	int pos;               ///< Position of sync word in buffer
};

/**
 * Audio Stream Reassembly Buffer
 *
 * Reassembles audio elementary streams by detecting sync words and validating
 * frame headers. Supports MP2, AAC (LATM/ADTS), AC3, E-AC3 and DTS codecs.
 *
 * @ingroup misc
 */
class cReassemblyBufferAudio : public cReassemblyBuffer {
public:
	cReassemblyBufferAudio(void) : cReassemblyBuffer("AUDIO") {}
	AVPacket *PopAvPacket(void) override;
	AVCodecID TruncateBufferUntilFirstValidData(void);
	SyncWordInfo FindSyncWord(const uint8_t *, int );
	AVCodecID DetectCodecFromSyncWord(const uint8_t *, int);
	int GetFrameSizeForCodec(AVCodecID, const uint8_t *);
private:
	SyncWordInfo FindTwoConsecutiveFramesWithSameSyncWord();
	static constexpr int MAX_HEADER_SIZE = 6; ///< bytes needed to detect a sync word and the frame size
	bool m_ptsInvalid = false;                ///< flag indicating if PTS is invalid for current buffer, because it was truncated
};

#endif
