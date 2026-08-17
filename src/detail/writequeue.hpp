/// @file writequeue.hpp
/// @brief Internal write buffer that serializes frames and drains via non-blocking writes.

#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <span>
#include <vector>

#include "mininetsockets/io.hpp"

namespace miniasyncnetsockets::detail
{

// Buffered write queue that prepends a 4-byte big-endian header to each payload.
//
// Enqueued payloads are serialized into individual buffers and drained one at a time
// via writeNonBlocking(). Partial writes are tracked via an internal offset.
class WriteQueue
{
public:
    using WriteFunction =
        std::function<mininetsockets::IoResult(std::span<std::byte> data)>;

    explicit WriteQueue(std::size_t maxPendingWriteBytes);

    // Serializes the payload with a 4-byte header and appends it to the queue.
    void enqueue(std::span<const std::byte> payload);

    // Drains the queue by calling the provided writer until blocked or empty.
    mininetsockets::IoResult writeNonBlocking(const WriteFunction& writer);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t pendingBytes() const noexcept;

    // Returns a span of the current front buffer (from the partial-write offset).
    [[nodiscard]] std::span<std::byte> pendingData() noexcept;

    // Advances the front buffer offset by the given number of bytes.
    void consume(std::size_t bytes);

private:
    static constexpr std::size_t headerSize{4};

    const std::size_t m_maxPendingWriteBytes;
    std::deque<std::vector<std::byte>> m_buffers; ///< Serialized frame buffers.
    std::size_t m_frontOffset{0};                 ///< Read offset into the front buffer.
    std::size_t m_pendingBytes{0};                ///< Total bytes queued across all buffers.
};

} // namespace miniasyncnetsockets::detail
