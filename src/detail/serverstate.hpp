#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include "miniasyncnetsockets/tcpserver.hpp"
#include "miniruntime/event/eventloop.h"
#include "miniruntime/event/handle.h"
#include "mininetsockets/tcplistener.hpp"

namespace miniasyncnetsockets::detail
{

class ServerState
{
public:
    ServerState(mininetsockets::Endpoint endpoint,
                ServerCallbacks callbacks,
                ServerOptions options);
    ~ServerState() noexcept;

    void start();
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;

private:
    enum class Lifecycle
    {
        Constructed,
        Running,
        Stopped
    };

    void run();
    void acceptConnections();
    void addConnection(mininetsockets::TcpStream stream);
    void cleanupClosedConnections();
    void cleanupAfterRun() noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    miniruntime::event::EventLoop m_loop;
    mininetsockets::Endpoint m_endpoint;
    ServerCallbacks m_callbacks;
    ServerOptions m_options;
    std::optional<mininetsockets::TcpListener> m_listener;
    std::optional<miniruntime::event::EventHandle> m_listenerEvent;
    std::unordered_map<TcpConnection*, std::unique_ptr<TcpConnection>> m_connections;
    std::thread m_thread;
    std::thread::id m_loopThreadId;
    Lifecycle m_lifecycle{Lifecycle::Constructed};
    bool m_joinInProgress{false};
};

} // namespace miniasyncnetsockets::detail
