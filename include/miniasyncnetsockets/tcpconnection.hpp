/**
 * @file tcpconnection.hpp
 * @brief Represents a single framed TCP connection.
 */

#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "mininetsockets/endpoint.hpp"
#include "mininetsockets/tcpstream.hpp"

namespace miniruntime::event
{
class EventHandle;
}

namespace miniasyncnetsockets
{

/**
 * @brief A frame payload: a byte vector received or sent over a connection.
 */
using Frame = std::vector<std::byte>;

namespace detail
{
class ConnectionState;
class ServerState;
class ClientState;
} // namespace detail

class TcpConnection;

/**
 * @brief Callback invoked when a complete frame is received.
 */
using FrameHandler = std::function<void(TcpConnection&, Frame)>;

/**
 * @brief Callback invoked when a new connection is established.
 */
using ConnectionHandler = std::function<void(TcpConnection&)>;

/**
 * @brief Callback invoked when a connection is closed.
 */
using CloseHandler = std::function<void(const TcpConnection&)>;

/**
 * @brief Callback invoked when a connection-level error occurs.
 */
using ErrorHandler = std::function<void(TcpConnection&, std::exception_ptr)>;

/**
 * @brief A framed TCP connection managed by the event loop.
 *
 * Created internally by TcpServer. Users interact with it through callbacks.
 * Non-copyable and non-movable.
 */
class TcpConnection
{
public:
    ~TcpConnection() noexcept;

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&&) = delete;
    TcpConnection& operator=(TcpConnection&&) = delete;

    /**
     * @brief Sends a framed payload to the remote peer.
     * @param payload The bytes to send.
     * @throws InvalidState if the connection is closed.
     * @throws FrameTooLarge if the payload exceeds maxFrameSize.
     */
    void sendFrame(std::span<const std::byte> payload);

    /**
     * @brief Closes the connection and releases resources.
     */
    void close() noexcept;

    /**
     * @brief Returns true if the connection is still open.
     */
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Returns the local endpoint of the connection.
     */
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;

    /**
     * @brief Returns the remote endpoint of the connection.
     */
    [[nodiscard]] mininetsockets::Endpoint remoteEndpoint() const;

private:
    TcpConnection(mininetsockets::TcpStream stream, std::size_t maxFrameSize,
        std::size_t maxPendingWriteBytes, FrameHandler onFrame, CloseHandler onClose,
        ErrorHandler onError);

    void attachEvent(miniruntime::event::EventHandle event);
    void onEvent();
    void handleError(std::exception_ptr error) noexcept;

    friend class detail::ServerState;
    friend class detail::ClientState;

    std::unique_ptr<detail::ConnectionState> m_state;
};

} // namespace miniasyncnetsockets
