#include "miniasyncnetsockets/miniasyncnetsockets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{

using miniasyncnetsockets::Frame;
using miniasyncnetsockets::TcpServer;
using mininetsockets::TcpStream;

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

std::vector<std::byte> serializedFrame(std::string_view payload)
{
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::byte> result{
        static_cast<std::byte>((size >> 24U) & 0xffU),
        static_cast<std::byte>((size >> 16U) & 0xffU),
        static_cast<std::byte>((size >> 8U) & 0xffU),
        static_cast<std::byte>(size & 0xffU),
    };
    const auto payloadBytes = bytes(payload);
    result.insert(result.end(), payloadBytes.begin(), payloadBytes.end());
    return result;
}

Frame readFrame(TcpStream& stream)
{
    std::array<std::byte, 4> header{};
    stream.readExact(header);
    const auto toInteger = [](std::byte value) {
        return static_cast<std::uint32_t>(std::to_integer<unsigned char>(value));
    };
    const auto size = static_cast<std::size_t>((toInteger(header[0]) << 24U) |
                                                (toInteger(header[1]) << 16U) |
                                                (toInteger(header[2]) << 8U) |
                                                toInteger(header[3]));
    Frame payload(size);
    stream.readExact(payload);
    return payload;
}

miniasyncnetsockets::ServerCallbacks makeEchoCallbacks()
{
    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection& connection, Frame frame) {
        connection.sendFrame(frame);
    };
    return callbacks;
}

} // namespace

TEST(TcpServerTest, EchoesOneFrame)
{
    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), makeEchoCallbacks());
    server.start();
    ASSERT_TRUE(server.isRunning());

    auto client = TcpStream::connect(server.localEndpoint());
    auto outgoing = serializedFrame("hello");
    client.writeAll(outgoing);

    EXPECT_EQ(readFrame(client), bytes("hello"));

    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST(TcpServerTest, EchoesSeveralFramesAndFragmentedInput)
{
    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), makeEchoCallbacks());
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());

    auto first = serializedFrame("first");
    auto second = serializedFrame("second");
    std::vector<std::byte> combined = first;
    combined.insert(combined.end(), second.begin(), second.end());
    client.writeAll(combined);

    EXPECT_EQ(readFrame(client), bytes("first"));
    EXPECT_EQ(readFrame(client), bytes("second"));

    auto fragmented = serializedFrame("fragmented");
    client.writeAll(std::span<std::byte>(fragmented.data(), 2));
    std::this_thread::sleep_for(2ms);
    client.writeAll(std::span<std::byte>(fragmented.data() + 2, fragmented.size() - 2));
    EXPECT_EQ(readFrame(client), bytes("fragmented"));

    server.stop();
}

TEST(TcpServerTest, ReportsProtocolErrorAndClosesConnection)
{
    std::promise<std::exception_ptr> errorPromise;
    auto errorFuture = errorPromise.get_future();
    std::atomic<bool> reported{false};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onError = [&errorPromise, &reported](miniasyncnetsockets::TcpConnection&,
                                                     std::exception_ptr error) {
        if (!reported.exchange(true)) errorPromise.set_value(error);
    };
    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks),
                     miniasyncnetsockets::ServerOptions{.maxFrameSize = 2});
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());

    std::array<std::byte, 4> oversizedHeader{
        static_cast<std::byte>(0),
        static_cast<std::byte>(0),
        static_cast<std::byte>(0),
        static_cast<std::byte>(3),
    };
    client.writeAll(oversizedHeader);

    ASSERT_EQ(errorFuture.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(errorFuture.get());
    server.stop();
}

TEST(TcpServerTest, ExternalStopClosesActiveConnection)
{
    std::promise<void> connectionPromise;
    auto connectionFuture = connectionPromise.get_future();
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> closed{false};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onConnection = [&connectionPromise](miniasyncnetsockets::TcpConnection&) {
        connectionPromise.set_value();
    };
    callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
        if (!closed.exchange(true)) closePromise.set_value();
    };
    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks));
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());
    ASSERT_EQ(connectionFuture.wait_for(1s), std::future_status::ready);

    server.stop();

    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(server.isRunning());
}

TEST(TcpServerTest, StopFromFrameCallbackDoesNotJoinCurrentThread)
{
    std::promise<void> callbackPromise;
    auto callbackFuture = callbackPromise.get_future();
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> callbackRan{false};
    std::atomic<bool> closed{false};
    TcpServer* serverPointer{nullptr};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [&serverPointer, &callbackPromise, &callbackRan](
                            miniasyncnetsockets::TcpConnection&, Frame) {
        serverPointer->stop();
        if (!callbackRan.exchange(true)) callbackPromise.set_value();
    };
    callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
        if (!closed.exchange(true)) closePromise.set_value();
    };

    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks));
    serverPointer = &server;
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());
    auto outgoing = serializedFrame("stop");
    client.writeAll(outgoing);

    ASSERT_EQ(callbackFuture.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
    server.stop();
    EXPECT_FALSE(server.isRunning());
}
