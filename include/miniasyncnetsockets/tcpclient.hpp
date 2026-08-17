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

struct ClientOptions
{
    std::size_t maxFrameSize{1024U * 1024U};
    std::size_t maxPendingWriteBytes{4U * 1024U * 1024U};
    std::chrono::milliseconds connectTimeout{10'000};
};

class TcpClient;

using ClientFrameHandler = std::function<void(TcpClient&, Frame)>;
using ClientErrorHandler = std::function<void(TcpClient&, std::exception_ptr)>;

struct ClientCallbacks
{
    std::function<void(TcpClient&)> onConnected;
    ClientFrameHandler onFrame;
    std::function<void(TcpClient&)> onClose;
    ClientErrorHandler onError;
};

class TcpClient
{
public:
    TcpClient(mininetsockets::Endpoint endpoint,
              ClientCallbacks callbacks,
              ClientOptions options = {});
    ~TcpClient() noexcept;

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&&) = delete;
    TcpClient& operator=(TcpClient&&) = delete;

    void start();
    void stop() noexcept;
    void sendFrame(std::span<const std::byte> payload);

private:
    std::unique_ptr<detail::ClientState> m_state;
};

} // namespace miniasyncnetsockets
