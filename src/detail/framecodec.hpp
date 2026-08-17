/// @file framecodec.hpp
/// @brief Internal state-machine decoder for the 4-byte-header framing protocol.

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <span>

#include "miniasyncnetsockets/tcpconnection.hpp"

namespace miniasyncnetsockets::detail
{

// State of the frame decoder.
enum class FrameCodecState {
    ReadingHeader,  ///< Accumulating the 4-byte frame header.
    ReadingPayload, ///< Reading the payload body.
    Closed          ///< Codec is closed and no longer usable.
};

// Decodes a stream of bytes into complete frames using a 4-byte big-endian header.
//
// Feed raw socket data via consume(). Complete frames are dispatched to the
// provided callback. Handles partial reads and fragmented payloads.
class FrameCodec
{
public:
    using FrameCallback = std::function<void(Frame)>;

    explicit FrameCodec(std::size_t maxFrameSize);

    // Feeds raw input bytes into the decoder, calling onFrame for each complete frame.
    void consume(std::span<const std::byte> input, const FrameCallback& onFrame);

    // Signals end-of-stream. Throws ProtocolError if a frame was incomplete.
    void endOfStream();

    // Forcefully closes the codec, discarding any buffered data.
    void close() noexcept;

    [[nodiscard]] FrameCodecState state() const noexcept;

private:
    static constexpr std::size_t headerSize{4};

    // Decodes the 4-byte big-endian header into a payload size.
    [[nodiscard]] std::size_t decodePayloadSize() const noexcept;

    // Accumulates header bytes. Returns true when header is complete.
    bool consumeHeader(std::span<const std::byte> input, std::size_t& offset,
        const FrameCallback& onFrame);

    // Accumulates payload bytes. Returns true when a frame is dispatched.
    bool consumePayload(std::span<const std::byte> input, std::size_t& offset,
        const FrameCallback& onFrame);

    // Throws InvalidState when the codec is already closed.
    [[noreturn]] void throwClosed() const;

    FrameCodecState m_state{FrameCodecState::ReadingHeader};

    const std::size_t m_maxFrameSize;
    std::array<std::byte, headerSize> m_header{};
    Frame m_payload;

    std::size_t m_headerBytes{0};
    std::size_t m_payloadBytes{0};
};

} // namespace miniasyncnetsockets::detail
