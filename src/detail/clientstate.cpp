#include "clientstate.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <sys/epoll.h>

namespace miniasyncnetsockets::detail
{

ClientState::ClientState(mininetsockets::Endpoint endpoint,
                         ClientCallbacks callbacks,
                         ClientOptions options)
    : m_endpoint(std::move(endpoint)),
      m_callbacks(std::move(callbacks)),
      m_options(options),
      m_codec(options.maxFrameSize),
      m_writeQueue(options.maxPendingWriteBytes)
{
}

ClientState::~ClientState() noexcept { stop(); }

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
        m_pending.emplace(mininetsockets::PendingTcpStream::connect(m_endpoint));
        auto event = m_loop.createEvent(
            m_pending->fdView(), EPOLLOUT, miniruntime::event::EventType::SOCKET, [this](int) {
                onEvent();
            });
        m_event.emplace(std::move(event));
        m_connectTimer.emplace(m_loop.createTimer(m_options.connectTimeout, [this] {
            onConnectTimeout();
        }));
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
    std::unique_lock lock(m_mutex);
    if (m_lifecycle == Lifecycle::Constructed) return;

    const bool calledFromLoopThread = std::this_thread::get_id() == m_loopThreadId;
    m_loop.stop();
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

void ClientState::sendFrame(std::span<const std::byte> payload)
{
    if (!m_open || !m_connected) throw InvalidState("client is not connected");
    if (payload.size() > m_options.maxFrameSize) {
        throw FrameTooLarge("frame payload exceeds maxFrameSize");
    }
    if (!m_event) throw InvalidState("client event is not attached");

    const bool wasEmpty = m_writeQueue.empty();
    m_writeQueue.enqueue(payload);
    if (wasEmpty) m_event->updateEvents(EPOLLIN | EPOLLOUT);
}

void ClientState::run() noexcept
{
    {
        std::lock_guard lock(m_mutex);
        m_loopThreadId = std::this_thread::get_id();
    }

    try {
        m_loop.run();
    } catch (...) {
        handleError(std::current_exception());
    }
    cleanupAfterRun();
}

void ClientState::onEvent()
{
    if (!m_open) return;

    try {
        if (!m_connected) {
            finishConnect();
        }
        if (m_open && m_connected) {
            readAvailable();
            if (m_open) flushWrites();
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

void ClientState::finishConnect()
{
    if (!m_pending) throw InvalidState("pending client stream is not available");

    auto stream = m_pending->finishConnect();
    m_stream.emplace(std::move(stream));
    m_pending.reset();
    if (m_connectTimer) {
        m_connectTimer->cancel();
        m_connectTimer.reset();
    }
    m_connected = true;
    {
        std::lock_guard lock(m_mutex);
        m_lifecycle = Lifecycle::Running;
    }

    if (m_callbacks.onConnected) m_callbacks.onConnected(*m_owner);
}

void ClientState::readAvailable()
{
    std::array<std::byte, 64U * 1024U> buffer{};
    while (m_open) {
        const auto result = m_stream->readNonBlocking(buffer);
        if (result.bytes > buffer.size()) throw InvalidState("socket returned too many bytes");

        if (result.bytes > 0) {
            m_codec.consume(std::span<const std::byte>(buffer.data(), result.bytes),
                            [this](Frame frame) {
                                if (m_callbacks.onFrame) {
                                    m_callbacks.onFrame(*m_owner, std::move(frame));
                                }
                            });
            if (!m_open) return;
        }

        if (result.status == mininetsockets::IoStatus::EndOfStream) {
            m_codec.endOfStream();
            close(nullptr);
            return;
        }
        if (result.status == mininetsockets::IoStatus::Blocked || result.bytes == 0) return;
    }
}

void ClientState::flushWrites()
{
    if (m_writeQueue.empty()) return;

    const auto result = m_writeQueue.writeNonBlocking(
        [this](std::span<std::byte> data) { return m_stream->writeNonBlocking(data); });
    if (result.status == mininetsockets::IoStatus::EndOfStream) {
        throw std::runtime_error("socket reached end of stream while writing");
    }
    if (m_writeQueue.empty() && m_event) m_event->updateEvents(EPOLLIN);
}

void ClientState::handleError(std::exception_ptr error) noexcept
{
    close(error);
}

void ClientState::reportError(std::exception_ptr error) noexcept
{
    if (!m_callbacks.onError) return;
    try {
        m_callbacks.onError(*m_owner, error);
    } catch (...) {
    }
}

void ClientState::close(std::exception_ptr error) noexcept
{
    if (!m_open) return;
    m_open = false;
    if (error) reportError(error);

    if (m_connectTimer) {
        m_connectTimer->cancel();
        m_connectTimer.reset();
    }
    m_event.reset();
    m_stream.reset();
    m_pending.reset();

    if (m_connected && !m_closeNotified) {
        m_closeNotified = true;
        if (m_callbacks.onClose) {
            try {
                m_callbacks.onClose(*m_owner);
            } catch (...) {
                reportError(std::current_exception());
            }
        }
    }
    m_loop.stop();
}

void ClientState::cleanupAfterRun() noexcept
{
    close(nullptr);
    {
        std::lock_guard lock(m_mutex);
        m_loopThreadId = {};
        m_lifecycle = Lifecycle::Stopped;
    }
    m_stateChanged.notify_all();
}

} // namespace miniasyncnetsockets::detail
