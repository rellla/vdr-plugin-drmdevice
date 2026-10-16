/**
 * @file test_queue.cpp
 * Unit tests for cQueue
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include "../queue.h"

TEST_CASE("cQueue - FIFO order", "[queue]") {
	cQueue<int> queue(3);
	int a = 1, b = 2, c = 3;

	REQUIRE(queue.IsEmpty());
	REQUIRE(queue.Pop() == nullptr);
	REQUIRE(queue.Peek() == nullptr);

	REQUIRE(queue.Push(&a));
	REQUIRE(queue.Push(&b));
	REQUIRE(queue.Push(&c));
	REQUIRE(queue.Size() == 3);

	REQUIRE(queue.Peek() == &a);
	REQUIRE(queue.Pop() == &a);
	REQUIRE(queue.Pop() == &b);
	REQUIRE(queue.Pop() == &c);
	REQUIRE(queue.IsEmpty());
}

TEST_CASE("cQueue - Capacity", "[queue]") {
	cQueue<int> queue(2);
	int a = 1, b = 2, c = 3;

	REQUIRE_FALSE(queue.IsFull());
	REQUIRE(queue.Push(&a));
	REQUIRE(queue.Push(&b));
	REQUIRE(queue.IsFull());
	REQUIRE_FALSE(queue.Push(&c));
	REQUIRE(queue.Size() == 2);

	queue.Pop();
	REQUIRE_FALSE(queue.IsFull());
	REQUIRE(queue.Push(&c));
}

TEST_CASE("cQueue - Clear", "[queue]") {
	cQueue<int> queue(5);
	int a = 1, b = 2;

	queue.Push(&a);
	queue.Push(&b);
	queue.Clear();

	REQUIRE(queue.IsEmpty());
	REQUIRE(queue.Size() == 0);
}

TEST_CASE("cQueue - nullptr elements are queued", "[queue]") {
	// cVideoStream uses a nullptr element as a drain marker
	cQueue<int> queue(5);
	int a = 1;

	REQUIRE(queue.Push(&a));
	REQUIRE(queue.Push(nullptr));
	REQUIRE(queue.Size() == 2);

	REQUIRE(queue.Pop() == &a);
	REQUIRE_FALSE(queue.IsEmpty());
	REQUIRE(queue.Peek() == nullptr);
	REQUIRE(queue.Pop() == nullptr);
	REQUIRE(queue.IsEmpty());
}
