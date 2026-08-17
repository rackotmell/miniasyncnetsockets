/// @file serverstate.hpp
/// @brief Internal server state object (pimpl behind TcpServer).

#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

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

    // Requests event-loop shutdown. The destructor joins the thread.
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

    // Starts thread safely
    void startThread();

    // Accepts pending connections from the listener.
    void acceptConnections();

    // Creates a TcpConnection and registers it in the event loop.
    void addConnection(mininetsockets::TcpStream stream);

    // Removes closed connections from the map.
    void cleanupClosedConnections();

    // Post-loop cleanup: resets listener, closes connections, updates state.
    void cleanupAfterRun() noexcept;

    mutable std::mutex m_mutex;
    std::thread m_thread;
    miniruntime::event::EventLoop m_loop;

    Lifecycle m_lifecycle{Lifecycle::Constructed};

    mininetsockets::Endpoint m_endpoint;
    ServerCallbacks m_callbacks;
    ServerOptions m_options;

    std::optional<mininetsockets::TcpListener> m_listener;
    std::optional<miniruntime::event::EventHandle> m_listenerEvent;
    std::list<std::unique_ptr<TcpConnection>> m_connections;
};

} // namespace miniasyncnetsockets::detail
