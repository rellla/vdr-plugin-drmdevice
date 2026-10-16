// SPDX-License-Identifier: AGLP-3.0-or-later

/**
 * @file audioprocessor.h
 * Audio Manipulation Interface Header File
 *
 * @copyright 2009 - 2014 by Johns.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_AUDIOPROCESSOR_H
#define __DRMDEVICE_AUDIOPROCESSOR_H

#include <cstdint>
#include <string>

/**
 * Audio Manipulation
 *
 * Software audio processing of interleaved signed 16 bit samples:
 * normalizer, compressor, software volume and the options of the
 * equalizer filter.
 *
 * @ingroup audio
 */
class cAudioProcessor {
public:
	cAudioProcessor(const int);

	void Normalize(int16_t *, int);
	void SetNormalizer(int);
	void ResetNormalizer(void);

	void Compress(int16_t *, int);
	void SetCompressor(int);
	void ResetCompressor(void);

	void SetEqualizer(int[18]);
	std::string GetEqualizerOptions(void) const;

	void Amplify(int16_t *, int, int);
	void SetAmplifier(int volume) { m_amplifier = volume; }  ///< set the software volume factor (0 .. 1000)

private:
	const int m_bytesPerSample;                         ///< bytes per sample

	// Normalizer
	constexpr static int NORMALIZE_MAX_INDEX = 128;        ///< number of normalize average samples
	constexpr static int NORMALIZE_SAMPLES = 4096;         ///< number of normalize samples
	constexpr static int NORMALIZE_DEFAULT_FACTOR = 1000;  ///< default normalize factor
	constexpr static int NORMALIZE_MIN_FACTOR = 100;       ///< min. normalize factor
	int m_normalizeCounter = 0;                            ///< normalize sample counter
	uint32_t m_normalizeAverage[NORMALIZE_MAX_INDEX] = {}; ///< average of n last normalize sample blocks
	int m_normalizeIndex = 0;                              ///< index into normalize average table
	int m_normalizeReady = 0;                              ///< index normalize counter
	int m_normalizeFactor = NORMALIZE_DEFAULT_FACTOR;      ///< current normalize factor
	int m_normalizeMaxFactor = NORMALIZE_DEFAULT_FACTOR;   ///< max. normalize factor

	// Compressor
	constexpr static int COMPRESSION_DEFAULT_FACTOR = 1000;      ///< default compression factor
	constexpr static int COMPRESSION_DEFAULT_MAX_FACTOR = 2000;  ///< default compression max. factor
	int m_compressionFactor = 0;                                 ///< current compression factor
	int m_compressionMaxFactor = COMPRESSION_DEFAULT_MAX_FACTOR; ///< max. compression factor

	// Equalizer
	float m_equalizerBand[18] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; ///< equalizer band

	// Amplifier
	int m_amplifier = 1000;                 ///< software volume amplify factor
};

#endif
