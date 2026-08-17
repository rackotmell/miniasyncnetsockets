# miniasyncnetsockets API

Статус документа: публичный API серверной версии `v0.1` и клиентской версии `v0.2`.

Документ фиксирует контракт корневой библиотеки. Серверный API реализован в `v0.1`,
клиентский API реализован в `v0.2`.

## Область API

`miniasyncnetsockets` предоставляет асинхронную работу с framed TCP-соединениями.
Сервер владеет своим `EventLoop` и потоком event loop. Пользователь работает с
готовыми сообщениями, а не с фрагментами TCP-потока.

В первой версии не поддерживаются UDP, TLS, HTTP, автоматический DNS в event-loop
потоке и произвольное изменение протокола framing.

## Публичные заголовки

Структура публичных заголовков:

```text
include/miniasyncnetsockets/errors.hpp
include/miniasyncnetsockets/tcpconnection.hpp
include/miniasyncnetsockets/tcpserver.hpp
include/miniasyncnetsockets/tcpclient.hpp
include/miniasyncnetsockets/miniasyncnetsockets.hpp
```

Основной include:

```cpp
#include <miniasyncnetsockets/miniasyncnetsockets.hpp>
```

## Framing

Каждое сообщение передается в формате:

```text
[4 bytes payload length, big endian][payload]
```

Правила протокола:

- длина содержит только размер payload;
- максимальный размер payload задается в `ServerOptions`;
- значение `0` для payload разрешено;
- несколько TCP reads могут составить один frame;
- один TCP read может содержать несколько frames;
- frame с длиной больше `maxFrameSize` является ошибкой протокола;
- EOF внутри заголовка или payload является ошибкой незавершенного frame;
- ошибка протокола закрывает соединение после вызова `onError`.

## Основные типы

Целевой интерфейс находится в namespace `miniasyncnetsockets`.

```cpp
namespace miniasyncnetsockets
{

using Frame = std::vector<std::byte>;

class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class InvalidState : public Error
{
public:
    using Error::Error;
};

class ProtocolError : public Error
{
public:
    using Error::Error;
};

class FrameTooLarge : public ProtocolError
{
public:
    using ProtocolError::ProtocolError;
};

class WriteQueueOverflow : public Error
{
public:
    using Error::Error;
};

struct ServerOptions
{
    int backlog{SOMAXCONN};
    std::size_t maxFrameSize{1024U * 1024U};
    std::size_t maxPendingWriteBytes{4U * 1024U * 1024U};
    std::size_t maxConnections{0};
};

class TcpConnection;

using FrameHandler = std::function<void(TcpConnection&, Frame)>;
using ConnectionHandler = std::function<void(TcpConnection&)>;
using CloseHandler = std::function<void(const TcpConnection&)>;
using ErrorHandler = std::function<void(TcpConnection&, std::exception_ptr)>;

struct ServerCallbacks
{
    FrameHandler onFrame;
    ConnectionHandler onConnection;
    CloseHandler onClose;
    ErrorHandler onError;
};

class TcpConnection
{
public:
    ~TcpConnection() noexcept;

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&&) = delete;
    TcpConnection& operator=(TcpConnection&&) = delete;

    void sendFrame(std::span<const std::byte> payload);
    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;
    [[nodiscard]] mininetsockets::Endpoint remoteEndpoint() const;

private:
    TcpConnection();

    friend class TcpServer;
};

class TcpServer
{
public:
    TcpServer(mininetsockets::Endpoint endpoint,
              ServerCallbacks callbacks,
              ServerOptions options = {});
    ~TcpServer() noexcept;

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    void start();
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] mininetsockets::Endpoint localEndpoint() const;
};

} // namespace miniasyncnetsockets
```

Приведенный код описывает форму API. Реальный `TcpConnection` будет создаваться
только сервером; пользователь получает ссылку на него через callback.

## TcpServer

### Создание

`TcpServer` принимает endpoint, callbacks и настройки. Конструктор не запускает
socket и event-loop thread.

`start()`:

- создает non-blocking `mininetsockets::TcpListener`;
- регистрирует listener fd в собственном `EventLoop`;
- запускает event-loop thread;
- переводит сервер в состояние running.

Ошибки создания listener или регистрации fd выбрасываются из `start()`.

### Остановка

`stop()` потокобезопасен и не выбрасывает исключения. Он:

- прекращает прием новых connections;
- останавливает event loop.

`stop()` не дожидается завершения event-loop thread. Деструктор дожидается
завершения через `join()`. Пользователь должен избегать уничтожения `TcpServer`
из callback event loop, поскольку это приведет к `join()` текущего потока.

Повторный `start()` после `stop()` в `v0.1` не поддерживается. Деструктор
автоматически вызывает `stop()` и дожидается завершения event-loop thread.

## TcpConnection

### Отправка

`sendFrame()` принимает payload без framing-заголовка. Метод:

1. проверяет размер payload;
2. добавляет 4-byte big-endian length;
3. копирует serialized frame во внутреннюю write queue;
4. включает наблюдение за `EPOLLOUT`.

Метод может выбросить:

- `InvalidState`, если connection уже закрыта;
- `FrameTooLarge`, если payload больше `maxFrameSize`;
- `WriteQueueOverflow`, если превышен `maxPendingWriteBytes`.

Запись выполняется частями через `writeNonBlocking()`. После отправки всей
очереди `EPOLLOUT` отключается.

### Callback-и

`onConnection` вызывается после создания и регистрации connection.

`onFrame` получает готовый payload с ownership через `Frame`. Callback может
изменить connection и вызвать `sendFrame()`. Вызов выполняется в event-loop
thread.

`onClose` вызывается перед окончательным уничтожением connection. Он вызывается
как при удаленном закрытии, так и при остановке сервера.

`onError` получает connection и `std::exception_ptr`. Ошибки запуска сервера не
передаются в callback и выбрасываются из `start()`.

Исключение из пользовательского callback не должно покинуть event-loop thread.
Оно передается в `onError`, после чего connection закрывается.

### Потокобезопасность

В `v0.1` следующие методы connection вызываются только из callback event loop:

- `sendFrame()`;
- `close()`;
- `localEndpoint()`;
- `remoteEndpoint()`.

`TcpServer::stop()` можно вызвать из любого потока. Безопасная отправка из
произвольного потока будет добавлена позднее через внутреннюю очередь команд и
trigger.

## Client API, версия v0.2

Клиент использует тот же framed protocol, codec и write queue, что и сервер:

```cpp
struct ClientOptions
{
    std::size_t maxFrameSize{1024U * 1024U};
    std::size_t maxPendingWriteBytes{4U * 1024U * 1024U};
    std::chrono::milliseconds connectTimeout{10'000};
};

class TcpClient;

using ClientFrameHandler = std::function<void(TcpClient&, Frame)>;
using ClientErrorHandler = std::function<void(TcpClient&, std::exception_ptr)>;

struct ClientCallbacks
{
    std::function<void(TcpClient&)> onConnected;
    ClientFrameHandler onFrame;
    std::function<void(TcpClient&)> onClose;
    ClientErrorHandler onError;
};

class TcpClient
{
public:
    TcpClient(mininetsockets::Endpoint endpoint,
              ClientCallbacks callbacks,
              ClientOptions options = {});

    void start();
    void stop() noexcept;
    void sendFrame(std::span<const std::byte> payload);
};
```

`start()` создает `PendingTcpStream`, регистрирует fd на `EPOLLOUT`, завершает
подключение через `finishConnect()` и запускает connect timeout. После успешного
подключения вызывается `onConnected` в event-loop thread. Ошибка подключения или
ошибка callback передается через `onError`, после чего клиент закрывается.

Клиент владеет собственным `EventLoop` и event-loop thread. `sendFrame()` и
callback-и клиента выполняются в event-loop thread; `stop()` можно вызвать из
любого потока. Безопасная cross-thread отправка в первой реализации не поддерживается.
