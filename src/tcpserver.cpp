/// @file tcpserver.cpp
/// @brief TcpServer pimpl delegation.

#include "miniasyncnetsockets/tcpserver.hpp"

#include "detail/serverstate.hpp"

#include <utility>

namespace miniasyncnetsockets
{

// Delegates to ServerState constructor.
TcpServer::TcpServer(
    mininetsockets::Endpoint endpoint, ServerCallbacks callbacks, ServerOptions options)
    : m_state(std::make_unique<detail::ServerState>(
          std::move(endpoint), std::move(callbacks), options))
{
}

// Ensures a clean shutdown on destruction.
TcpServer::~TcpServer() noexcept
{
    if (m_state) m_state->stop();
}

void TcpServer::start()
{
    if (!m_state) throw InvalidState("server state is not available");
    m_state->start();
}

void TcpServer::stop() noexcept
{
    if (m_state) m_state->stop();
}

bool TcpServer::isRunning() const noexcept { return m_state && m_state->isRunning(); }

mininetsockets::Endpoint TcpServer::localEndpoint() const
{
    if (!m_state) throw InvalidState("server state is not available");
    return m_state->localEndpoint();
}

} // namespace miniasyncnetsockets
