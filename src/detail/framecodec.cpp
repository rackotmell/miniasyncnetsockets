/// @file framecodec.cpp
/// @brief Implementation of the framing protocol state machine.

#include "framecodec.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace miniasyncnetsockets::detail
{

FrameCodec::FrameCodec(std::size_t maxFrameSize) : m_maxFrameSize(maxFrameSize) {}

void FrameCodec::consume(std::span<const std::byte> input, const FrameCallback& onFrame)
{
    if (m_state == FrameCodecState::Closed) throwClosed();

    std::size_t offset{0};
    while (offset < input.size()) {
        // Accumulate the 4-byte header one chunk at a time.
        if (m_state == FrameCodecState::ReadingHeader) {
            const std::size_t bytesToCopy = std::min(headerSize - m_headerBytes,
                                                     input.size() - offset);
            std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytesToCopy,
                        m_header.begin() + static_cast<std::ptrdiff_t>(m_headerBytes));
            offset += bytesToCopy;
            m_headerBytes += bytesToCopy;

            if (m_headerBytes != headerSize) continue;

            // Header complete: decode payload size and validate.
            const std::size_t payloadSize = decodePayloadSize();
            m_headerBytes = 0;
            if (payloadSize > m_maxFrameSize) {
                m_state = FrameCodecState::Closed;
                throw FrameTooLarge("frame payload exceeds maxFrameSize: " +
                                    std::to_string(payloadSize));
            }

            // Empty payload: dispatch immediately without entering ReadingPayload.
            if (payloadSize == 0) {
                if (onFrame) onFrame(Frame{});
                if (m_state == FrameCodecState::Closed) return;
                continue;
            }

            m_payload.resize(payloadSize);
            m_payloadBytes = 0;
            m_state = FrameCodecState::ReadingPayload;
        }

        // Accumulate payload bytes until complete.
        if (m_state == FrameCodecState::ReadingPayload) {
            const std::size_t bytesToCopy =
                std::min(m_payload.size() - m_payloadBytes, input.size() - offset);
            std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytesToCopy,
                        m_payload.begin() + static_cast<std::ptrdiff_t>(m_payloadBytes));
            offset += bytesToCopy;
            m_payloadBytes += bytesToCopy;

            if (m_payloadBytes != m_payload.size()) continue;

            // Payload complete: dispatch and reset for next frame.
            Frame frame = std::move(m_payload);
            m_payloadBytes = 0;
            m_state = FrameCodecState::ReadingHeader;
            if (onFrame) onFrame(std::move(frame));
            if (m_state == FrameCodecState::Closed) return;
        }
    }
}

void FrameCodec::endOfStream()
{
    if (m_state == FrameCodecState::Closed) return;

    // EOF is only clean if we're between frames (no partial header/payload).
    const bool incomplete = m_state == FrameCodecState::ReadingPayload || m_headerBytes != 0;
    m_state = FrameCodecState::Closed;
    m_payload.clear();
    m_payloadBytes = 0;
    if (incomplete) throw ProtocolError("end of stream inside frame");
}

void FrameCodec::close() noexcept
{
    m_state = FrameCodecState::Closed;
    m_headerBytes = 0;
    m_payload.clear();
    m_payloadBytes = 0;
}

FrameCodecState FrameCodec::state() const noexcept { return m_state; }

// Decodes a 4-byte big-endian unsigned integer from the header buffer.
std::size_t FrameCodec::decodePayloadSize() const noexcept
{
    const auto toInteger = [](std::byte value) {
        return static_cast<std::uint32_t>(std::to_integer<unsigned char>(value));
    };

    const std::uint32_t size = (toInteger(m_header[0]) << 24U) |
                               (toInteger(m_header[1]) << 16U) |
                               (toInteger(m_header[2]) << 8U) | toInteger(m_header[3]);
    return static_cast<std::size_t>(size);
}

[[noreturn]] void FrameCodec::throwClosed() const
{
    throw InvalidState("frame codec is closed");
}

} // namespace miniasyncnetsockets::detail
