/**
 * @file tcpclient.hpp
 * @brief Framed TCP client with epoll-based event loop.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <span>

#include "miniasyncnetsockets/tcpconnection.hpp"
#include "mininetsockets/endpoint.hpp"

namespace miniasyncnetsockets
{

namespace detail
{
class ClientState;
}

/**
 * @brief Configuration options for TcpClient.
 * maxFrameSize - Maximum frame payload size in bytes.
 * maxPendingWriteBytes - Maximum queued write bytes.
 * connectTimeout - Non-blocking connect timeout.
 */
struct ClientOptions {
    std::size_t maxFrameSize{1024U * 1024U};
    std::size_t maxPendingWriteBytes{4U * 1024U * 1024U};
    std::chrono::milliseconds connectTimeout{10'000};
};

class TcpClient;

/**
 * @brief Callback invoked when a frame is received from the server.
 */
using ClientFrameHandler = std::function<void(TcpClient&, Frame)>;

/**
 * @brief Callback invoked when a client-level error occurs.
 */
using ClientErrorHandler = std::function<void(TcpClient&, std::exception_ptr)>;

/**
 * @brief Callbacks for TcpClient events.
 * onConnected - connection is established.
 * onFrame - complete frame is received.
 * onClose - connection is closed.
 * onError - error occurred.
 */
struct ClientCallbacks {
    std::function<void(TcpClient&)> onConnected;
    ClientFrameHandler onFrame;
    std::function<void(TcpClient&)> onClose;
    ClientErrorHandler onError;
};

/**
 * @brief A framed TCP client that connects to a server and dispatches events.
 *
 * Initiates a non-blocking connect with a timeout, then reads/writes frames
 * in a dedicated epoll-based event-loop thread.
 * Non-copyable and non-movable.
 */
class TcpClient
{
public:
    /**
     * @brief Constructs a client targeting the given endpoint.
     * @param endpoint The server address to connect to.
     * @param callbacks User-provided event callbacks.
     * @param options Optional client configuration.
     */
    TcpClient(mininetsockets::Endpoint endpoint, ClientCallbacks callbacks,
        ClientOptions options = {});
    ~TcpClient() noexcept;

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(TcpClient&&) = delete;

    /**
     * @brief Starts the client: initiates a non-blocking connect and spawns the
     * event-loop thread.
     * @throws InvalidState if the client has already been started or connectTimeout is
     * non-positive.
     */
    void start();

    /**
     * @brief Stops the client and closes the connection.
     */
    void stop() noexcept;

    /**
     * @brief Sends a framed payload to the server.
     * @param payload The bytes to send.
     * @throws InvalidState if the client is not connected.
     * @throws FrameTooLarge if the payload exceeds maxFrameSize.
     */
    void sendFrame(std::span<const std::byte> payload);

private:
    std::unique_ptr<detail::ClientState> m_state;
};

} // namespace miniasyncnetsockets
