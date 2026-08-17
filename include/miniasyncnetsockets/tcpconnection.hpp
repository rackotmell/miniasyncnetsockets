#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "miniasyncnetsockets/errors.hpp"
#include "mininetsockets/endpoint.hpp"
#include "mininetsockets/tcpstream.hpp"

namespace miniruntime::event
{
class EventHandle;
}

namespace miniasyncnetsockets
{

using Frame = std::vector<std::byte>;

namespace detail
{
class ConnectionState;
class ServerState;
}

class TcpConnection;

using FrameHandler = std::function<void(TcpConnection&, Frame)>;
using ConnectionHandler = std::function<void(TcpConnection&)>;
using CloseHandler = std::function<void(const TcpConnection&)>;
using ErrorHandler = std::function<void(TcpConnection&, std::exception_ptr)>;

class TcpConnection
{
public:
    ~TcpConnection() noexcept;

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&&) = delete;
    TcpConnection& operator=(TcpConnection&&) = delete;

    void sendFrame(std::span<const std::byte> payload);
    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;
    [[nodiscard]] mininetsockets::Endpoint remoteEndpoint() const;

private:
    TcpConnection(mininetsockets::TcpStream stream,
                  std::size_t maxFrameSize,
                  std::size_t maxPendingWriteBytes,
                  FrameHandler onFrame,
                  CloseHandler onClose,
                  ErrorHandler onError);

    void attachEvent(miniruntime::event::EventHandle event);
    void onEvent();
    void handleError(std::exception_ptr error) noexcept;

    friend class TcpServer;
    friend class detail::ServerState;

    std::unique_ptr<detail::ConnectionState> m_state;
};

} // namespace miniasyncnetsockets
