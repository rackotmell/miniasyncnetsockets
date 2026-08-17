/// @file clientstate.cpp
/// @brief Implementation of the client event-loop thread and connect lifecycle.

#include "clientstate.hpp"
#include "miniasyncnetsockets/errors.hpp"

#include <array>
#include <stdexcept>
#include <sys/epoll.h>
#include <utility>

namespace miniasyncnetsockets::detail
{

ClientState::ClientState(
    mininetsockets::Endpoint endpoint, ClientCallbacks callbacks, ClientOptions options)
    : m_endpoint(std::move(endpoint)), m_callbacks(std::move(callbacks)),
      m_options(options), m_codec(options.maxFrameSize),
      m_writeQueue(options.maxPendingWriteBytes)
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
    if (!m_open || !m_connected) throw InvalidState("client is not connected");
    if (payload.size() > m_options.maxFrameSize) {
        throw FrameTooLarge("frame payload exceeds maxFrameSize");
    }
    if (!m_event) throw InvalidState("client event is not attached");

    // Enable EPOLLOUT when the queue transitions from empty to non-empty.
    const bool wasEmpty = m_writeQueue.empty();
    m_writeQueue.enqueue(payload);
    if (wasEmpty) m_event->updateEvents(EPOLLIN | EPOLLOUT);
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

// Dispatches events: complete the connect first, then read/write.
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

// Completes the non-blocking connect and transitions to the Running state.
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

// Reads from the socket until blocked or EOF, feeding data into the frame codec.
void ClientState::readAvailable()
{
    std::array<std::byte, readBufferSize> buffer{};
    while (m_open) {
        const auto result = m_stream->readNonBlocking(buffer);
        if (result.bytes > buffer.size())
            throw InvalidState("socket returned too many bytes");

        if (result.bytes > 0) {
            m_codec.consume(std::span<const std::byte>(buffer.data(), result.bytes),
                [this](Frame frame) {
                    if (m_callbacks.onFrame) {
                        m_callbacks.onFrame(*m_owner, std::move(frame));
                    }
                });
        }

        if (result.status == mininetsockets::IoStatus::EndOfStream) {
            m_codec.endOfStream();
            close(nullptr);
            return;
        }
        if (result.status == mininetsockets::IoStatus::Blocked || result.bytes == 0)
            return;
    }
}

// Drains the write queue to the socket and disables EPOLLOUT when empty.
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

void ClientState::handleError(std::exception_ptr error) noexcept { close(error); }

// Safely invokes the onError callback; swallows any exception it throws.
void ClientState::reportError(std::exception_ptr error) noexcept
{
    if (!m_callbacks.onError) return;
    try {
        m_callbacks.onError(*m_owner, error);
    } catch (...) {
    }
}

// Closes resources and invokes onClose if the connection was established.
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

    // Notify onClose at most once, only if we were connected.
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

// Post-loop cleanup: closes resources.
void ClientState::cleanupAfterRun() noexcept { close(nullptr); }

} // namespace miniasyncnetsockets::detail
