// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file videofilter.h
 * Deinterlace and Scaling Filters Header File
 *
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_VIDEOFILTER_H
#define __DRMDEVICE_VIDEOFILTER_H

#include <functional>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
}

#include <vdr/thread.h>

#include "misc.h"
#include "queue.h"

class cDrmBuffer;
class cVideoRender;

/**
 * Deinterlace and Scaling Filters
 * @defgroup filter Video filters
 */

/**
 * Video Filter
 *
 * @ingroup filter
 */
class cVideoFilter : public cThread {
public:
	cVideoFilter(cVideoRender *, cQueue<cDrmBuffer> *, const char *, std::function<void(AVFrame *)>);
	void InitAndStart(const AVCodecContext *, AVFrame *, bool);
	void Stop(void);
	void PushFrame(AVFrame *);
	bool IsInputBufferFull(void) { return m_frames.IsFull(); };
	bool IsInputBufferEmpty(void) { return m_frames.IsEmpty(); };
	int GetNumFramesToFilter(void) { return m_numFramesToFilter; };

private:
	cVideoRender *m_pRender;                          ///< pointer to renderer
	constexpr static int VIDEO_FILTER_QUEUE_MAX = 3;  ///< maximum number of decoded frames waiting for the filter

	AVFilterGraph *m_pFilterGraph = nullptr;          ///< filter graph
	AVFilterContext *m_pBuffersrcCtx = nullptr;       ///< buffer src context
	AVFilterContext *m_pBuffersinkCtx = nullptr;      ///< buffer sink context

	cQueue<AVFrame> m_frames{VIDEO_FILTER_QUEUE_MAX}; ///< queue for frames to be filtered
	std::function<void(AVFrame *)> m_frameOutput;     ///< function to output the frame
	cQueue<cDrmBuffer> *m_pDrmBufferQueue;            ///< pointer to renderer's DRM buffer queue
	int m_numFramesToFilter = 0;                      ///< number of frames to be filtered

	void Action(void);
	void SetFilterOutputPixFormat(AVPixelFormat);
};

#endif
