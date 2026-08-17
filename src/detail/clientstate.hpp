#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

#include "framecodec.hpp"
#include "writequeue.hpp"
#include "miniasyncnetsockets/tcpclient.hpp"
#include "miniruntime/event/eventloop.h"
#include "miniruntime/event/handle.h"
#include "mininetsockets/pendingtcpstream.hpp"
#include "mininetsockets/tcpstream.hpp"

namespace miniasyncnetsockets::detail
{

class ClientState
{
public:
    ClientState(mininetsockets::Endpoint endpoint,
                ClientCallbacks callbacks,
                ClientOptions options);
    ~ClientState() noexcept;

    void start(TcpClient& owner);
    void stop() noexcept;
    void sendFrame(std::span<const std::byte> payload);

private:
    enum class Lifecycle
    {
        Constructed,
        Connecting,
        Running,
        Stopped
    };

    void run() noexcept;
    void onEvent();
    void onConnectTimeout();
    void finishConnect();
    void readAvailable();
    void flushWrites();
    void handleError(std::exception_ptr error) noexcept;
    void reportError(std::exception_ptr error) noexcept;
    void close(std::exception_ptr error) noexcept;
    void cleanupAfterRun() noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    miniruntime::event::EventLoop m_loop;
    mininetsockets::Endpoint m_endpoint;
    ClientCallbacks m_callbacks;
    ClientOptions m_options;
    FrameCodec m_codec;
    WriteQueue m_writeQueue;
    std::optional<mininetsockets::PendingTcpStream> m_pending;
    std::optional<mininetsockets::TcpStream> m_stream;
    std::optional<miniruntime::event::EventHandle> m_event;
    std::optional<miniruntime::event::TimerHandle> m_connectTimer;
    std::thread m_thread;
    std::thread::id m_loopThreadId;
    TcpClient* m_owner{nullptr};
    Lifecycle m_lifecycle{Lifecycle::Constructed};
    bool m_connected{false};
    bool m_open{true};
    bool m_closeNotified{false};
    bool m_joinInProgress{false};
};

} // namespace miniasyncnetsockets::detail
