/// @file clientstate.hpp
/// @brief Internal client state object (pimpl behind TcpClient).

#pragma once

#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

#include "framecodec.hpp"
#include "miniasyncnetsockets/tcpclient.hpp"
#include "mininetsockets/pendingtcpstream.hpp"
#include "mininetsockets/tcpstream.hpp"
#include "miniruntime/event/eventloop.h"
#include "miniruntime/event/handle.h"
#include "writequeue.hpp"

namespace miniasyncnetsockets::detail
{

// Holds all internal state for TcpClient.
//
// Manages the non-blocking connect, event loop, frame codec, write queue,
// and the dedicated event-loop thread. Handles the full client lifecycle.
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
    static constexpr std::size_t readBufferSize{64U * 1024U};

    enum class Lifecycle {
        Constructed, ///< Not yet started.
        Connecting,  ///< Non-blocking connect in progress.
        Running,     ///< Connected and processing events.
        Stopped      ///< Event loop has finished.
    };

    // Event-loop thread entry point.
    void run() noexcept;

    // Main event dispatcher: finishConnect, read, or write.
    void onEvent();

    // Handles connect timeout expiration.
    void onConnectTimeout();

    // Completes the non-blocking connect and transitions to Running.
    void finishConnect();

    // Drains the socket into the frame codec.
    void readAvailable();

    // Drains the write queue to the socket.
    void flushWrites();

    // Closes the connection due to an error.
    void handleError(std::exception_ptr error) noexcept;

    // Safely invokes the onError callback, swallowing exceptions.
    void reportError(std::exception_ptr error) noexcept;

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

    FrameCodec m_codec;
    WriteQueue m_writeQueue;

    std::optional<mininetsockets::PendingTcpStream> m_pending;
    std::optional<mininetsockets::TcpStream> m_stream;
    std::optional<miniruntime::event::EventHandle> m_event;
    std::optional<miniruntime::event::TimerHandle> m_connectTimer;

    TcpClient* m_owner{nullptr}; ///< Back-pointer to the public TcpClient.

    Lifecycle m_lifecycle{Lifecycle::Constructed};

    bool m_connected{false};      ///< True after finishConnect() succeeds.
    bool m_open{true};            ///< False after close() has been called.
    bool m_closeNotified{false};  ///< Ensures onClose is called at most once.
};

} // namespace miniasyncnetsockets::detail
