#include "miniasyncnetsockets/tcpconnection.hpp"

#include "detail/connectionstate.hpp"

#include <utility>

namespace miniasyncnetsockets
{

TcpConnection::TcpConnection(mininetsockets::TcpStream stream,
                             std::size_t maxFrameSize,
                             std::size_t maxPendingWriteBytes,
                             FrameHandler onFrame,
                             CloseHandler onClose,
                             ErrorHandler onError)
    : m_state(std::make_unique<detail::ConnectionState>(std::move(stream),
                                                        maxFrameSize,
                                                        maxPendingWriteBytes,
                                                        std::move(onFrame),
                                                        std::move(onClose),
                                                        std::move(onError)))
{
}

TcpConnection::~TcpConnection() noexcept { close(); }

void TcpConnection::attachEvent(miniruntime::event::EventHandle event)
{
    m_state->attachEvent(std::move(event));
}

void TcpConnection::onEvent() { m_state->onEvent(*this); }

void TcpConnection::handleError(std::exception_ptr error) noexcept
{
    if (m_state) m_state->handleError(*this, error);
}

void TcpConnection::sendFrame(std::span<const std::byte> payload)
{
    if (!m_state) throw InvalidState("connection state is not available");
    m_state->sendFrame(payload);
}

void TcpConnection::close() noexcept
{
    if (m_state) m_state->close(*this);
}

bool TcpConnection::isOpen() const noexcept { return m_state && m_state->isOpen(); }

mininetsockets::Endpoint TcpConnection::localEndpoint() const
{
    if (!m_state) throw InvalidState("connection state is not available");
    return m_state->localEndpoint();
}

mininetsockets::Endpoint TcpConnection::remoteEndpoint() const
{
    if (!m_state) throw InvalidState("connection state is not available");
    return m_state->remoteEndpoint();
}

} // namespace miniasyncnetsockets
