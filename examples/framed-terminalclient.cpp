/// @file framed-terminalclient.cpp
/// @brief Minimal framed terminal client for the echo server example.
///
/// Usage: framed-terminalclient [port]
///   port - server port (default: 12345)
///
/// Reads lines from stdin and sends them as frames. Received frames are printed
/// to stdout. Type "quit" or "exit" to disconnect.

#include "miniruntime/logger/logger.h"
#include <miniasyncnetsockets/miniasyncnetsockets.hpp>

#include <atomic>
#include <iostream>
#include <span>
#include <string>
#include <thread>

int main(int argc, char* argv[])
{
    miniruntime::logger::Logger::getInstance().setMinLevel(
        miniruntime::logger::LogLevel::Error);

    const std::uint16_t port =
        (argc > 1) ? static_cast<std::uint16_t>(std::stoi(argv[1])) : 12345;

    std::atomic<bool> connected{false};

    miniasyncnetsockets::ClientCallbacks callbacks;
    callbacks.onConnected = [&connected](miniasyncnetsockets::TcpConnection&) {
        connected.store(true);
        std::cout << "connected\n";
    };
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection&,
                            miniasyncnetsockets::Frame frame) {
        std::cout << "> "
                  << std::string(reinterpret_cast<char*>(frame.data()), frame.size())
                  << '\n';
    };
    callbacks.onClose = [](const miniasyncnetsockets::TcpConnection&) {
        std::cout << "disconnected\n";
    };
    callbacks.onError = [](miniasyncnetsockets::TcpConnection&, std::exception_ptr) {
        std::cout << "error\n";
    };

    miniasyncnetsockets::TcpClient client(
        mininetsockets::Endpoint::ipv4("127.0.0.1", port), std::move(callbacks));
    client.start();

    while (!connected.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit" || line == "exit") break;
        auto sendSpan = std::as_writable_bytes(std::span(line.data(), line.size()));
        client.sendFrame(sendSpan);
    }

    client.stop();
}
