# miniasyncnetsockets

[README на русском](README_RU.md)

> **Note:** Code was developed using agentic workflow with [opencode](https://opencode.ai), model GPT 5.6 Luna, `cpp-core-guidelines` skill, and instructions from `AGENTS.md`. All code has been manually reviewed.

A framed TCP server and client library for Linux, built on top of [miniruntime](https://github.com/rackotmell/miniruntime) (async task execution) and [mininetsockets](https://github.com/rackotmell/mininetsockets) (TCP operations).

## Features

- Framed TCP communication with 4-byte big-endian length prefix
- Asynchronous non-blocking I/O via epoll-based event loop
- Thread-safe task scheduling for connection handling
- Automatic connection lifecycle management
- Configurable frame size and write queue limits
- Exception-safe error hierarchy

## Components

| Module | Header | Description |
|--------|--------|-------------|
| **TcpServer** | `tcpserver.hpp` | Framed TCP server: bind, listen, accept, handle frames |
| **TcpClient** | `tcpclient.hpp` | Framed TCP client: connect, send/receive frames |
| **TcpConnection** | `tcpconnection.hpp` | Single TCP connection: frame read/write, close |
| **errors** | `errors.hpp` | Exception hierarchy for protocol and state errors |

## Technologies

- C++20 (`std::format`, `std::span`)
- CMake 3.20+
- Linux-only (epoll, eventfd, timerfd)
- Dependencies: [miniruntime](https://github.com/rackotmell/miniruntime), [mininetsockets](https://github.com/rackotmell/mininetsockets)

## Project Structure

```
miniasyncnetsockets/
├── include/miniasyncnetsockets/   # Public headers
├── src/                           # Implementation
├── tests/                         # Unit tests (GoogleTest)
├── examples/                      # Example programs
├── external/                      # Git submodules
│   ├── miniruntime/               # Async runtime library
│   └── mininetsockets/            # TCP socket wrapper
└── patches/                       # Submodule patches
```

## Documentation

API documentation is provided as **Doxygen-style comments** directly in the header files. Each public class, method, and important parameter is documented with `@brief`, `@param`, `@return`, and `@throws` tags.

## Build

### Prerequisites

- C++20 compatible compiler (GCC 10+ or Clang 12+)
- CMake 3.20+
- Linux (epoll/timerfd/eventfd are Linux-specific)

### Build Commands

```bash
cmake -B build && cmake --build build
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `MINIASYNCNETSOCKETS_BUILD_TESTS` | ON | Build unit tests |
| `MINIASYNCNETSOCKETS_BUILD_EXAMPLES` | ON | Build examples |
| `ENABLE_ASAN_UBSAN` | OFF | Build with AddressSanitizer + UndefinedBehaviorSanitizer |
| `ENABLE_TSAN` | OFF | Build with ThreadSanitizer |

### Sanitizers

ASAN+UBSAN and TSAN are mutually exclusive:

```bash
# ASAN + UBSAN
cmake -B build-asan -DENABLE_ASAN_UBSAN=ON && cmake --build build-asan

# TSAN only
cmake -B build-tsan -DENABLE_TSAN=ON && cmake --build build-tsan
```

## Running Tests

```bash
ctest --test-dir build --output-on-failure
```

## Examples

### Framed Echo Server

Listens on port 12345, accepts clients, and echoes all received frames back.

```bash
cmake -B build -DMINIASYNCNETSOCKETS_BUILD_EXAMPLES=ON && cmake --build build
./build/examples/framed-echoserver
```

### Framed Terminal Client

Connects to the echo server, reads lines from stdin, sends them as frames, and prints received frames to stdout. Type `quit` or `exit` to disconnect.

```bash
# In another terminal, while the server is running:
./build/examples/framed-terminalclient
```

The client accepts an optional port argument (default: 12345):

```bash
./build/examples/framed-terminalclient 8080
```

### Server and Client Interaction

The echo server and terminal client demonstrate a complete framed TCP exchange:

1. The server listens on a port and accepts incoming connections.
2. The client connects and sends each stdin line as a framed message (4-byte length prefix + payload).
3. The server's `onFrame` callback receives the frame and echoes it back via `sendFrame`.
4. The client's `onFrame` callback prints the echoed frame to stdout.

```
Client                          Server
  |                               |
  |--- TCP connect -------------->|
  |<-- accept --------------------|
  |                               |
  |--- frame("hello") ----------->|
  |<-- frame("hello") ------------|  (echo)
  |                               |
  |--- frame("world") ----------->|
  |<-- frame("world") ------------|  (echo)
  |                               |
  |--- disconnect --------------->|
```

Callbacks provide the `TcpConnection` reference, allowing the handler to inspect endpoints, close the connection, or send additional frames.

## Submodule Patches

This project includes patches for the submodules. To apply them:

```bash
./patches/apply.sh
```

## License

This is an educational project. Use as you see fit.