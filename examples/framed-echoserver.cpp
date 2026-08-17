/// @file framed-echoserver.cpp
/// @brief Minimal framed echo server example using TcpServer.

#include <miniasyncnetsockets/miniasyncnetsockets.hpp>

#include <iostream>

int main()
{
    // Echo callback: sends back every received frame.
    miniasyncnetsockets::ServerCallbacks callbacks;
    callbacks.onFrame = [](miniasyncnetsockets::TcpConnection& connection,
                           miniasyncnetsockets::Frame frame) {
        connection.sendFrame(frame);
    };

    miniasyncnetsockets::TcpServer server(
        mininetsockets::Endpoint::ipv4Any(12345), std::move(callbacks));
    server.start();
    std::cout << "framed echo server listening on " << server.localEndpoint().toString() << '\n';
    std::cout << "press Enter to stop\n";

    std::string line;
    std::getline(std::cin, line);
    server.stop();
}
