/**
 * @file tcpserver.hpp
 * @brief Framed TCP server with epoll-based event loop.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <sys/socket.h>

#include "miniasyncnetsockets/tcpconnection.hpp"
#include "mininetsockets/endpoint.hpp"

namespace miniasyncnetsockets
{

namespace detail
{
class ServerState;
}

/**
 * @brief Configuration options for TcpServer.
 */
struct ServerOptions
{
    int backlog{SOMAXCONN};                               ///< listen() backlog.
    std::size_t maxFrameSize{1024U * 1024U};              ///< Maximum frame payload size in bytes.
    std::size_t maxPendingWriteBytes{4U * 1024U * 1024U}; ///< Maximum queued write bytes per connection.
    std::size_t maxConnections{0};                         ///< 0 = unlimited.
};

/**
 * @brief Callbacks for TcpServer events.
 */
struct ServerCallbacks
{
    FrameHandler onFrame;           ///< Called when a complete frame is received from a connection.
    ConnectionHandler onConnection; ///< Called when a new connection is accepted.
    CloseHandler onClose;           ///< Called when a connection is closed.
    ErrorHandler onError;           ///< Called when an error occurs on a connection.
};

/**
 * @brief A framed TCP server that accepts connections and dispatches events.
 *
 * Listens on a given endpoint, accepts connections in a dedicated thread,
 * and invokes user callbacks for frames, connections, errors, and closes.
 * Non-copyable and non-movable.
 */
class TcpServer
{
public:
    /**
     * @brief Constructs a server bound to the given endpoint.
     * @param endpoint The address to listen on.
     * @param callbacks User-provided event callbacks.
     * @param options Optional server configuration.
     */
    TcpServer(mininetsockets::Endpoint endpoint,
              ServerCallbacks callbacks,
              ServerOptions options = {});
    ~TcpServer() noexcept;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    /**
     * @brief Starts the server: binds the listener and spawns the event-loop thread.
     * @throws InvalidState if the server has already been started.
     */
    void start();

    /**
     * @brief Stops the server and closes all active connections.
     */
    void stop() noexcept;

    /**
     * @brief Returns true if the server is currently running.
     */
    [[nodiscard]] bool isRunning() const noexcept;

    /**
     * @brief Returns the local endpoint the server is listening on.
     * @throws InvalidState if the server has not been started.
     */
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;

private:
    std::unique_ptr<detail::ServerState> m_state;
};

} // namespace miniasyncnetsockets
