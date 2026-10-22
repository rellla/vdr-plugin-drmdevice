// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file subtitle.cpp
 * Text Subtitles of the Mediaplayer
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>

#include <vdr/config.h>
#include <vdr/device.h>
#include <vdr/font.h>
#include <vdr/osd.h>

#include "subtitle.h"

cTextSubtitles::~cTextSubtitles(void)
{
	Hide();
}

/**
 * Add a subtitle
 *
 * A subtitle without an end is shown until the next subtitle starts, at most
 * UNKNOWN_DURATION.
 *
 * @param startPts  start of the display (PES timebase)
 * @param endPts    end of the display (PES timebase), AV_NOPTS_VALUE or
 *                  startPts, if the end is unknown
 * @param lines     text lines
 */
void cTextSubtitles::Add(int64_t startPts, int64_t endPts, const std::vector<std::string> &lines)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (auto &subtitle : m_subtitles) {
		if (subtitle.endUnknown && subtitle.startPts < startPts && subtitle.endPts > startPts)
			subtitle.endPts = startPts;
	}

	if (lines.empty())
		return;

	bool endUnknown = endPts <= startPts;
	sSubtitle subtitle = { startPts, endUnknown ? startPts + UNKNOWN_DURATION : endPts, endUnknown, lines };

	auto pos = std::upper_bound(m_subtitles.begin(), m_subtitles.end(), startPts,
		[](int64_t pts, const sSubtitle &s) { return pts < s.startPts; });
	m_subtitles.insert(pos, subtitle);
}

/**
 * Drop all subtitles and hide the shown one (e.g. after a jump)
 */
void cTextSubtitles::Clear(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_subtitles.clear();
	Hide();
}

/**
 * Show the subtitles of the given presentation time
 *
 * @param stc       presentation time of the played video (PES timebase)
 */
void cTextSubtitles::Process(int64_t stc)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	while (!m_subtitles.empty() && m_subtitles.front().endPts <= stc)
		m_subtitles.pop_front();

	std::vector<std::string> lines;
	for (auto &subtitle : m_subtitles) {
		if (subtitle.startPts > stc)
			break;
		lines.insert(lines.end(), subtitle.lines.begin(), subtitle.lines.end());
	}

	if (lines == m_shownLines)
		return;

	if (lines.empty())
		Hide();
	else
		Show(lines);
}

/**
 * Draw the subtitle lines centered at the bottom of the screen
 *
 * Long lines are wrapped. The colors and the transparencies of VDR's subtitle
 * setup are used, the subtitle offset moves the subtitles down.
 *
 * @param lines     text lines
 */
void cTextSubtitles::Show(const std::vector<std::string> &lines)
{
	Hide();
	// remember the lines also, if they can't be shown, so it isn't tried again
	m_shownLines = lines;

	int width, height;
	double aspect;
	cDevice::PrimaryDevice()->GetOsdSize(width, height, aspect);
	if (width <= 0 || height <= 0)
		return;

	int fontHeight = height / 20;
	if (!m_pFont || m_fontHeight != fontHeight) {
		m_pFont.reset(cFont::CreateFont(Setup.FontOsd, fontHeight));
		m_fontHeight = fontHeight;
	}
	if (!m_pFont)
		return;

	// wrap the lines, which are too long
	int maxWidth = width * 9 / 10;
	std::vector<std::string> wrapped;
	for (auto &line : lines) {
		cTextWrapper wrapper(line.c_str(), m_pFont.get(), maxWidth);
		for (int i = 0; i < wrapper.Lines(); i++)
			wrapped.push_back(wrapper.GetLine(i));
	}

	int lineHeight = m_pFont->Height();
	int padding = lineHeight / 4;
	int areaHeight = static_cast<int>(wrapped.size()) * lineHeight;
	int top = std::clamp(height - height / 12 - areaHeight + Setup.SubtitleOffset, 0, std::max(height - areaHeight, 0));

	m_pOsd = cOsdProvider::NewOsd(0, top, OSD_LEVEL_SUBTITLES);
	tArea area = { 0, 0, width - 1, areaHeight - 1, 32 };
	if (m_pOsd->CanHandleAreas(&area, 1) != oeOk) {
		area.bpp = 8;
		if (m_pOsd->CanHandleAreas(&area, 1) != oeOk) {
			delete m_pOsd;
			m_pOsd = nullptr;
			return;
		}
	}
	m_pOsd->SetAreas(&area, 1);

	tColor fg = static_cast<tColor>((10 - Setup.SubtitleFgTransparency) * 0xFF / 10) << 24 | 0x00FFFFFF;
	tColor bg = static_cast<tColor>((10 - Setup.SubtitleBgTransparency) * 0xFF / 10) << 24;

	m_pOsd->DrawRectangle(0, 0, width - 1, areaHeight - 1, clrTransparent);
	for (size_t i = 0; i < wrapped.size(); i++) {
		int textWidth = m_pFont->Width(wrapped[i].c_str()) + 2 * padding;
		int x = std::max((width - textWidth) / 2, 0);
		int y = static_cast<int>(i) * lineHeight;
		m_pOsd->DrawRectangle(x, y, std::min(x + textWidth, width) - 1, y + lineHeight - 1, bg);
		m_pOsd->DrawText(x + padding, y, wrapped[i].c_str(), fg, bg, m_pFont.get());
	}
	m_pOsd->Flush();
}

/**
 * Hide the shown subtitle
 */
void cTextSubtitles::Hide(void)
{
	delete m_pOsd;
	m_pOsd = nullptr;
	m_shownLines.clear();
}
