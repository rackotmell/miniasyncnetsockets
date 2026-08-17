/// @file serverstate.hpp
/// @brief Internal server state object (pimpl behind TcpServer).

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

// Holds all internal state for TcpServer.
//
// Manages the event loop, listener socket, active connections, and the
// dedicated event-loop thread. Handles accept(), connection lifecycle,
// and thread synchronization for stop().
class ServerState
{
public:
    ServerState(mininetsockets::Endpoint endpoint,
                ServerCallbacks callbacks,
                ServerOptions options);
    ~ServerState() noexcept;

    // Binds the listener and starts the event-loop thread.
    void start();

    // Stops the event loop and joins the thread.
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;

private:
    enum class Lifecycle
    {
        Constructed, ///< Not yet started.
        Running,     ///< Event loop is active.
        Stopped      ///< Event loop has finished.
    };

    // Event-loop thread entry point.
    void run();

    // Accepts pending connections from the listener.
    void acceptConnections();

    // Creates a TcpConnection and registers it in the event loop.
    void addConnection(mininetsockets::TcpStream stream);

    // Removes closed connections from the map.
    void cleanupClosedConnections();

    // Post-loop cleanup: resets listener, closes connections, updates state.
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
    std::thread::id m_loopThreadId;    ///< Thread ID of the event-loop thread.
    Lifecycle m_lifecycle{Lifecycle::Constructed};
    bool m_joinInProgress{false};      ///< Guards against concurrent stop() calls.
};

} // namespace miniasyncnetsockets::detail
