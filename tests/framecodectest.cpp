/// @file framecodectest.cpp
/// @brief Unit tests for detail::FrameCodec.

#include "detail/framecodec.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using miniasyncnetsockets::Frame;
using miniasyncnetsockets::detail::FrameCodec;
using miniasyncnetsockets::detail::FrameCodecState;

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

// Serializes a payload with a 4-byte big-endian header.
std::vector<std::byte> serializedFrame(std::string_view payload)
{
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::byte> result{
        static_cast<std::byte>((size >> 24U) & 0xffU),
        static_cast<std::byte>((size >> 16U) & 0xffU),
        static_cast<std::byte>((size >> 8U) & 0xffU),
        static_cast<std::byte>(size & 0xffU),
    };
    auto payloadBytes = bytes(payload);
    result.insert(result.end(), payloadBytes.begin(), payloadBytes.end());
    return result;
}

Frame asFrame(std::string_view text)
{
    return bytes(text);
}

} // namespace

TEST(FrameCodecTest, ParsesCompleteFrame)
{
    FrameCodec codec(1024);
    std::vector<Frame> frames;
    const auto input = serializedFrame("hello");

    codec.consume(input, [&frames](Frame frame) { frames.push_back(std::move(frame)); });

    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames.front(), asFrame("hello"));
    EXPECT_EQ(codec.state(), FrameCodecState::ReadingHeader);
}

TEST(FrameCodecTest, AccumulatesHeaderOneByteAtATime)
{
    FrameCodec codec(1024);
    std::vector<Frame> frames;
    const auto input = serializedFrame("header");

    // Feed one byte at a time to exercise partial header accumulation.
    for (const auto byte : input) {
        codec.consume(std::span<const std::byte>(&byte, 1),
                      [&frames](Frame frame) { frames.push_back(std::move(frame)); });
    }

    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames.front(), asFrame("header"));
}

TEST(FrameCodecTest, AccumulatesPayloadFragments)
{
    FrameCodec codec(1024);
    std::vector<Frame> frames;
    const auto input = serializedFrame("fragmented");

    // Feed only the header + 2 payload bytes first.
    codec.consume(std::span<const std::byte>(input.data(), 6),
                  [&frames](Frame frame) { frames.push_back(std::move(frame)); });
    EXPECT_TRUE(frames.empty());
    EXPECT_EQ(codec.state(), FrameCodecState::ReadingPayload);

    // Feed the remaining payload bytes.
    codec.consume(std::span<const std::byte>(input.data() + 6, input.size() - 6),
                  [&frames](Frame frame) { frames.push_back(std::move(frame)); });

    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames.front(), asFrame("fragmented"));
}

TEST(FrameCodecTest, ParsesSeveralFramesFromOneInput)
{
    FrameCodec codec(1024);
    std::vector<Frame> frames;
    auto input = serializedFrame("one");
    const auto second = serializedFrame("two");
    input.insert(input.end(), second.begin(), second.end());

    codec.consume(input, [&frames](Frame frame) { frames.push_back(std::move(frame)); });

    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0], asFrame("one"));
    EXPECT_EQ(frames[1], asFrame("two"));
}

TEST(FrameCodecTest, AcceptsEmptyFrame)
{
    FrameCodec codec(0);
    std::vector<Frame> frames;
    const auto input = serializedFrame("");

    codec.consume(input, [&frames](Frame frame) { frames.push_back(std::move(frame)); });

    ASSERT_EQ(frames.size(), 1U);
    EXPECT_TRUE(frames.front().empty());
}

TEST(FrameCodecTest, RejectsFrameAboveLimitBeforePayloadAllocation)
{
    FrameCodec codec(2);
    const std::array<std::byte, 4> header{
        static_cast<std::byte>(0),
        static_cast<std::byte>(0),
        static_cast<std::byte>(0),
        static_cast<std::byte>(3),
    };

    EXPECT_THROW(codec.consume(header, FrameCodec::FrameCallback{}),
                 miniasyncnetsockets::FrameTooLarge);
    EXPECT_EQ(codec.state(), FrameCodecState::Closed);
}

TEST(FrameCodecTest, RejectsEofInsideHeader)
{
    const auto input = serializedFrame("payload");
    // Feed 1..3 header bytes then call endOfStream -- all should throw.
    for (std::size_t bytesReceived = 1; bytesReceived < 4; ++bytesReceived) {
        FrameCodec codec(1024);
        codec.consume(std::span<const std::byte>(input.data(), bytesReceived),
                      FrameCodec::FrameCallback{});

        EXPECT_THROW(codec.endOfStream(), miniasyncnetsockets::ProtocolError);
        EXPECT_EQ(codec.state(), FrameCodecState::Closed);
    }
}

TEST(FrameCodecTest, RejectsEofInsidePayload)
{
    const auto input = serializedFrame("payload");
    // Feed 5..N-1 bytes (partial payload) then call endOfStream.
    for (std::size_t bytesReceived = 5; bytesReceived < input.size(); ++bytesReceived) {
        FrameCodec codec(1024);
        codec.consume(std::span<const std::byte>(input.data(), bytesReceived),
                      FrameCodec::FrameCallback{});

        EXPECT_THROW(codec.endOfStream(), miniasyncnetsockets::ProtocolError);
        EXPECT_EQ(codec.state(), FrameCodecState::Closed);
    }
}

TEST(FrameCodecTest, EofAfterCompleteFrameIsClean)
{
    FrameCodec codec(1024);
    const auto input = serializedFrame("complete");
    codec.consume(input, FrameCodec::FrameCallback{});

    EXPECT_NO_THROW(codec.endOfStream());
    EXPECT_EQ(codec.state(), FrameCodecState::Closed);
}
