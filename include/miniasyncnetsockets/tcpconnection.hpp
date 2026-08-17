#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <span>
#include <vector>

#include "miniasyncnetsockets/errors.hpp"
#include "mininetsockets/endpoint.hpp"

namespace miniasyncnetsockets
{

using Frame = std::vector<std::byte>;

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
    TcpConnection();

    friend class TcpServer;
};

} // namespace miniasyncnetsockets
