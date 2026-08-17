#include "serverstate.hpp"

#include <exception>
#include <utility>
#include <sys/epoll.h>

namespace miniasyncnetsockets::detail
{

ServerState::ServerState(mininetsockets::Endpoint endpoint,
                         ServerCallbacks callbacks,
                         ServerOptions options)
    : m_endpoint(std::move(endpoint)),
      m_callbacks(std::move(callbacks)),
      m_options(options)
{
}

ServerState::~ServerState() noexcept { stop(); }

void ServerState::start()
{
    std::lock_guard lock(m_mutex);
    if (m_lifecycle != Lifecycle::Constructed) {
        throw InvalidState("server can only be started once");
    }

    std::optional<mininetsockets::TcpListener> listener;
    listener.emplace(mininetsockets::TcpListener::bind(
        m_endpoint, m_options.backlog, mininetsockets::ListenerMode::NonBlocking));

    auto listenerEvent = m_loop.createEvent(
        listener->fdView(), EPOLLIN, miniruntime::event::EventType::SOCKET, [this](int) {
            acceptConnections();
        });

    m_listener = std::move(listener);
    m_listenerEvent.emplace(std::move(listenerEvent));
    try {
        m_thread = std::thread([this] { run(); });
    } catch (...) {
        m_listenerEvent.reset();
        m_listener.reset();
        throw;
    }
    m_lifecycle = Lifecycle::Running;
}

void ServerState::stop() noexcept
{
    std::unique_lock lock(m_mutex);
    if (m_lifecycle == Lifecycle::Constructed) return;

    const bool calledFromLoopThread = std::this_thread::get_id() == m_loopThreadId;
    if (m_lifecycle == Lifecycle::Running) m_loop.stop();
    if (calledFromLoopThread || !m_thread.joinable()) return;

    if (m_joinInProgress) {
        m_stateChanged.wait(lock, [this] { return !m_joinInProgress; });
        return;
    }

    m_joinInProgress = true;
    lock.unlock();
    m_thread.join();
    lock.lock();
    m_joinInProgress = false;
    m_stateChanged.notify_all();
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

void ServerState::run()
{
    {
        std::lock_guard lock(m_mutex);
        m_loopThreadId = std::this_thread::get_id();
    }

    try {
        m_loop.run();
    } catch (...) {
        m_loop.stop();
    }
    cleanupAfterRun();
}

void ServerState::acceptConnections()
{
    if (!m_listener) return;
    cleanupClosedConnections();

    while (true) {
        std::optional<mininetsockets::TcpStream> stream;
        try {
            stream = m_listener->accept();
        } catch (...) {
            m_loop.stop();
            return;
        }
        if (!stream) break;

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

void ServerState::addConnection(mininetsockets::TcpStream stream)
{
    const int fd = stream.fdView();
    auto connection = std::unique_ptr<TcpConnection>(new TcpConnection(
        std::move(stream),
        m_options.maxFrameSize,
        m_options.maxPendingWriteBytes,
        m_callbacks.onFrame,
        m_callbacks.onClose,
        m_callbacks.onError));
    TcpConnection* const rawConnection = connection.get();

    auto event = m_loop.createEvent(
        fd, EPOLLIN, miniruntime::event::EventType::SOCKET, [rawConnection](int) {
            rawConnection->onEvent();
        });
    rawConnection->attachEvent(std::move(event));
    m_connections.emplace(rawConnection, std::move(connection));

    if (m_callbacks.onConnection) {
        try {
            m_callbacks.onConnection(*rawConnection);
        } catch (...) {
            rawConnection->handleError(std::current_exception());
        }
    }
}

void ServerState::cleanupClosedConnections()
{
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        if (!it->first->isOpen()) {
            it = m_connections.erase(it);
        } else {
            ++it;
        }
    }
}

void ServerState::cleanupAfterRun() noexcept
{
    {
        std::lock_guard lock(m_mutex);
        m_listenerEvent.reset();
        m_listener.reset();
    }
    for (auto& [connection, state] : m_connections) connection->close();
    m_connections.clear();

    {
        std::lock_guard lock(m_mutex);
        m_loopThreadId = {};
        m_lifecycle = Lifecycle::Stopped;
    }
    m_stateChanged.notify_all();
}

} // namespace miniasyncnetsockets::detail
