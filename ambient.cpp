// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ambient.cpp
 * Ambient Image Service
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

#include "ambient.h"
#include "drmbuffer.h"
#include "logger.h"

/**
 * @addtogroup misc
 * @{
 */

/**
 * Offer the displayed buffer
 *
 * Called by the display thread after every display cycle. This must not block.
 * Only if a consumer waits for a new frame and the frame has changed, a snapshot
 * (duplicated descriptors) is taken. If the mutex is busy, the frame is skipped.
 *
 * @param buf             displayed video buffer (nullptr, if no video is shown)
 * @param screenWidth     current screen width
 * @param screenHeight    current screen height
 */
void cAmbientService::Offer(cDrmBuffer *buf, int screenWidth, int screenHeight)
{
	m_videoShown.store(buf && buf->IsDirty(), std::memory_order_relaxed);

	if (!m_waiting.load(std::memory_order_relaxed) || !buf || !buf->IsDirty())
		return;

	// the frame didn't change (pause, still picture)
	int64_t pts = buf->frame ? buf->frame->pts : AV_NOPTS_VALUE;
	if (buf->Id() == m_lastFbId && pts == m_lastPts)
		return;

	if (screenWidth <= 0 || screenHeight <= 0)
		return;

	std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
	if (!lock.owns_lock() || m_pending)
		return;

	// the frame isn't kept: a changed content doesn't matter for an ambient light,
	// and the decoder must never wait for another plugin
	if (!m_snapshot.Take(buf, false))
		return;

	m_frame = m_frame + 1 ? m_frame + 1 : 1;    // 0 means "none" for the consumer
	m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;
	m_pending = true;
	m_snapshotsTaken++;
	lock.unlock();
	m_cond.notify_all();

	m_lastFbId = buf->Id();
	m_lastPts = pts;
}

/**
 * Set, if the device is detached
 *
 * A waiting consumer returns at once.
 */
void cAmbientService::SetDetached(bool detached)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_detached = detached;
		if (detached) {
			m_snapshot.Release();
			m_pending = false;
			m_videoShown = false;
		}
	}
	m_cond.notify_all();
}

/**
 * Get the image of the displayed video (service AMBIENTIMAGE_SERVICE)
 *
 * Called by another plugin in its own thread. Waits up to req->timeoutMs for
 * a frame newer than req->lastFrame and reads the image from it.
 *
 * @param req       the request
 * @return          false, if the request is invalid
 */
bool cAmbientService::GetImage(AmbientImage_v1_0_t *req)
{
	if (!req || req->structSize != sizeof(AmbientImage_v1_0_t) || req->width < 8 || req->width > 640 || !req->rgb || req->rgbSize <= 0)
		return false;

	req->flags = 0;
	req->imageWidth = 0;
	req->imageHeight = 0;
	req->sampleUs = 0;

	cFrameSnapshot snapshot;
	int screenWidth = 0, screenHeight = 0;
	unsigned int frame = 0;

	{
		std::unique_lock<std::mutex> lock(m_mutex);
		auto ready = [this, req] { return m_detached || (m_pending && m_frame != req->lastFrame); };

		if (!ready() && req->timeoutMs > 0) {
			m_waiting++;
			m_cond.wait_for(lock, std::chrono::milliseconds(std::min(req->timeoutMs, 5000)), ready);
			m_waiting--;
		}

		if (!m_detached && m_pending && m_frame != req->lastFrame) {
			snapshot.Swap(m_snapshot);
			m_pending = false;
			frame = m_frame;
			screenWidth = m_screenWidth;
			screenHeight = m_screenHeight;
		}
	}

	if (m_detached)
		req->flags |= AMBIENT_DETACHED;
	if (m_videoShown)
		req->flags |= AMBIENT_VIDEO;
	if (m_paused)
		req->flags |= AMBIENT_PAUSED;

	if (!snapshot.IsValid())
		return true;

	std::lock_guard<std::mutex> lock(m_sampleMutex);
	if (CreateImage(snapshot, screenWidth, screenHeight, req)) {
		req->flags |= AMBIENT_NEW_IMAGE;
		req->frame = frame;
	}

	return true;
}

/**
 * Read the image from a snapshot
 *
 * The image has the aspect ratio of the screen. The video is placed into it
 * as it is placed on the screen (scaled video, letterbox of a different
 * aspect ratio), the rest is black.
 */
bool cAmbientService::CreateImage(cFrameSnapshot &snapshot, int screenWidth, int screenHeight, AmbientImage_v1_0_t *req)
{
	const char *toneMap = cFrameSampler::ToneMapName(snapshot.ColorTrc(), snapshot.ColorPrimaries());
	snprintf(req->format, sizeof(req->format), "%s %ux%u%s%s", cFrameSampler::FormatName(snapshot.PixFmt(), snapshot.Modifier()),
		snapshot.Width(), snapshot.Height(), toneMap ? ", " : "", toneMap ? toneMap : "");
	if (toneMap && (snapshot.ColorTrc() == AVCOL_TRC_SMPTE2084 || snapshot.ColorTrc() == AVCOL_TRC_ARIB_STD_B67))
		req->flags |= AMBIENT_TONEMAPPED;

	if (!cFrameSampler::IsSupported(snapshot.PixFmt(), snapshot.Modifier()))
		return false;

	int width = req->width;
	int height = std::max(1, (int)(((int64_t)width * screenHeight + screenWidth / 2) / screenWidth));
	if ((int64_t)width * height * 3 > req->rgbSize)
		return false;

	memset(req->rgb, 0, (size_t)width * height * 3);

	// position of the video in the image
	const cRect &screen = snapshot.ScreenRect();
	int x0 = std::clamp((int)(((int64_t)screen.X() * width + screenWidth / 2) / screenWidth), 0, width);
	int y0 = std::clamp((int)(((int64_t)screen.Y() * height + screenHeight / 2) / screenHeight), 0, height);
	int x1 = std::clamp((int)(((int64_t)(screen.X() + screen.Width()) * width + screenWidth / 2) / screenWidth), 0, width);
	int y1 = std::clamp((int)(((int64_t)(screen.Y() + screen.Height()) * height + screenHeight / 2) / screenHeight), 0, height);

	req->imageWidth = width;
	req->imageHeight = height;

	if (x1 <= x0 || y1 <= y0)
		return true;    // the video is not visible, the image is black

	// shown part of the frame (zoom)
	cRect crop(0, 0, snapshot.Width(), snapshot.Height());
	const cRect &c = snapshot.CropRect();
	if (!c.IsEmpty() && c.X() >= 0 && c.Y() >= 0 &&
	    c.X() + c.Width() <= (int)snapshot.Width() && c.Y() + c.Height() <= (int)snapshot.Height())
		crop = c;

	auto start = std::chrono::steady_clock::now();

	sMappedFrame frame;
	if (!snapshot.Map(&frame))
		return false;

	bool ok = m_sampler.Sample(frame, crop.X(), crop.Y(), crop.Width(), crop.Height(),
	                           req->rgb + ((size_t)y0 * width + x0) * 3, width * 3, x1 - x0, y1 - y0);
	snapshot.Unmap();

	req->sampleUs = (int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

	if (!ok)
		LOGDEBUG2(L_GRAB, "ambient: %s: cannot sample the frame (%s)", __FUNCTION__, req->format);

	return ok;
}

/** @} */
