/// @file connectionstate.hpp
/// @brief Internal per-connection state object (pimpl behind TcpConnection).

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

// Holds all internal state for a single TcpConnection.
//
// Owns the socket, frame codec, write queue, and user callbacks.
// Integrates with the epoll event loop via an EventHandle.
class ConnectionState
{
public:
    ConnectionState(mininetsockets::TcpStream stream,
                    std::size_t maxFrameSize,
                    std::size_t maxPendingWriteBytes,
                    FrameHandler onFrame,
                    CloseHandler onClose,
                    ErrorHandler onError);

    // Registers the epoll event handle for this connection.
    void attachEvent(miniruntime::event::EventHandle event);

    // Main event-loop entry point: reads available data and flushes writes.
    void onEvent(TcpConnection& owner);

    // Enqueues a frame for sending; enables EPOLLOUT if the queue was empty.
    void sendFrame(std::span<const std::byte> payload);

    // Reports an error to the user callback and closes the connection.
    void handleError(TcpConnection& owner, std::exception_ptr error) noexcept;

    // Closes the connection and invokes the onClose callback.
    void close(TcpConnection& owner) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;
    [[nodiscard]] mininetsockets::Endpoint remoteEndpoint() const;

private:
    static constexpr std::size_t readBufferSize{64U * 1024U};

    // Drains the socket into the frame codec, dispatching complete frames.
    void readAvailable(TcpConnection& owner);

    // Drains the write queue to the socket.
    void flushWrites();

    // Safely invokes the onError callback, swallowing exceptions.
    void reportError(TcpConnection& owner, std::exception_ptr error) noexcept;

    mininetsockets::TcpStream m_stream;
    std::optional<miniruntime::event::EventHandle> m_event;
    const std::size_t m_maxFrameSize;
    FrameCodec m_codec;
    WriteQueue m_writeQueue;
    FrameHandler m_onFrame;
    CloseHandler m_onClose;
    ErrorHandler m_onError;
    bool m_open{true}; ///< False after close() has been called.
};

} // namespace miniasyncnetsockets::detail
