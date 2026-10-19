// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mediaresume.cpp
 * Resume Positions of the Mediaplayer
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "mediaresume.h"

/**
 * Create the resume positions
 *
 * @param fileName       file with the stored positions
 * @param maxEntries     max. number of stored positions
 */
cMediaResume::cMediaResume(const std::string &fileName, size_t maxEntries)
	: m_fileName(fileName),
	  m_maxEntries(maxEntries)
{
}

/**
 * Read all stored positions
 *
 * Invalid lines are skipped.
 *
 * @return          the stored positions, the most recent first
 */
cMediaResume::Entries cMediaResume::Load(void)
{
	Entries entries;
	std::ifstream file(m_fileName);
	std::string line;

	while (std::getline(file, line)) {
		size_t space = line.find(' ');
		if (space == std::string::npos || space == 0 || space + 1 >= line.size())
			continue;

		try {
			size_t end;
			int64_t positionMs = std::stoll(line.substr(0, space), &end);
			if (end != space || positionMs <= 0)
				continue;

			entries.emplace_back(line.substr(space + 1), positionMs);
		} catch (...) {
			continue;
		}
	}

	return entries;
}

/**
 * Write all positions
 *
 * The positions are written to a temporary file first, which then replaces
 * the old file, so the old positions aren't lost on an error.
 *
 * @param entries   the positions
 * @retval true     on success
 */
bool cMediaResume::Save(const Entries &entries)
{
	std::string tmpName = m_fileName + ".tmp";

	{
		std::ofstream file(tmpName, std::ios::trunc);
		if (!file)
			return false;

		for (const auto &entry : entries)
			file << entry.second << ' ' << entry.first << '\n';

		file.flush();
		if (!file)
			return false;
	}

	return rename(tmpName.c_str(), m_fileName.c_str()) == 0;
}

/**
 * Get the stored position of a media file
 *
 * @param path      path of the media file
 * @return          position in ms, -1 if no position is stored
 */
int64_t cMediaResume::Get(const std::string &path)
{
	Entries entries = Load();
	auto it = std::find_if(entries.begin(), entries.end(), [&path](const auto &entry) { return entry.first == path; });

	return it != entries.end() ? it->second : -1;
}

/**
 * Store the position of a media file
 *
 * A position <= 0 deletes the stored position.
 *
 * @param path        path of the media file
 * @param positionMs  position in ms
 * @retval true       on success
 */
bool cMediaResume::Store(const std::string &path, int64_t positionMs)
{
	if (path.empty() || path.find('\n') != std::string::npos)
		return false;

	if (positionMs <= 0)
		return Delete(path);

	Entries entries;
	entries.emplace_back(path, positionMs);

	for (auto &entry : Load()) {
		if (entry.first != path && entries.size() < m_maxEntries)
			entries.push_back(std::move(entry));
	}

	return Save(entries);
}

/**
 * Delete the stored position of a media file
 *
 * @param path      path of the media file
 * @retval true     on success (also if no position was stored)
 */
bool cMediaResume::Delete(const std::string &path)
{
	Entries entries = Load();
	size_t size = entries.size();

	entries.erase(std::remove_if(entries.begin(), entries.end(), [&path](const auto &entry) { return entry.first == path; }), entries.end());

	if (entries.size() == size)
		return true;

	return Save(entries);
}
