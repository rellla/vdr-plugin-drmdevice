/**
 * @file test_zapbuffer.cpp
 * Unit tests for cZapBuffer
 *
 * @license{AGPL-3.0-or-later}
 */

#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <vdr/remux.h>

#include "../zapbuffer.h"

namespace {

/** A TS packet with a pid and a counter in its payload, so packets can be told apart */
std::array<uint8_t, TS_SIZE> Packet(int pid, int counter, bool scrambled = false)
{
	std::array<uint8_t, TS_SIZE> packet{};
	packet[0] = TS_SYNC_BYTE;
	packet[1] = (pid >> 8) & 0x1F;
	packet[2] = pid & 0xFF;
	packet[3] = 0x10 | (scrambled ? 0x80 : 0x00) | (counter & 0x0F);
	packet[4] = counter & 0xFF;
	packet[5] = (counter >> 8) & 0xFF;
	return packet;
}

/** Counter of the n-th packet in a buffer */
int CounterAt(const std::vector<uint8_t> &data, size_t n)
{
	return data[n * TS_SIZE + 4] | (data[n * TS_SIZE + 5] << 8);
}

const int VPID = 0x100;
const int APID = 0x101;

} // namespace

TEST_CASE("cZapBuffer - waits for an I-frame", "[zapbuffer]") {
	cZapBuffer buffer;

	buffer.Put(Packet(VPID, 1).data(), true, true, false);   // P-frame
	buffer.Put(Packet(APID, 2).data(), false, false, false);
	REQUIRE_FALSE(buffer.Ready());
	REQUIRE(buffer.Take(nullptr).empty());

	// an audio packet before a video PES start isn't kept
	buffer.Put(Packet(APID, 3).data(), false, false, false);
	REQUIRE(buffer.Size() == 0);

	buffer.Put(Packet(VPID, 4).data(), true, true, true);   // I-frame
	buffer.Put(Packet(APID, 5).data(), false, false, false);
	REQUIRE(buffer.Ready());
	REQUIRE(buffer.Frames() == 1);
	REQUIRE(buffer.Size() == 2 * TS_SIZE);
}

TEST_CASE("cZapBuffer - starts with the PES packet of the I-frame", "[zapbuffer]") {
	cZapBuffer buffer;

	// the frame detector detects the I-frame in the second packet of its PES packet
	buffer.Put(Packet(VPID, 1).data(), true, true, false);    // P-frame
	buffer.Put(Packet(VPID, 2).data(), true, false, false);   // PES start of the I-frame
	buffer.Put(Packet(APID, 3).data(), false, false, false);
	REQUIRE_FALSE(buffer.Ready());
	buffer.Put(Packet(VPID, 4).data(), false, true, true);   // I-frame detected
	buffer.Put(Packet(VPID, 5).data(), true, true, false);   // P-frame
	REQUIRE(buffer.Ready());
	REQUIRE(buffer.Frames() == 2);

	std::vector<uint8_t> data = buffer.Take(nullptr);
	REQUIRE(data.size() == 4 * TS_SIZE);
	REQUIRE(CounterAt(data, 0) == 2);
	REQUIRE(CounterAt(data, 3) == 5);

	// the same for an I-frame, while the buffer is ready
	buffer.Put(Packet(VPID, 6).data(), true, true, true);     // I-frame
	buffer.Put(Packet(VPID, 7).data(), true, false, false);   // PES start of the next I-frame
	buffer.Put(Packet(VPID, 8).data(), false, true, true);    // next I-frame detected
	data = buffer.Take(nullptr);
	REQUIRE(data.size() == 2 * TS_SIZE);
	REQUIRE(CounterAt(data, 0) == 7);
}

TEST_CASE("cZapBuffer - an I-frame without PES start is ignored", "[zapbuffer]") {
	cZapBuffer buffer;

	buffer.Put(Packet(VPID, 1).data(), false, true, true);
	REQUIRE_FALSE(buffer.Ready());
	REQUIRE(buffer.Size() == 0);
}

TEST_CASE("cZapBuffer - starts with the last I-frame", "[zapbuffer]") {
	cZapBuffer buffer;

	buffer.Put(Packet(VPID, 1).data(), true, true, true);    // I-frame
	buffer.Put(Packet(VPID, 2).data(), true, true, false);   // P-frame
	buffer.Put(Packet(APID, 3).data(), false, false, false);
	REQUIRE(buffer.Frames() == 2);

	buffer.Put(Packet(VPID, 4).data(), true, true, true);      // next I-frame
	buffer.Put(Packet(VPID, 5).data(), false, false, false);   // continuation
	buffer.Put(Packet(APID, 6).data(), false, false, false);
	REQUIRE(buffer.Frames() == 1);

	std::vector<uint8_t> data = buffer.Take(nullptr);
	REQUIRE(data.size() == 3 * TS_SIZE);
	REQUIRE(CounterAt(data, 0) == 4);
	REQUIRE(CounterAt(data, 1) == 5);
	REQUIRE(CounterAt(data, 2) == 6);
}

TEST_CASE("cZapBuffer - Take() excludes the next packet", "[zapbuffer]") {
	SECTION("the buffer got the next packet already") {
		cZapBuffer buffer;
		buffer.Put(Packet(VPID, 1).data(), true, true, true);
		buffer.Put(Packet(APID, 2).data(), false, false, false);
		auto next = Packet(VPID, 3);
		buffer.Put(next.data(), false, false, false);

		std::vector<uint8_t> data = buffer.Take(next.data());
		REQUIRE(data.size() == 2 * TS_SIZE);
		REQUIRE(CounterAt(data, 1) == 2);
	}

	SECTION("the buffer didn't get the next packet yet") {
		cZapBuffer buffer;
		buffer.Put(Packet(VPID, 1).data(), true, true, true);
		buffer.Put(Packet(APID, 2).data(), false, false, false);
		auto next = Packet(VPID, 3);

		std::vector<uint8_t> data = buffer.Take(next.data());
		REQUIRE(data.size() == 2 * TS_SIZE);
	}

	SECTION("the buffer is empty afterwards") {
		cZapBuffer buffer;
		buffer.Put(Packet(VPID, 1).data(), true, true, true);
		REQUIRE_FALSE(buffer.Take(nullptr).empty());
		REQUIRE_FALSE(buffer.Ready());
		REQUIRE(buffer.Size() == 0);
		REQUIRE(buffer.Take(nullptr).empty());
	}
}

TEST_CASE("cZapBuffer - a scrambled packet discards the buffer", "[zapbuffer]") {
	cZapBuffer buffer;

	buffer.Put(Packet(VPID, 1).data(), true, true, true);
	buffer.Put(Packet(APID, 2, true).data(), false, false, false);
	REQUIRE_FALSE(buffer.Ready());
	REQUIRE(buffer.Size() == 0);

	// the following packets of the frame are dropped, too
	buffer.Put(Packet(APID, 3).data(), false, false, false);
	REQUIRE(buffer.Size() == 0);

	buffer.Put(Packet(VPID, 4).data(), true, true, true);
	REQUIRE(buffer.Ready());
}

TEST_CASE("cZapBuffer - a too long GOP discards the buffer", "[zapbuffer]") {
	cZapBuffer buffer;

	buffer.Put(Packet(VPID, 0).data(), true, true, true);
	for (int i = 1; i < cZapBuffer::MAX_FRAMES; i++)
		buffer.Put(Packet(VPID, i).data(), true, true, false);
	REQUIRE(buffer.Ready());
	REQUIRE(buffer.Frames() == cZapBuffer::MAX_FRAMES);

	buffer.Put(Packet(VPID, cZapBuffer::MAX_FRAMES).data(), true, true, false);
	REQUIRE_FALSE(buffer.Ready());
	REQUIRE(buffer.Size() == 0);

	// it waits for the next I-frame
	buffer.Put(Packet(VPID, 1000).data(), true, true, false);
	REQUIRE_FALSE(buffer.Ready());
	buffer.Put(Packet(VPID, 1001).data(), true, true, true);
	REQUIRE(buffer.Ready());
	REQUIRE(buffer.Frames() == 1);
}

TEST_CASE("cZapBuffer - the size is limited", "[zapbuffer]") {
	cZapBuffer buffer;
	const size_t maxPackets = cZapBuffer::MAX_BYTES / TS_SIZE;

	buffer.Put(Packet(VPID, 0).data(), true, true, true);
	for (size_t i = 1; i < maxPackets; i++)
		buffer.Put(Packet(APID, i).data(), false, false, false);
	REQUIRE(buffer.Ready());
	REQUIRE(buffer.Size() == maxPackets * TS_SIZE);

	buffer.Put(Packet(APID, 0).data(), false, false, false);
	REQUIRE_FALSE(buffer.Ready());
	REQUIRE(buffer.Size() == 0);
}
