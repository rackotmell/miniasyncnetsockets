/// @file clientstate.cpp
/// @brief Implementation of the client event-loop thread and connect lifecycle.

#include "clientstate.hpp"
#include "miniasyncnetsockets/errors.hpp"

#include <mutex>
#include <stdexcept>
#include <sys/epoll.h>
#include <utility>

namespace miniasyncnetsockets::detail
{

ClientState::ClientState(
    mininetsockets::Endpoint endpoint, ClientCallbacks callbacks, ClientOptions options)
    : m_endpoint(std::move(endpoint)), m_callbacks(std::move(callbacks)),
      m_options(options)
{
}

ClientState::~ClientState() noexcept
{
    stop();
    if (m_thread.joinable()) m_thread.join();
}

// Initiates a non-blocking connect, registers events, and starts the event-loop thread.
void ClientState::start(TcpClient& owner)
{
    std::lock_guard lock(m_mutex);
    if (m_lifecycle != Lifecycle::Constructed) {
        throw InvalidState("client can only be started once");
    }
    if (m_options.connectTimeout <= std::chrono::milliseconds::zero()) {
        throw InvalidState("connectTimeout must be positive");
    }

    m_owner = &owner;
    try {
        // Begin non-blocking connect.
        m_pending.emplace(mininetsockets::PendingTcpStream::connect(m_endpoint));

        auto event = m_loop.createEvent(m_pending->fdView(), EPOLLOUT,
            miniruntime::event::EventType::SOCKET, [this](int) { onEvent(); });

        m_event.emplace(std::move(event));

        // Arm the connect timeout timer.
        m_connectTimer.emplace(
            m_loop.createTimer(m_options.connectTimeout, [this] { onConnectTimeout(); }));

        m_thread = std::thread([this] { run(); });
    } catch (...) {
        m_connectTimer.reset();
        m_event.reset();
        m_pending.reset();
        m_owner = nullptr;
        throw;
    }
    m_lifecycle = Lifecycle::Connecting;
}

void ClientState::stop() noexcept
{
    std::lock_guard lock(m_mutex);
    if (m_lifecycle == Lifecycle::Constructed) return;
    m_lifecycle = Lifecycle::Stopped;
    m_loop.stop();
}

void ClientState::sendFrame(std::span<const std::byte> payload)
{
    {
        std::lock_guard lock(m_mutex);

        if (!m_open || !m_connected) throw InvalidState("client is not connected");
        if (!m_connection) throw InvalidState("connection is not available");
    }

    m_connection->sendFrame(payload);
}

// Event-loop thread entry point.
void ClientState::run() noexcept
{
    try {
        m_loop.run();
    } catch (...) {
        handleError(std::current_exception());
    }
    cleanupAfterRun();
}

// Dispatches events: complete the connect first, then delegate to TcpConnection.
void ClientState::onEvent()
{
    if (!m_open) return;

    try {
        if (!m_connected) {
            finishConnect();
        }
        if (m_open && m_connected && m_connection) {
            m_connection->onEvent();
        }
    } catch (...) {
        handleError(std::current_exception());
    }
}

void ClientState::onConnectTimeout()
{
    if (!m_connected) {
        handleError(std::make_exception_ptr(std::runtime_error("connect timeout")));
    }
}

// Completes the non-blocking connect, creates TcpConnection, and transitions to Running.
void ClientState::finishConnect()
{
    if (!m_pending) throw InvalidState("pending client stream is not available");

    auto stream = m_pending->finishConnect();
    const int fd = stream.fdView();
    m_pending.reset();
    if (m_connectTimer) {
        m_connectTimer->cancel();
        m_connectTimer.reset();
    }

    // Reset the connect-phase event (EPOLLOUT on pending fd).
    m_event.reset();

    // Create TcpConnection from the established stream.
    m_connection = std::unique_ptr<TcpConnection>(new TcpConnection(std::move(stream),
        m_options.maxFrameSize, m_options.maxPendingWriteBytes, m_callbacks.onFrame,
        m_callbacks.onClose, m_callbacks.onError));

    // Create a new event (EPOLLIN) for the connected socket and attach to TcpConnection.
    auto event = m_loop.createEvent(fd, EPOLLIN, miniruntime::event::EventType::SOCKET,
        [this](int) { onEvent(); });
    m_connection->attachEvent(std::move(event));

    m_connected = true;
    {
        std::lock_guard lock(m_mutex);
        m_lifecycle = Lifecycle::Running;
    }

    if (m_callbacks.onConnected) m_callbacks.onConnected(*m_connection);
}

void ClientState::handleError(std::exception_ptr error) noexcept { close(error); }

// Closes resources. Delegates to TcpConnection for connection-level cleanup.
void ClientState::close(std::exception_ptr error) noexcept
{
    {
        std::lock_guard lock(m_mutex);

        if (!m_open) return;
        m_open = false;

        if (m_connectTimer) {
            m_connectTimer->cancel();
            m_connectTimer.reset();
        }
        m_event.reset();
        m_pending.reset();

        if (m_connection) {
            if (error) {
                m_connection->handleError(error);
            } else if (m_connected) {
                m_connection->close();
            }
            m_connection.reset();
        }
    }

    m_loop.stop();
}

// Post-loop cleanup: closes resources.
void ClientState::cleanupAfterRun() noexcept { close(nullptr); }

} // namespace miniasyncnetsockets::detail
