// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file videostream.h
 * Video Input Stream Header File
 *
 * @copyright 2011 - 2015 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_VIDEOSTREAM_H
#define __DRMDEVICE_VIDEOSTREAM_H

#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <vdr/remux.h>
#include <vdr/thread.h>

#include "config.h"
#include "queue.h"
#include "videofilter.h"

class cDeviceConfig;
class cDrmBuffer;
class cVideoDecoder;
class cVideoRender;

/**
 * Video Input Stream
 *
 * @defgroup video Video Input Stream
 */

/**
 * Video Input Stream
 *
 * Base class of the main and the PiP video stream. It holds the queue of the
 * AVPackets received from VDR and runs the decoding thread, which sends the
 * packets to the decoder (cVideoDecoder) and passes the decoded frames either
 * to the filter thread (cVideoFilter) or directly to the renderer.
 *
 * @ingroup video
 */
class cVideoStream : public cThread {
public:
	virtual ~cVideoStream(void);

	void DecodeInput(void);
	bool IsInterlaced(void) { return false; }           ///< always false (not implemented)

	void Open(AVCodecID, AVCodecParameters * = nullptr, AVRational = { .num = 1, .den = PTSTICKS });
	void Exit(void);
	void ClearVdrCoreToDecoderQueue(void);
	void FlushDecoder(void);
	void CloseDecoder(void);
	bool PushAvPacket(AVPacket *avpkt);
	bool Drain(void);

	// decoding thread
	void Stop(void);
	void Halt(void) { m_mutex.lock(); }                 ///< halt the decoding thread
	void Resume(void) { m_mutex.unlock(); }             ///< resume the halted decoding thread

	// getters and setters
	cVideoDecoder *Decoder(void) { return m_pDecoder; }  ///< get the decoder
	void StartDecoder();
	size_t GetAvPacketsFilled(void) { return m_packets.Size(); }                  ///< get the number of queued packets
	bool IsInputBufferFull(void) { return m_packets.Size() >= VIDEO_PACKET_MAX; } ///< true, if the packet queue is full
	bool BuffersEmpty(void);
	enum AVCodecID GetCodecId(void) { return m_codecId; }                          ///< get the current codec id
	void ResetTrickSpeedFramesSentCounter(void) { m_sentTrickPkts = 0; }           ///< reset the counter of packets sent in trick speed
	bool HasInputPts(void) { return m_inputPts != AV_NOPTS_VALUE; }                ///< true, if a packet with pts was queued
	int64_t GetInputPtsMs(void) { return m_inputPts * 1000 * av_q2d(m_timebase); } ///< get the pts of the last queued packet in ms
	void ResetInputPts(void) { m_inputPts = AV_NOPTS_VALUE; }                      ///< forget the input pts
	void GetVideoSize(int *, int *, double *);
	int GetVideoPacketMax(void) { return VIDEO_PACKET_MAX; }  ///< get the size of the packet queue

	// Filter
	void CancelFilterThread(void);
	/** check again, if the filter thread is needed */
	void ResetFilterThreadNeededCheck() { m_checkFilterThreadNeeded = true; m_useDeinterlacer = false; }

	/** temporarily disable the deinterlacer */
	virtual void SetDeinterlacerDeactivated(bool deactivate) { m_deinterlacerDeactivated = deactivate; }
	bool IsDeinterlacerDeactivated(void) { return m_deinterlacerDeactivated; } ///< true, if the deinterlacer is temporarily disabled
	void DisableDeint(bool disable) { m_userDisabledDeinterlacer = disable; }  ///< disable the deinterlacer by user configuration
	/** set the number of packets, after which a failing hw decoder falls back to sw */
	void SetDecoderFallbackToSwNumPkts(int numPackets) { m_decoderFallbackToSwNumPkts = numPackets; }

protected:
	cVideoStream(cVideoRender *, int, cQueue<cDrmBuffer> *, cDeviceConfig *, bool, std::function<void(AVFrame *)>);
	virtual void Action(void);

private:
	cDeviceConfig *m_pConfig;           ///< plugin config
	cVideoDecoder *m_pDecoder;          ///< video decoder
	cVideoRender *m_pRender;            ///< video renderer
	const char *m_identifier;           ///< identifier string for logging
	bool m_isPipStream;                 ///< true, if this is the pip stream
	std::function<void(AVFrame *)> m_frameOutput;   ///< function to output the frame
	cQueue<cDrmBuffer> *m_pDrmBufferQueue;          ///< pointer to renderer's DRM buffer queue
	cVideoFilter m_videoFilter;         ///< pointer to deinterlace/scaling video filter thread
	std::mutex m_mutex;                 ///< mutex for decoding thread control

	bool m_checkFilterThreadNeeded = false;         ///< set, if we have to check, if filter thread is needed at start of playback
	int m_hardwareQuirks;                           ///< hardware specific quirks
	bool m_userDisabledDeinterlacer = false;        ///< set, if the user configured the deinterlace to be disabled
	bool m_deinterlacerDeactivated;                 ///< set, if the deinterlacer should be disabled temporarily (trickspeed, stillpicture, pip)
	bool m_useDeinterlacer = false;                 ///< set, if the deinterlacer is used
	int m_decoderFallbackToSwNumPkts = 22;          ///< fallback to sw decoder if hw decoder fails after the given number of packets sent

	constexpr static int VIDEO_PACKET_MAX = 192;    ///< max number of video packets held in the buffer
	cQueue<AVPacket> m_packets{VIDEO_PACKET_MAX};   ///< AVPackets queue
	constexpr static int AV_SYNC_BORDER_MS = 5000;  ///< absolute max a/v difference which should trigger a decoder flush

	enum AVCodecID m_codecId = AV_CODEC_ID_NONE;    ///< current codec id
	AVCodecParameters *m_pPar = nullptr;            ///< current codec parameters
	std::atomic<struct AVRational> m_timebase;      ///< current codec timebase
	int m_trickpkts = 0;                            ///< how many avpkt does the decoder need in trickspeed mode?
	int m_sentTrickPkts = 0;                        ///< how many avpkt have been sent to the decoder in trickspeed mode?
	std::atomic<bool> m_newStream = false;          ///< flag for new stream
	bool m_interlaced = false;                      ///< flag for interlaced stream
	sVideoStreamInfo m_streamInfo;                  ///< last published properties of the stream (main stream only)
	double m_framerate = 0.0;                       ///< current stream framerate

	int64_t m_inputPts = AV_NOPTS_VALUE;                 ///< PTS of the last packet (with a pts) pushed to the input buffer
	int64_t m_ptsForFramerateDetection = AV_NOPTS_VALUE; ///< helper PTS to calculate a framerate at stream start
	static constexpr int FRAMERATE_DETECTION_FRAMES = 8; ///< frames at stream start, whose pts differences are checked for the framerate
	int m_framerateDetectionFrames = 0;                  ///< number of the checked pts differences
	int64_t m_minFramerateDelta = 0;                     ///< smallest positive pts difference of two frames (0: none)
	int64_t m_lastDecodedPts = AV_NOPTS_VALUE;           ///< PTS of the latest packet, which was sent to the decoder in order to detect stream discontinuity

	// h264 parsing
	std::vector<std::string> m_naluTypesAtStart;    ///< array of strings to log the H.264 frames at stream start
	int m_numIFrames = 0;                           ///< counter for the arriving I-Frames at H.264 stream start
	int m_logPackets = 0;                           ///< parse and log all frames until the number of given I-Frames arrived
	int m_dropInvalidPackets = 0;                   ///< drop P-Frames with invalid references until the given number of I-Frames arrived
	std::set<int> m_dpbFrames;                      ///< private set of reference frames (internal short-time decoded picture buffer)
	int m_maxFrameNum = 1;                          ///< = 1 << Log2MaxFrameNumMinus4 + 4
	int m_log2MaxFrameNumMinus4 = -4;               ///< cache Log2MaxFrameNumMinus4 from a previous SPS parsing
	int m_ppsNumRefIdxL0DefaultActiveMinus1 = -1;   ///< cache NumRefIdxL0DefaultActiveMinuns1 from a previous PPS parsing
	int m_ppsNumRefIdxL1DefaultActiveMinus1 = -1;   ///< cache NumRefIdxL1DefaultActiveMinuns1 from a previous PPS parsing
	bool m_isResend = false;                        ///< track, if we already tried to send the AVPacket to the decoder
	                                                ///< if so, skip the parsing

	void SendPacket(AVPacket *);
	int ReceiveFrame(AVFrame **);
	void RenderFrame(AVFrame *);
	void UpdateStreamInfo(const AVFrame *);
	void CheckForcingFrameDecode(void);
	void OpenDecoder(void);
	bool ParseH264Packet(AVPacket *);
	bool PacketDropNeeded(AVPacket *);
	int64_t PtsToMs(int64_t pts) { return pts * av_q2d(m_timebase) * 1000; }  ///< convert a pts of the stream timebase to ms
};

/**
 * Main Video Stream
 *
 * The video stream, which is displayed on the video plane.
 *
 * @ingroup video
 */
class cMainVideoStream : public cVideoStream {
public:
	/** Create the main video stream */
	cMainVideoStream(cVideoRender *render, int hardwareQuirks, cQueue<cDrmBuffer> *buf, cDeviceConfig *config, std::function<void(AVFrame *)> fn)
		: cVideoStream(render, hardwareQuirks, buf, config, false, fn) {};
};

/**
 * PiP Video Stream
 *
 * The video stream of the picture in picture window, which is never deinterlaced.
 *
 * @ingroup video
 */
class cPipVideoStream : public cVideoStream {
public:
	/** Create the pip video stream */
	cPipVideoStream(cVideoRender *render, int hardwareQuirks, cQueue<cDrmBuffer> *buf, cDeviceConfig *config, std::function<void(AVFrame *)> fn)
		: cVideoStream(render, hardwareQuirks, buf, config, true, fn) {};
	void SetDeinterlacerDeactivated(bool) override {}     ///< deinterlacing is permanently disabled
};

#endif
