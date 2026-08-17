#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include "miniasyncnetsockets/errors.hpp"
#include "miniasyncnetsockets/tcpconnection.hpp"

namespace miniasyncnetsockets::detail
{

enum class FrameCodecState
{
    ReadingHeader,
    ReadingPayload,
    Closed
};

class FrameCodec
{
public:
    using FrameCallback = std::function<void(Frame)>;

    explicit FrameCodec(std::size_t maxFrameSize);

    void consume(std::span<const std::byte> input, const FrameCallback& onFrame);
    void endOfStream();
    void close() noexcept;

    [[nodiscard]] FrameCodecState state() const noexcept;

private:
    static constexpr std::size_t headerSize{4};

    [[nodiscard]] std::size_t decodePayloadSize() const noexcept;
    [[noreturn]] void throwClosed() const;

    const std::size_t m_maxFrameSize;
    FrameCodecState m_state{FrameCodecState::ReadingHeader};
    std::array<std::byte, headerSize> m_header{};
    std::size_t m_headerBytes{0};
    Frame m_payload;
    std::size_t m_payloadBytes{0};
};

} // namespace miniasyncnetsockets::detail
