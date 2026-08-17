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

struct ServerOptions
{
    int backlog{SOMAXCONN};
    std::size_t maxFrameSize{1024U * 1024U};
    std::size_t maxPendingWriteBytes{4U * 1024U * 1024U};
    std::size_t maxConnections{0};
};

struct ServerCallbacks
{
    FrameHandler onFrame;
    ConnectionHandler onConnection;
    CloseHandler onClose;
    ErrorHandler onError;
};

class TcpServer
{
public:
    TcpServer(mininetsockets::Endpoint endpoint,
              ServerCallbacks callbacks,
              ServerOptions options = {});
    ~TcpServer() noexcept;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    void start();
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;

private:
    std::unique_ptr<detail::ServerState> m_state;
};

} // namespace miniasyncnetsockets
