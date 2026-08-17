/// @file serverstate.cpp
/// @brief Implementation of the server event-loop thread and connection management.

#include "serverstate.hpp"
#include "miniasyncnetsockets/errors.hpp"

#include <exception>
#include <sys/epoll.h>
#include <utility>

namespace miniasyncnetsockets::detail
{

ServerState::ServerState(
    mininetsockets::Endpoint endpoint, ServerCallbacks callbacks, ServerOptions options)
    : m_endpoint(std::move(endpoint)), m_callbacks(std::move(callbacks)),
      m_options(options)
{
}

ServerState::~ServerState() noexcept
{
    stop();
    if (m_thread.joinable()) m_thread.join();
}

void ServerState::start()
{
    std::lock_guard lock(m_mutex);
    if (m_lifecycle != Lifecycle::Constructed) {
        throw InvalidState("server can only be started once");
    }

    // Bind the listener socket.
    std::optional<mininetsockets::TcpListener> listener;
    listener.emplace(mininetsockets::TcpListener::bind(
        m_endpoint, m_options.backlog, mininetsockets::ListenerMode::NonBlocking));

    // Register the listener fd in the event loop for accept events.
    auto listenerEvent = m_loop.createEvent(listener->fdView(), EPOLLIN,
        miniruntime::event::EventType::SOCKET, [this](int) { acceptConnections(); });

    m_listener = std::move(listener);
    m_listenerEvent.emplace(std::move(listenerEvent));

    startThread();

    m_lifecycle = Lifecycle::Running;
}

void ServerState::stop() noexcept
{
    std::lock_guard lock(m_mutex);
    if (m_lifecycle == Lifecycle::Constructed) return;
    m_lifecycle = Lifecycle::Stopped;
    m_loop.stop();
}

bool ServerState::isRunning() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_lifecycle == Lifecycle::Running;
}

mininetsockets::Endpoint ServerState::localEndpoint() const
{
    std::lock_guard lock(m_mutex);
    if (!m_listener) throw InvalidState("server is not running");
    return m_listener->localEndpoint();
}

// Event-loop thread entry point.
void ServerState::run()
{
    try {
        m_loop.run();
    } catch (...) {
        m_loop.stop();
    }
    cleanupAfterRun();
}

// Starts thread safely
void ServerState::startThread()
{
    try {
        m_thread = std::thread([this] { run(); });
    } catch (...) {
        // Roll back listener registration on thread creation failure.
        m_listenerEvent.reset();
        m_listener.reset();
        throw;
    }
}

// Accepts all pending connections, respecting maxConnections.
void ServerState::acceptConnections()
{
    if (!m_listener) return;
    cleanupClosedConnections();

    for (;;) {
        auto stream = m_listener->accept();

        if (!stream) break;

        // Reject new connections if at capacity.
        if (m_options.maxConnections != 0 &&
            m_connections.size() >= m_options.maxConnections) {
            continue;
        }

        try {
            addConnection(std::move(*stream));
        } catch (...) {
            // The accepted stream is closed by its RAII owner.
        }
    }

    cleanupClosedConnections();
}

// Creates a TcpConnection, registers it in the event loop, and notifies onConnection.
void ServerState::addConnection(mininetsockets::TcpStream stream)
{
    const int fd = stream.fdView();

    auto connection = std::unique_ptr<TcpConnection>(new TcpConnection(std::move(stream),
        m_options.maxFrameSize, m_options.maxPendingWriteBytes, m_callbacks.onFrame,
        m_callbacks.onClose, m_callbacks.onError));

    TcpConnection* const rawConnection = connection.get();

    auto event = m_loop.createEvent(fd, EPOLLIN, miniruntime::event::EventType::SOCKET,
        [rawConnection](int) { rawConnection->onEvent(); });

    rawConnection->attachEvent(std::move(event));
    m_connections.push_back(std::move(connection));

    if (m_callbacks.onConnection) {
        try {
            m_callbacks.onConnection(*rawConnection);
        } catch (...) {
            rawConnection->handleError(std::current_exception());
        }
    }
}

// Removes entries for closed connections from the list.
void ServerState::cleanupClosedConnections()
{
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        if (!(*it)->isOpen()) {
            it = m_connections.erase(it);
        } else {
            ++it;
        }
    }
}

// Post-loop cleanup: releases listener, closes all connections, updates lifecycle.
void ServerState::cleanupAfterRun() noexcept
{
    {
        std::lock_guard lock(m_mutex);
        m_listenerEvent.reset();
        m_listener.reset();
    }

    for (auto& conn : m_connections)
        conn->close();
    m_connections.clear();

    {
        std::lock_guard lock(m_mutex);
        m_lifecycle = Lifecycle::Stopped;
    }
}

} // namespace miniasyncnetsockets::detail
