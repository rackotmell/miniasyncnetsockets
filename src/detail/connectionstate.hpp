#pragma once

#include <cstddef>
#include <exception>
#include <optional>

#include "framecodec.hpp"
#include "writequeue.hpp"
#include "miniasyncnetsockets/tcpconnection.hpp"
#include "miniruntime/event/handle.h"
#include "mininetsockets/tcpstream.hpp"

namespace miniasyncnetsockets::detail
{

class ConnectionState
{
public:
    ConnectionState(mininetsockets::TcpStream stream,
                    std::size_t maxFrameSize,
                    std::size_t maxPendingWriteBytes,
                    FrameHandler onFrame,
                    CloseHandler onClose,
                    ErrorHandler onError);

    void attachEvent(miniruntime::event::EventHandle event);
    void onEvent(TcpConnection& owner);
    void sendFrame(std::span<const std::byte> payload);
    void handleError(TcpConnection& owner, std::exception_ptr error) noexcept;
    void close(TcpConnection& owner) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;
    [[nodiscard]] mininetsockets::Endpoint remoteEndpoint() const;

private:
    static constexpr std::size_t readBufferSize{64U * 1024U};

    void readAvailable(TcpConnection& owner);
    void flushWrites();
    void reportError(TcpConnection& owner, std::exception_ptr error) noexcept;

    mininetsockets::TcpStream m_stream;
    std::optional<miniruntime::event::EventHandle> m_event;
    const std::size_t m_maxFrameSize;
    FrameCodec m_codec;
    WriteQueue m_writeQueue;
    FrameHandler m_onFrame;
    CloseHandler m_onClose;
    ErrorHandler m_onError;
    bool m_open{true};
};

} // namespace miniasyncnetsockets::detail
