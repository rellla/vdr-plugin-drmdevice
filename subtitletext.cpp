// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file subtitletext.cpp
 * Text Conversion of the Mediaplayer Subtitles
 *
 * FFmpeg's decoders of text subtitles (e.g. SubRip, ASS, mov_text) return the
 * text as an ASS dialogue line. The functions convert it to plain text lines,
 * which can be drawn on the OSD.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "subtitle.h"

/**
 * Split a text into trimmed lines and drop the empty ones
 *
 * @param text      text with '\n' as line breaks
 * @return          the lines
 *
 * @ingroup mediaplayer
 */
static std::vector<std::string> SplitLines(const std::string &text)
{
	std::vector<std::string> lines;
	size_t start = 0;

	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();

		std::string line = text.substr(start, end - start);
		size_t first = line.find_first_not_of(" \t\r");
		size_t last = line.find_last_not_of(" \t\r");
		if (first != std::string::npos)
			lines.push_back(line.substr(first, last - first + 1));

		start = end + 1;
	}

	return lines;
}

/**
 * Convert an ASS dialogue line to plain text lines
 *
 * The dialogue line has the fields "ReadOrder,Layer,Style,Name,MarginL,MarginR,
 * MarginV,Effect,Text" (FFmpeg >= 3.0) or starts with "Dialogue:" and has the
 * start and end time instead of the read order (older versions). The override
 * codes ({...}) are removed, \\N and \\n are line breaks and \\h is a space.
 *
 * @param dialogue  ASS dialogue line
 * @return          the text lines (empty, if there's no text)
 *
 * @ingroup mediaplayer
 */
std::vector<std::string> AssDialogueToLines(const char *dialogue)
{
	if (!dialogue)
		return {};

	// skip the fields before the text
	int fields = strncmp(dialogue, "Dialogue:", 9) ? 8 : 9;
	const char *s = dialogue;
	for (int i = 0; i < fields; i++) {
		s = strchr(s, ',');
		if (!s)
			return {};
		s++;
	}

	std::string text;
	for (; *s; s++) {
		if (*s == '{') {
			const char *end = strchr(s, '}');
			if (!end)
				break;
			s = end;
		} else if (*s == '\\' && (s[1] == 'N' || s[1] == 'n')) {
			text += '\n';
			s++;
		} else if (*s == '\\' && s[1] == 'h') {
			text += ' ';
			s++;
		} else
			text += *s;
	}

	return SplitLines(text);
}

/**
 * Convert a plain text subtitle to text lines
 *
 * HTML like tags (e.g. \<i\>) are removed.
 *
 * @param subtitle  subtitle text
 * @return          the text lines (empty, if there's no text)
 *
 * @ingroup mediaplayer
 */
std::vector<std::string> TextToLines(const char *subtitle)
{
	if (!subtitle)
		return {};

	std::string text;
	for (const char *s = subtitle; *s; s++) {
		if (*s == '<' && (isalpha(static_cast<unsigned char>(s[1])) || s[1] == '/')) {
			const char *end = strchr(s, '>');
			if (end) {
				s = end;
				continue;
			}
		}
		text += *s;
	}

	return SplitLines(text);
}
