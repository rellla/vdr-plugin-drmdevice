/**
 * @file test_hardwaredevice.cpp
 * Unit tests for the hardware detection (cHardwareDevice)
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "../hardwaredevice.h"

/**
 * Writes a temporary device tree "compatible" file, which is removed at the end of the test
 */
class cCompatibleFile {
public:
	explicit cCompatibleFile(const std::string &content) {
		char path[] = "/tmp/drmdevice-compatible-XXXXXX";
		int fd = mkstemp(path);
		REQUIRE(fd >= 0);
		REQUIRE(write(fd, content.data(), content.size()) == (ssize_t)content.size());
		close(fd);
		m_path = path;
	}
	~cCompatibleFile() { unlink(m_path.c_str()); }
	const char *Path(void) const { return m_path.c_str(); }

private:
	std::string m_path;
};

TEST_CASE("cHardwareDevice - detection from the device tree", "[hardwaredevice]") {
	SECTION("Raspberry Pi 4, the SoC is not the first entry") {
		cCompatibleFile file(std::string("raspberrypi,4-model-b\0brcm,bcm2711\0", 35));
		cHardwareDevice device(file.Path());

		REQUIRE(device.GetName() != nullptr);
		REQUIRE(std::string(device.GetName()).find("bcm2711") == 0);
		REQUIRE(device.GetQuirks() & QUIRK_CODEC_FLUSH_WORKAROUND);
	}

	SECTION("Long property (larger than the old fixed read buffer)") {
		std::string content;
		for (int i = 0; i < 20; i++)
			content += "vendor,some-long-compatible-string-" + std::to_string(i) + '\0';
		content += std::string("brcm,bcm2712\0", 13);

		cCompatibleFile file(content);
		cHardwareDevice device(file.Path());

		REQUIRE(device.GetName() != nullptr);
		REQUIRE(std::string(device.GetName()).find("bcm2712") == 0);
	}

	SECTION("Unknown device") {
		cCompatibleFile file(std::string("rockchip,rk3588\0", 16));
		cHardwareDevice device(file.Path());

		REQUIRE(device.GetName() == nullptr);
		REQUIRE(device.GetQuirks() == 0);
	}

	SECTION("Missing file") {
		cHardwareDevice device("/nonexistent/compatible");

		REQUIRE(device.GetName() == nullptr);
		REQUIRE(device.GetQuirks() == 0);
	}
}
