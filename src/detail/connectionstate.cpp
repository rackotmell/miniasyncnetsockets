/// @file connectionstate.cpp
/// @brief Implementation of the per-connection event handler.

#include "connectionstate.hpp"

#include <array>
#include <stdexcept>
#include <sys/epoll.h>
#include <utility>

namespace miniasyncnetsockets::detail
{

ConnectionState::ConnectionState(mininetsockets::TcpStream stream,
    std::size_t maxFrameSize, std::size_t maxPendingWriteBytes, FrameHandler onFrame,
    CloseHandler onClose, ErrorHandler onError)
    : m_stream(std::move(stream)), m_codec(maxFrameSize),
      m_writeQueue(maxPendingWriteBytes), m_maxFrameSize(maxFrameSize),
      m_onFrame(std::move(onFrame)), m_onClose(std::move(onClose)),
      m_onError(std::move(onError))
{
}

void ConnectionState::attachEvent(miniruntime::event::EventHandle event)
{
    if (m_event) throw InvalidState("connection event is already attached");
    m_event.emplace(std::move(event));
}

// Main event-loop callback: read available data, then flush pending writes.
void ConnectionState::onEvent(TcpConnection& owner)
{
    if (!m_open) return;

    try {
        readAvailable(owner);
        if (m_open) flushWrites();
    } catch (...) {
        handleError(owner, std::current_exception());
    }
}

void ConnectionState::handleError(TcpConnection& owner, std::exception_ptr error) noexcept
{
    reportError(owner, error);
    close(owner);
}

void ConnectionState::sendFrame(std::span<const std::byte> payload)
{
    if (!m_open) throw InvalidState("connection is closed");
    if (!m_event) throw InvalidState("connection event is not attached");
    if (payload.size() > m_maxFrameSize)
        throw FrameTooLarge("frame payload exceeds maxFrameSize");

    // Enable EPOLLOUT when the queue transitions from empty to non-empty.
    const bool wasEmpty = m_writeQueue.empty();
    m_writeQueue.enqueue(payload);
    if (wasEmpty) m_event->updateEvents(EPOLLIN | EPOLLOUT);
}

void ConnectionState::close(TcpConnection& owner) noexcept
{
    if (!m_open) return;

    m_open = false;
    m_codec.close();
    m_event.reset();

    // Invoke onClose; report any exception from onClose via onError.
    if (m_onClose) {
        try {
            m_onClose(owner);
        } catch (...) {
            reportError(owner, std::current_exception());
        }
    }
}

bool ConnectionState::isOpen() const noexcept { return m_open; }

mininetsockets::Endpoint ConnectionState::localEndpoint() const
{
    return m_stream.localEndpoint();
}

mininetsockets::Endpoint ConnectionState::remoteEndpoint() const
{
    return m_stream.remoteEndpoint();
}

// Reads from the socket until blocked or EOF, feeding data into the frame codec.
void ConnectionState::readAvailable(TcpConnection& owner)
{
    std::array<std::byte, readBufferSize> buffer{};
    while (m_open) {
        const auto result = m_stream.readNonBlocking(buffer);
        if (result.bytes > buffer.size())
            throw InvalidState("socket returned too many bytes");

        if (result.bytes > 0) {
            m_codec.consume(std::span<const std::byte>(buffer.data(), result.bytes),
                [this, &owner](Frame frame) {
                    if (m_onFrame) m_onFrame(owner, std::move(frame));
                });
            if (!m_open) return;
        }

        if (result.status == mininetsockets::IoStatus::EndOfStream) {
            m_codec.endOfStream();
            close(owner);
            return;
        }
        if (result.status == mininetsockets::IoStatus::Blocked || result.bytes == 0)
            return;
    }
}

// Drains the write queue to the socket and disables EPOLLOUT when empty.
void ConnectionState::flushWrites()
{
    if (m_writeQueue.empty()) return;

    const auto result = m_writeQueue.writeNonBlocking(
        [this](std::span<std::byte> data) { return m_stream.writeNonBlocking(data); });
    if (result.status == mininetsockets::IoStatus::EndOfStream) {
        throw std::runtime_error("socket reached end of stream while writing");
    }

    if (m_writeQueue.empty() && m_event) m_event->updateEvents(EPOLLIN);
}

// Safely invokes the onError callback; swallows any exception it throws.
void ConnectionState::reportError(TcpConnection& owner, std::exception_ptr error) noexcept
{
    if (!m_onError) return;
    try {
        m_onError(owner, error);
    } catch (...) {
    }
}

} // namespace miniasyncnetsockets::detail
