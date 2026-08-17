# Архитектурные решения

Статус: решения зафиксированы для первой реализации `miniasyncnetsockets`.

## D1. Порядок разработки

Сначала реализуется сервер, затем клиент.

Причина: серверный path проверяет полный основной цикл интеграции:

```text
listener -> accept -> read -> frame parser -> write queue -> close
```

Клиент добавляется после стабилизации этих компонентов и переиспользует framing,
write queue и connection state.

## D2. Владение EventLoop

`TcpServer` и будущий `TcpClient` владеют собственными `EventLoop` и event-loop
thread.

Пользователь не передает `EventLoop` в публичный API и не должен самостоятельно
управлять потоком event loop.

Причины:

- простой lifecycle для прикладного кода;
- единая точка запуска и остановки;
- невозможно случайно уничтожить `EventLoop` раньше его handles;
- socket callbacks всегда выполняются в известном внутреннем потоке.

Ограничение: первая версия не поддерживает объединение нескольких серверов или
клиентов в один внешний event loop.

## D3. Границы модулей

### mininetsockets

Отвечает за:

- создание и закрытие TCP fd;
- bind/listen/accept/connect;
- blocking и non-blocking socket operations;
- endpoint representation;
- socket exceptions;
- RAII-владение fd.

Не отвечает за:

- epoll registration;
- event-loop lifecycle;
- framing;
- output queue;
- connection callbacks.

### miniruntime

Отвечает за:

- epoll event loop;
- raw fd registrations;
- eventfd triggers;
- timerfd timers;
- thread pool primitives.

Для интеграции требуется добавить изменение epoll-маски raw fd и настоящее
пробуждение `EventLoop::stop()`.

### miniasyncnetsockets

Отвечает за:

- ownership и lifecycle server/client;
- связь socket fd с `EventHandle`;
- framed protocol;
- framing parser;
- output queue и backpressure;
- connection state machine;
- пользовательские callbacks.

## D4. Формат framing

Фиксированный формат первой версии:

```text
[uint32 payload length, network byte order][payload]
```

Решения:

- длина хранится в 4 байтах;
- порядок байт big endian;
- длина относится только к payload;
- zero-length payload разрешен;
- default `maxFrameSize` равен `1 MiB`;
- payload больше лимита является `FrameTooLarge`;
- EOF внутри frame является `ProtocolError`.

В заголовок не добавляются version, message type или checksum. Такие поля будут
добавлены только при появлении конкретного протокольного требования.

## D5. Модель чтения

Используется level-triggered epoll.

Callback читает через `readNonBlocking()` до результата `Blocked`. Это позволяет
обработать все доступные байты без обязательного перехода к `EPOLLET` и снижает
сложность state machine.

`EPOLLET` и `EPOLLONESHOT` в первой версии не используются.

## D6. Модель записи

`sendFrame()` принимает payload, но не serialized frame. Библиотека сама добавляет
framing header и копирует данные в per-connection output queue.

`EPOLLOUT` регистрируется только пока очередь непуста. Для этого `miniruntime`
должен предоставить изменение event mask существующей регистрации.

При превышении `maxPendingWriteBytes` соединение получает `WriteQueueOverflow` и
закрывается после вызова `onError`.

Event-loop thread никогда не блокируется в ожидании свободного места в очереди.

## D7. Ownership и порядок уничтожения

`TcpStream` или `TcpListener` владеет socket fd. `EventHandle` владеет только
epoll registration и не закрывает raw fd.

Для каждого connection гарантируется порядок:

```text
remove event registration -> destroy EventHandle -> destroy TcpStream
```

Это исключает регистрацию закрытого fd и риск fd reuse с устаревшей epoll
регистрацией.

`TcpServer` владеет listener, connections, event handles и event-loop thread.

## D8. Concurrency model

В первой версии:

- `onConnection`, `onFrame`, `onClose` и `onError` выполняются в event-loop thread;
- `sendFrame()` и `TcpConnection::close()` вызываются из callbacks;
- `TcpServer::stop()` можно вызвать из любого потока;
- `TcpServer::start()` вызывается один раз до начала работы;
- direct cross-thread access к `TcpConnection` не поддерживается.

`stop()` только запрашивает остановку event loop и не дожидается завершения
потока. Деструктор дожидается завершения через `join()`. Пользователь должен
избегать уничтожения `TcpServer` из callback event loop.

Межпоточная отправка будет реализована позднее через очередь команд и
`TriggerHandle`, а не через прямой вызов socket methods из worker thread.

## D9. Обработка ошибок

Ошибки разделяются по месту возникновения:

| Ситуация | Поведение |
|---|---|
| Ошибка создания listener | исключение из `start()` |
| Ошибка epoll registration | исключение из `start()` или закрытие connection |
| Ошибка socket read/write | `onError`, затем закрытие connection |
| EOF после полного frame | `onClose` |
| EOF внутри frame | `ProtocolError`, `onError`, затем `onClose` |
| Frame выше лимита | `FrameTooLarge`, `onError`, затем `onClose` |
| Переполнение write queue | `WriteQueueOverflow`, `onError`, затем `onClose` |
| Исключение пользовательского callback | `onError`, затем закрытие connection |

Исключения не должны выйти из event-loop callback и завершить event-loop thread.

## D10. TaskScheduler и worker pool

`TaskScheduler` не используется как основной socket event loop, потому что его
внутренний `EventLoop` не доступен для raw socket registration.

На этапе worker integration используется:

```text
EventLoop + DynamicThreadPool + result queue + TriggerHandle
```

Worker tasks не владеют и не изменяют `TcpStream`. Они возвращают результаты в
event-loop thread, где проверяется актуальность connection state.

## D11. CMake и submodule changes

Оба submodule должны поддерживать два режима:

1. standalone build;
2. подключение через root `add_subdirectory()`.

Для этого исправляются CMake-пути от `${CMAKE_SOURCE_DIR}`, generic target names и
публичное объявление C++20.

Изменения в submodule применяются через patch-файлы, хранящиеся в superproject.
Superproject фиксирует patch-файлы и собственные root files.

## D12. Ограничения первой версии

Не реализуются:

- UDP;
- TLS;
- HTTP;
- автоматическое framing negotiation;
- произвольные message types;
- async DNS в event-loop thread;
- shared external EventLoop;
- cross-thread `sendFrame()`;
- повторный `start()` после `stop()`;
- корутины и continuation API.

Эти ограничения сохраняют небольшой и проверяемый первый vertical slice.
