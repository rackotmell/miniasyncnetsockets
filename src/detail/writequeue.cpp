/// @file writequeue.cpp
/// @brief Implementation of the buffered write queue.

#include "writequeue.hpp"
#include "miniasyncnetsockets/errors.hpp"

#include <cstdint>
#include <limits>
#include <utility>

namespace miniasyncnetsockets::detail
{

WriteQueue::WriteQueue(std::size_t maxPendingWriteBytes)
    : m_maxPendingWriteBytes(maxPendingWriteBytes)
{
}

void WriteQueue::enqueue(std::span<const std::byte> payload)
{
    // Reject payloads that don't fit in a 32-bit protocol header.
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw FrameTooLarge("frame payload cannot be represented by protocol header");
    }

    const std::size_t serializedSize = headerSize + payload.size();
    // Check overflow before allocating.
    if (serializedSize > m_maxPendingWriteBytes ||
        m_pendingBytes > m_maxPendingWriteBytes - serializedSize) {
        throw WriteQueueOverflow("pending write queue exceeds maxPendingWriteBytes");
    }

    // Serialize: 4-byte big-endian length header + payload.
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::byte> buffer;
    buffer.reserve(serializedSize);
    buffer.push_back(static_cast<std::byte>((size >> 24U) & 0xffU));
    buffer.push_back(static_cast<std::byte>((size >> 16U) & 0xffU));
    buffer.push_back(static_cast<std::byte>((size >> 8U) & 0xffU));
    buffer.push_back(static_cast<std::byte>(size & 0xffU));
    buffer.insert(buffer.end(), payload.begin(), payload.end());

    m_pendingBytes += buffer.size();
    m_buffers.push_back(std::move(buffer));
}

mininetsockets::IoResult WriteQueue::writeNonBlocking(const WriteFunction& writer)
{
    std::size_t written{0};
    while (!empty()) {
        const auto data = pendingData();
        const auto result = writer(data);
        if (result.bytes > data.size()) throw InvalidState("writer returned too many bytes");

        consume(result.bytes);
        written += result.bytes;
        // Stop draining if the writer is blocked or produced no output.
        if (result.status != mininetsockets::IoStatus::Progress || result.bytes == 0) {
            return {written, result.status};
        }
    }

    return {written, mininetsockets::IoStatus::Progress};
}

bool WriteQueue::empty() const noexcept { return m_buffers.empty(); }

std::size_t WriteQueue::pendingBytes() const noexcept { return m_pendingBytes; }

// Returns the unwritten portion of the front buffer.
std::span<std::byte> WriteQueue::pendingData() noexcept
{
    if (empty()) return {};
    auto& buffer = m_buffers.front();
    return {buffer.data() + m_frontOffset, buffer.size() - m_frontOffset};
}

void WriteQueue::consume(std::size_t bytes)
{
    if (bytes == 0) return;

    const auto data = pendingData();
    if (bytes > data.size()) throw InvalidState("write queue offset exceeds current buffer");

    m_frontOffset += bytes;
    m_pendingBytes -= bytes;
    // Rotate to the next buffer when the front one is fully consumed.
    if (m_frontOffset == m_buffers.front().size()) {
        m_buffers.pop_front();
        m_frontOffset = 0;
    }
}

} // namespace miniasyncnetsockets::detail
