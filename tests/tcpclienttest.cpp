/// @file tcpclienttest.cpp
/// @brief Integration tests for TcpClient.

#include "miniasyncnetsockets/miniasyncnetsockets.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{

// Converts a string literal to a byte vector.
std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

// Creates a minimal echo server callback set.
miniasyncnetsockets::ServerCallbacks echoCallbacks()
{
    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection& connection,
                           miniasyncnetsockets::Frame frame) {
        connection.sendFrame(frame);
    };
    return callbacks;
}

} // namespace

TEST(TcpClientTest, ConnectsSendsAndReceivesFrame)
{
    miniasyncnetsockets::TcpServer server(
        mininetsockets::Endpoint::ipv4Any(0), echoCallbacks());
    server.start();

    std::promise<void> connectedPromise;
    auto connectedFuture = connectedPromise.get_future();
    std::promise<void> framePromise;
    auto frameFuture = framePromise.get_future();
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> connected{false};
    std::atomic<bool> frameReceived{false};
    std::atomic<bool> closed{false};
    std::vector<std::byte> received;

    miniasyncnetsockets::ClientCallbacks callbacks;
    callbacks.onConnected = [&connectedPromise, &connected](miniasyncnetsockets::TcpConnection& connection) {
        if (!connected.exchange(true)) connectedPromise.set_value();
        auto payload = bytes("client hello");
        connection.sendFrame(payload);
    };
    callbacks.onFrame = [&framePromise, &frameReceived, &received](
                            miniasyncnetsockets::TcpConnection& connection,
                            miniasyncnetsockets::Frame frame) {
        received = std::move(frame);
        if (!frameReceived.exchange(true)) framePromise.set_value();
        connection.close();
    };
    callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
        if (!closed.exchange(true)) closePromise.set_value();
    };

    miniasyncnetsockets::TcpClient client(
        server.localEndpoint(), std::move(callbacks));
    client.start();

    ASSERT_EQ(connectedFuture.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(frameFuture.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(received, bytes("client hello"));

    client.stop();
    server.stop();
}

TEST(TcpClientTest, RejectsNonPositiveConnectTimeout)
{
    miniasyncnetsockets::ClientOptions options;
    options.connectTimeout = 0ms;
    miniasyncnetsockets::TcpClient client(
        mininetsockets::Endpoint::ipv4("127.0.0.1", 1), {}, options);

    EXPECT_THROW(client.start(), miniasyncnetsockets::InvalidState);
    client.stop();
}
