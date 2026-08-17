/// @file tcpservertest.cpp
/// @brief Integration tests for TcpServer.

#include "miniasyncnetsockets/miniasyncnetsockets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <stdexcept>
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

// Serializes a payload with a 4-byte big-endian header.
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

// Reads exactly one framed message from a TcpStream (blocking).
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

// Checks whether an exception_ptr holds a specific exception type.
template<typename ErrorType>
bool isError(std::exception_ptr error)
{
    try {
        std::rethrow_exception(error);
    } catch (const ErrorType&) {
        return true;
    } catch (...) {
        return false;
    }
}

// Creates a minimal echo server callback set.
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

    // Send two frames in a single write.
    auto first = serializedFrame("first");
    auto second = serializedFrame("second");
    std::vector<std::byte> combined = first;
    combined.insert(combined.end(), second.begin(), second.end());
    client.writeAll(combined);

    EXPECT_EQ(readFrame(client), bytes("first"));
    EXPECT_EQ(readFrame(client), bytes("second"));

    // Send a frame in two TCP segments.
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
    // maxFrameSize = 2, but client sends a frame claiming 3 bytes.
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

    // Stopping the server externally should trigger onClose on active connections.
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
        // Calling stop() from within a callback must not deadlock (no self-join).
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

TEST(TcpServerTest, ReportsOnConnectionCallbackException)
{
    std::promise<std::exception_ptr> errorPromise;
    auto errorFuture = errorPromise.get_future();
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> errorReported{false};
    std::atomic<bool> closed{false};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onConnection = [](miniasyncnetsockets::TcpConnection&) {
        throw std::runtime_error("onConnection failure");
    };
    callbacks.onError = [&errorPromise, &errorReported](miniasyncnetsockets::TcpConnection&,
                                                         std::exception_ptr error) {
        if (!errorReported.exchange(true)) errorPromise.set_value(error);
    };
    callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
        if (!closed.exchange(true)) closePromise.set_value();
    };

    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks));
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());

    ASSERT_EQ(errorFuture.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(isError<miniasyncnetsockets::ProtocolError>(errorFuture.get()));
    server.stop();
}

TEST(TcpServerTest, ReportsOnFrameCallbackException)
{
    std::promise<std::exception_ptr> errorPromise;
    auto errorFuture = errorPromise.get_future();
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> errorReported{false};
    std::atomic<bool> closed{false};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection&, Frame) {
        throw std::runtime_error("onFrame failure");
    };
    callbacks.onError = [&errorPromise, &errorReported](miniasyncnetsockets::TcpConnection&,
                                                         std::exception_ptr error) {
        if (!errorReported.exchange(true)) errorPromise.set_value(error);
    };
    callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
        if (!closed.exchange(true)) closePromise.set_value();
    };

    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks));
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());
    auto outgoing = serializedFrame("failure");
    client.writeAll(outgoing);

    ASSERT_EQ(errorFuture.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(isError<miniasyncnetsockets::ProtocolError>(errorFuture.get()));
    server.stop();
}

TEST(TcpServerTest, ReportsOnCloseExceptionEvenWhenOnErrorThrows)
{
    std::promise<void> errorPromise;
    auto errorFuture = errorPromise.get_future();
    std::atomic<bool> errorReported{false};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection& connection, Frame) {
        connection.close();
    };
    callbacks.onClose = [](const miniasyncnetsockets::TcpConnection&) {
        throw std::runtime_error("onClose failure");
    };
    // onError itself throws -- the onClose exception should still be reported.
    callbacks.onError = [&errorPromise, &errorReported](miniasyncnetsockets::TcpConnection&,
                                                         std::exception_ptr) {
        if (!errorReported.exchange(true)) errorPromise.set_value();
        throw std::runtime_error("onError failure");
    };

    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks));
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());
    auto outgoing = serializedFrame("close");
    client.writeAll(outgoing);

    ASSERT_EQ(errorFuture.wait_for(1s), std::future_status::ready);
    server.stop();
}

TEST(TcpServerTest, RejectsConnectionsAboveLimit)
{
    std::promise<void> firstConnectionPromise;
    auto firstConnectionFuture = firstConnectionPromise.get_future();
    std::atomic<int> connectionCount{0};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onConnection = [&firstConnectionPromise, &connectionCount](
                                 miniasyncnetsockets::TcpConnection&) {
        if (connectionCount.fetch_add(1) == 0) firstConnectionPromise.set_value();
    };
    miniasyncnetsockets::ServerOptions options;
    options.maxConnections = 1;
    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks), options);
    server.start();
    auto firstClient = TcpStream::connect(server.localEndpoint());
    ASSERT_EQ(firstConnectionFuture.wait_for(1s), std::future_status::ready);
    auto secondClient = TcpStream::connect(server.localEndpoint());
    std::this_thread::sleep_for(20ms);

    // Only the first connection should have been accepted.
    EXPECT_EQ(connectionCount.load(), 1);
    server.stop();
}

TEST(TcpServerTest, WriteQueueOverflowIsReported)
{
    std::promise<std::exception_ptr> errorPromise;
    auto errorFuture = errorPromise.get_future();
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> errorReported{false};
    std::atomic<bool> closed{false};

    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection& connection, Frame frame) {
        connection.sendFrame(frame);
    };
    callbacks.onError = [&errorPromise, &errorReported](miniasyncnetsockets::TcpConnection&,
                                                         std::exception_ptr error) {
        if (!errorReported.exchange(true)) errorPromise.set_value(error);
    };
    callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
        if (!closed.exchange(true)) closePromise.set_value();
    };
    // Tiny write queue: 4 bytes is less than a serialized frame.
    miniasyncnetsockets::ServerOptions options;
    options.maxPendingWriteBytes = 4;
    TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks), options);
    server.start();
    auto client = TcpStream::connect(server.localEndpoint());
    auto outgoing = serializedFrame("x");
    client.writeAll(outgoing);

    ASSERT_EQ(errorFuture.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(isError<miniasyncnetsockets::WriteQueueOverflow>(errorFuture.get()));
    server.stop();
}

TEST(TcpServerTest, DestructorClosesActiveConnections)
{
    std::promise<void> closePromise;
    auto closeFuture = closePromise.get_future();
    std::atomic<bool> closed{false};

    {
        miniasyncnetsockets::ServerCallbacks callbacks;
        callbacks.onClose = [&closePromise, &closed](const miniasyncnetsockets::TcpConnection&) {
            if (!closed.exchange(true)) closePromise.set_value();
        };
        TcpServer server(mininetsockets::Endpoint::ipv4Any(0), std::move(callbacks));
        server.start();
        auto client = TcpStream::connect(server.localEndpoint());
        std::this_thread::sleep_for(10ms);
    }
    // Server destructor should close active connections.

    ASSERT_EQ(closeFuture.wait_for(1s), std::future_status::ready);
}
