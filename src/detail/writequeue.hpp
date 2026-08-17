#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <span>
#include <vector>

#include "miniasyncnetsockets/errors.hpp"
#include "mininetsockets/io.hpp"

namespace miniasyncnetsockets::detail
{

class WriteQueue
{
public:
    using WriteFunction =
        std::function<mininetsockets::IoResult(std::span<std::byte> data)>;

    explicit WriteQueue(std::size_t maxPendingWriteBytes);

    void enqueue(std::span<const std::byte> payload);
    mininetsockets::IoResult writeNonBlocking(const WriteFunction& writer);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t pendingBytes() const noexcept;
    [[nodiscard]] std::span<std::byte> pendingData() noexcept;
    void consume(std::size_t bytes);

private:
    static constexpr std::size_t headerSize{4};

    const std::size_t m_maxPendingWriteBytes;
    std::deque<std::vector<std::byte>> m_buffers;
    std::size_t m_frontOffset{0};
    std::size_t m_pendingBytes{0};
};

} // namespace miniasyncnetsockets::detail
