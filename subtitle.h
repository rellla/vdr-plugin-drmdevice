// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file subtitle.h
 * Text Subtitles of the Mediaplayer Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_SUBTITLE_H
#define __DRMDEVICE_SUBTITLE_H

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class cFont;
class cOsd;

std::vector<std::string> AssDialogueToLines(const char *);
std::vector<std::string> TextToLines(const char *);

/**
 * Text Subtitles
 *
 * Shows the text subtitles of the mediaplayer (e.g. SubRip, ASS or mov_text)
 * on an OSD with the subtitle level, so it is hidden by the menus like VDR's
 * DVB subtitles. The colors, the transparencies and the offset of VDR's
 * subtitle setup are used.
 *
 * The subtitles are added by the player thread with their presentation time
 * and shown by Process() at the presentation time of the played video.
 *
 * @ingroup mediaplayer
 */
class cTextSubtitles {
public:
	cTextSubtitles(void) = default;
	~cTextSubtitles(void);
	cTextSubtitles(const cTextSubtitles &) = delete;
	cTextSubtitles &operator=(const cTextSubtitles &) = delete;

	void Add(int64_t, int64_t, const std::vector<std::string> &);
	void Clear(void);
	void Process(int64_t);

private:
	/** one subtitle */
	struct sSubtitle {
		int64_t startPts;                   ///< start of the display (PES timebase)
		int64_t endPts;                     ///< end of the display (PES timebase)
		bool endUnknown;                    ///< true, if the end is unknown (ends with the next subtitle)
		std::vector<std::string> lines;     ///< text lines
	};

	static constexpr int64_t UNKNOWN_DURATION = 5 * 90000; ///< duration of a subtitle without an end (PES timebase)

	std::mutex m_mutex;                     ///< protects the subtitles
	std::deque<sSubtitle> m_subtitles;      ///< subtitles, which are shown or will be shown (sorted by start)
	cOsd *m_pOsd = nullptr;                 ///< subtitle osd (nullptr, if nothing is shown)
	std::unique_ptr<cFont> m_pFont;         ///< font of the subtitles
	int m_fontHeight = 0;                   ///< height of m_pFont
	std::vector<std::string> m_shownLines;  ///< the shown lines

	void Show(const std::vector<std::string> &);
	void Hide(void);
};

#endif
