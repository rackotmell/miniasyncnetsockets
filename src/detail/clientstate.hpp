/// @file clientstate.hpp
/// @brief Internal client state object (pimpl behind TcpClient).

#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "miniasyncnetsockets/tcpclient.hpp"
#include "miniasyncnetsockets/tcpconnection.hpp"
#include "mininetsockets/pendingtcpstream.hpp"
#include "miniruntime/event/eventloop.h"
#include "miniruntime/event/handle.h"

namespace miniasyncnetsockets::detail
{

// Holds all internal state for TcpClient.
//
// Manages the non-blocking connect, event loop, and the dedicated event-loop
// thread. After connect completes, delegates to TcpConnection for frame I/O.
class ClientState
{
public:
    ClientState(mininetsockets::Endpoint endpoint, ClientCallbacks callbacks,
        ClientOptions options);
    ~ClientState() noexcept;

    // Initiates non-blocking connect and starts the event-loop thread.
    void start(TcpClient& owner);

    // Requests event-loop shutdown. The destructor joins the thread.
    void stop() noexcept;

    // Enqueues a frame for sending if connected.
    void sendFrame(std::span<const std::byte> payload);

private:
    enum class Lifecycle {
        Constructed, ///< Not yet started.
        Connecting,  ///< Non-blocking connect in progress.
        Running,     ///< Connected and processing events.
        Stopped      ///< Event loop has finished.
    };

    // Event-loop thread entry point.
    void run() noexcept;

    // Main event dispatcher: finishConnect, or delegate to TcpConnection.
    void onEvent();

    // Handles connect timeout expiration.
    void onConnectTimeout();

    // Completes the non-blocking connect and transitions to Running.
    void finishConnect();

    // Closes the connection due to an error.
    void handleError(std::exception_ptr error) noexcept;

    // Closes resources and invokes onClose if needed.
    void close(std::exception_ptr error) noexcept;

    // Post-loop cleanup: closes resources and notifies waiting stop() callers.
    void cleanupAfterRun() noexcept;

    std::thread m_thread;
    mutable std::mutex m_mutex;
    miniruntime::event::EventLoop m_loop;

    mininetsockets::Endpoint m_endpoint;
    ClientCallbacks m_callbacks;
    ClientOptions m_options;

    std::unique_ptr<TcpConnection> m_connection;

    std::optional<mininetsockets::PendingTcpStream> m_pending;
    std::optional<miniruntime::event::EventHandle> m_event;
    std::optional<miniruntime::event::TimerHandle> m_connectTimer;

    TcpClient* m_owner{nullptr}; ///< Back-pointer to the public TcpClient.

    Lifecycle m_lifecycle{Lifecycle::Constructed};

    bool m_connected{false};      ///< True after finishConnect() succeeds.
    bool m_open{true};            ///< False after close() has been called.
};

} // namespace miniasyncnetsockets::detail
