/// @file writequeuetest.cpp
/// @brief Unit tests for detail::WriteQueue.

#include "detail/writequeue.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using miniasyncnetsockets::detail::WriteQueue;
using mininetsockets::IoResult;
using mininetsockets::IoStatus;

// Converts a string literal to a byte vector.
std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

// Serializes a payload with a 4-byte big-endian header (for small payloads).
std::vector<std::byte> serialized(std::string_view payload)
{
    auto result = std::vector<std::byte>{
        static_cast<std::byte>(0),
        static_cast<std::byte>(0),
        static_cast<std::byte>(0),
        static_cast<std::byte>(payload.size()),
    };
    const auto payloadBytes = bytes(payload);
    result.insert(result.end(), payloadBytes.begin(), payloadBytes.end());
    return result;
}

} // namespace

TEST(WriteQueueTest, SerializesFrameAndTracksPendingBytes)
{
    WriteQueue queue(1024);
    const auto payload = bytes("hello");
    queue.enqueue(payload);

    // 4-byte header + 5-byte payload = 9 bytes total.
    EXPECT_EQ(queue.pendingBytes(), 9U);
    EXPECT_EQ(std::vector<std::byte>(queue.pendingData().begin(), queue.pendingData().end()),
              serialized("hello"));

    // Consume 2 bytes, verify offset tracking.
    queue.consume(2);
    EXPECT_EQ(queue.pendingBytes(), 7U);
    EXPECT_EQ(queue.pendingData().size(), 7U);
    queue.consume(7);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.pendingBytes(), 0U);
}

TEST(WriteQueueTest, PreservesFrameOrder)
{
    WriteQueue queue(1024);
    queue.enqueue(bytes("one"));
    queue.enqueue(bytes("two"));

    std::vector<std::byte> written;
    const auto result = queue.writeNonBlocking([&written](std::span<std::byte> data) {
        written.insert(written.end(), data.begin(), data.end());
        return IoResult{data.size(), IoStatus::Progress};
    });

    auto expected = serialized("one");
    const auto second = serialized("two");
    expected.insert(expected.end(), second.begin(), second.end());
    EXPECT_EQ(result.status, IoStatus::Progress);
    EXPECT_EQ(result.bytes, expected.size());
    EXPECT_EQ(written, expected);
    EXPECT_TRUE(queue.empty());
}

TEST(WriteQueueTest, RetainsOffsetAfterBlockedWrite)
{
    WriteQueue queue(1024);
    queue.enqueue(bytes("partial"));
    std::vector<std::byte> written;

    // First write: only 3 bytes accepted, then blocked.
    const auto first = queue.writeNonBlocking([&written](std::span<std::byte> data) {
        const auto count = std::min<std::size_t>(3, data.size());
        written.insert(written.end(), data.begin(), data.begin() + count);
        return IoResult{count, IoStatus::Blocked};
    });

    EXPECT_EQ(first.status, IoStatus::Blocked);
    EXPECT_EQ(first.bytes, 3U);
    EXPECT_EQ(queue.pendingBytes(), serialized("partial").size() - 3U);

    // Second write: drain the rest.
    const auto second = queue.writeNonBlocking([&written](std::span<std::byte> data) {
        written.insert(written.end(), data.begin(), data.end());
        return IoResult{data.size(), IoStatus::Progress};
    });

    EXPECT_EQ(second.status, IoStatus::Progress);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(written, serialized("partial"));
}

TEST(WriteQueueTest, RejectsOverflowWithoutChangingQueue)
{
    WriteQueue queue(8);
    queue.enqueue(bytes("four"));
    const auto pending = queue.pendingBytes();

    EXPECT_THROW(queue.enqueue(bytes("x")), miniasyncnetsockets::WriteQueueOverflow);
    EXPECT_EQ(queue.pendingBytes(), pending);
}

TEST(WriteQueueTest, EmptyQueueDoesNotCallWriter)
{
    WriteQueue queue(1024);
    bool called{false};

    const auto result = queue.writeNonBlocking([&called](std::span<std::byte>) {
        called = true;
        return IoResult{0, IoStatus::Progress};
    });

    EXPECT_FALSE(called);
    EXPECT_EQ(result.bytes, 0U);
    EXPECT_EQ(result.status, IoStatus::Progress);
}
