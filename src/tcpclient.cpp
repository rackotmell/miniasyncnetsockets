#include "miniasyncnetsockets/tcpclient.hpp"

#include "detail/clientstate.hpp"

#include <utility>

namespace miniasyncnetsockets
{

TcpClient::TcpClient(mininetsockets::Endpoint endpoint,
                     ClientCallbacks callbacks,
                     ClientOptions options)
    : m_state(std::make_unique<detail::ClientState>(std::move(endpoint),
                                                    std::move(callbacks),
                                                    options))
{
}

TcpClient::~TcpClient() noexcept
{
    if (m_state) m_state->stop();
}

void TcpClient::start()
{
    if (!m_state) throw InvalidState("client state is not available");
    m_state->start(*this);
}

void TcpClient::stop() noexcept
{
    if (m_state) m_state->stop();
}

void TcpClient::sendFrame(std::span<const std::byte> payload)
{
    if (!m_state) throw InvalidState("client state is not available");
    m_state->sendFrame(payload);
}

} // namespace miniasyncnetsockets
