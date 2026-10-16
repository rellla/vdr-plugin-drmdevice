/**
 * @file test_ringbuffer.cpp
 * Unit tests for cRingbuffer
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

#include "../ringbuffer.h"

static std::vector<uint8_t> Sequence(size_t size, uint8_t start = 0)
{
	std::vector<uint8_t> data(size);
	std::iota(data.begin(), data.end(), start);
	return data;
}

TEST_CASE("cRingbuffer - Initial state", "[ringbuffer]") {
	cRingbuffer rb(100);

	REQUIRE(rb.UsedBytes() == 0);
	REQUIRE(rb.FreeBytes() == 100);
}

TEST_CASE("cRingbuffer - Write and read", "[ringbuffer]") {
	cRingbuffer rb(100);
	auto in = Sequence(40);

	REQUIRE(rb.Write(in.data(), in.size()) == 40);
	REQUIRE(rb.UsedBytes() == 40);
	REQUIRE(rb.FreeBytes() == 60);

	std::vector<uint8_t> out(40);
	REQUIRE(rb.Read(out.data(), out.size()) == 40);
	REQUIRE(out == in);
	REQUIRE(rb.UsedBytes() == 0);
	REQUIRE(rb.FreeBytes() == 100);
}

TEST_CASE("cRingbuffer - Write is limited by free space", "[ringbuffer]") {
	cRingbuffer rb(16);
	auto in = Sequence(20);

	REQUIRE(rb.Write(in.data(), in.size()) == 16);
	REQUIRE(rb.FreeBytes() == 0);
	REQUIRE(rb.Write(in.data(), 1) == 0);

	std::vector<uint8_t> out(16);
	REQUIRE(rb.Read(out.data(), out.size()) == 16);
	REQUIRE(std::equal(out.begin(), out.end(), in.begin()));
}

TEST_CASE("cRingbuffer - Read is limited by used bytes", "[ringbuffer]") {
	cRingbuffer rb(16);
	auto in = Sequence(5);
	std::vector<uint8_t> out(10, 0xFF);

	rb.Write(in.data(), in.size());
	REQUIRE(rb.Read(out.data(), out.size()) == 5);
	REQUIRE(std::equal(in.begin(), in.end(), out.begin()));
	REQUIRE(out[5] == 0xFF); // untouched
	REQUIRE(rb.Read(out.data(), out.size()) == 0);
}

TEST_CASE("cRingbuffer - Wrap around", "[ringbuffer]") {
	cRingbuffer rb(10);
	std::vector<uint8_t> out(10);

	// move read and write pointer to position 7
	auto first = Sequence(7);
	rb.Write(first.data(), first.size());
	rb.Read(out.data(), 7);

	SECTION("Write and read across the buffer end") {
		auto in = Sequence(8, 100);
		REQUIRE(rb.Write(in.data(), in.size()) == 8);
		REQUIRE(rb.UsedBytes() == 8);

		REQUIRE(rb.Read(out.data(), 8) == 8);
		REQUIRE(std::equal(in.begin(), in.end(), out.begin()));
	}

	SECTION("Write exactly up to the buffer end") {
		auto in = Sequence(3, 50);
		REQUIRE(rb.Write(in.data(), in.size()) == 3);

		const void *rp;
		REQUIRE(rb.GetReadPointer(&rp) == 3);

		auto in2 = Sequence(4, 60);
		REQUIRE(rb.Write(in2.data(), in2.size()) == 4);

		REQUIRE(rb.Read(out.data(), 7) == 7);
		REQUIRE(std::equal(in.begin(), in.end(), out.begin()));
		REQUIRE(std::equal(in2.begin(), in2.end(), out.begin() + 3));
	}
}

TEST_CASE("cRingbuffer - Read and write pointer access", "[ringbuffer]") {
	cRingbuffer rb(10);

	SECTION("Write pointer reports contiguous free space") {
		void *wp;
		REQUIRE(rb.GetWritePointer(&wp) == 10);

		memset(wp, 0xAB, 4);
		REQUIRE(rb.WriteAdvance(4) == 4);
		REQUIRE(rb.UsedBytes() == 4);

		const void *rp;
		REQUIRE(rb.GetReadPointer(&rp) == 4);
		REQUIRE(static_cast<const uint8_t *>(rp)[0] == 0xAB);
		REQUIRE(static_cast<const uint8_t *>(rp)[3] == 0xAB);

		REQUIRE(rb.ReadAdvance(4) == 4);
		REQUIRE(rb.UsedBytes() == 0);
	}

	SECTION("Contiguous space ends at the buffer end") {
		std::vector<uint8_t> tmp(8);
		rb.Write(tmp.data(), 8);
		rb.Read(tmp.data(), 6);

		// used: positions 6..7, free: 8..9 and 0..5
		void *wp;
		REQUIRE(rb.GetWritePointer(&wp) == 2);
		REQUIRE(rb.FreeBytes() == 8);

		const void *rp;
		REQUIRE(rb.GetReadPointer(&rp) == 2);
	}

	SECTION("Advance is limited") {
		REQUIRE(rb.WriteAdvance(20) == 10);
		REQUIRE(rb.FreeBytes() == 0);
		REQUIRE(rb.ReadAdvance(20) == 10);
		REQUIRE(rb.UsedBytes() == 0);
	}

	SECTION("Advance across the buffer end") {
		REQUIRE(rb.WriteAdvance(8) == 8);
		REQUIRE(rb.ReadAdvance(8) == 8);
		REQUIRE(rb.WriteAdvance(5) == 5);

		void *wp;
		REQUIRE(rb.GetWritePointer(&wp) == 5);
		REQUIRE(rb.ReadAdvance(5) == 5);
		REQUIRE(rb.UsedBytes() == 0);
	}
}

TEST_CASE("cRingbuffer - Reset", "[ringbuffer]") {
	cRingbuffer rb(10);
	auto in = Sequence(7);

	rb.Write(in.data(), in.size());
	rb.Reset();

	REQUIRE(rb.UsedBytes() == 0);
	REQUIRE(rb.FreeBytes() == 10);

	void *wp;
	REQUIRE(rb.GetWritePointer(&wp) == 10);
}
