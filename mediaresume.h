// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mediaresume.h
 * Resume Positions of the Mediaplayer Header File
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_MEDIARESUME_H
#define __DRMDEVICE_MEDIARESUME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/**
 * Resume Positions
 *
 * Stores the position, at which the replay of a media file was stopped,
 * so a later replay can continue there (like VDR's resume file of a
 * recording).
 *
 * All positions are stored in one file, one line per media file
 * ("<position in ms> <path>"), the most recently stored first. Only the
 * newest entries are kept.
 *
 * @ingroup mediaplayer
 */
class cMediaResume {
public:
	static constexpr size_t MAX_ENTRIES = 100;          ///< default number of stored positions

	explicit cMediaResume(const std::string &, size_t = MAX_ENTRIES);

	int64_t Get(const std::string &);
	bool Store(const std::string &, int64_t);
	bool Delete(const std::string &);

private:
	using Entries = std::vector<std::pair<std::string, int64_t>>; ///< path and position in ms

	Entries Load(void);
	bool Save(const Entries &);

	std::string m_fileName;                             ///< file with the stored positions
	size_t m_maxEntries;                                ///< max. number of stored positions
};

#endif
