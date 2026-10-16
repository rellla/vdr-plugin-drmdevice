// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hardwaredevice.h
 * Describes a hardware device
 *
 * @copyright 2018 - 2019 by zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_HARDWAREDEVICE_H
#define __DRMDEVICE_HARDWAREDEVICE_H

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "logger.h"

/**
 * Hardware dependent quirks
 *
 * @ingroup misc
 */
enum HardwareQuirks {
	QUIRK_CODEC_FLUSH_WORKAROUND       = 1 << 0,     ///< set, if we have to close and reopen the codec instead of avcodec_flush_buffers (rpi)
};

/**
 * Helper function to read a (small) file completely
 *
 * The content may contain null bytes (e.g. device tree properties).
 *
 * @param file           the file to be read
 *
 * @return the content of the file, an empty string on errors
 *
 * @ingroup misc
 */
static inline std::string ReadFile(const char *file) {
	std::ifstream input(file, std::ios::binary);
	if (!input) {
		LOGERROR("%s: Can't open %s", __FUNCTION__, file);
		return {};
	}

	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

/**
 * Hardware device
 *
 * @ingroup misc
 */
class cHardwareDevice
{
public:
	/**
	 * Create a new hardware device
	 *
	 * @param compatibleFile    device tree "compatible" property to detect the device from
	 */
	explicit cHardwareDevice(const char *compatibleFile = "/sys/firmware/devicetree/base/compatible") {
		// the device tree property is a list of null terminated strings,
		// e.g. "raspberrypi,4-model-b\0brcm,bcm2711\0"
		std::string compatible = ReadFile(compatibleFile);
		if (compatible.empty()) {
			LOGERROR("could not read from %s, no quirks set", compatibleFile);
			return;
		}

		static const struct {
			const char *id;
			const char *name;
			int quirks;
		} devices[] = {
			{ "bcm2836", "bcm2836 (Raspberry Pi 2 Model B)", QUIRK_CODEC_FLUSH_WORKAROUND },
			{ "bcm2837", "bcm2837 (Raspberry Pi 2 Model B v1.2/ 3 Model B, Raspberry Pi 3 Compute Module 3)", QUIRK_CODEC_FLUSH_WORKAROUND },
			{ "bcm2711", "bcm2711 (Raspberry Pi 4 Model B, Compute Module 4, Pi 400)", QUIRK_CODEC_FLUSH_WORKAROUND },
			{ "bcm2712", "bcm2712 (Raspberry Pi 5, Compute Module 5, Pi 500)", QUIRK_CODEC_FLUSH_WORKAROUND },
		};

		size_t pos = 0;
		while (pos < compatible.size() && !m_deviceName) {
			size_t end = compatible.find('\0', pos);
			if (end == std::string::npos)
				end = compatible.size();

			std::string entry = compatible.substr(pos, end - pos);
			for (const auto &device : devices) {
				if (entry.find(device.id) != std::string::npos) {
					m_deviceName = device.name;
					m_quirks |= device.quirks;
					break;
				}
			}

			pos = end + 1;
		}

		if (m_deviceName)
			LOGDEBUG("%s found%s", m_deviceName,
			    m_quirks & QUIRK_CODEC_FLUSH_WORKAROUND ? ", flush workaround" : "");
		else
			LOGDEBUG("%s found, no quirks set", compatible.c_str());
	}

	/** Get Hardware Quirks */
	int GetQuirks(void) { return m_quirks; };
	/** Get Hardware Name (currently unused) */
	const char *GetName(void) { return m_deviceName; };

private:
	const char *m_deviceName = nullptr;  ///< device name
	int m_quirks = 0;                    ///< hardware dependent quirks for codec and and display
};

#endif
