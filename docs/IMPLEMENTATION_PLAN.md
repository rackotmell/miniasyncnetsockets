# План реализации miniasyncnetsockets

Цель проекта: объединить неблокирующие TCP-сокеты `mininetsockets` и epoll-based
runtime `miniruntime` в библиотеку framed TCP-сервера, а затем framed TCP-клиента.

Разработка выполняется по вертикальным срезам. После каждого логического этапа
нужно собрать проект, запустить тесты, выполнить self-review и создать commit.

## Этап 0. Базовая фиксация

Результат:

- зафиксирован framed protocol `4-byte big-endian payload length`;
- зафиксированы серверные API и lifecycle;
- зафиксировано владение собственным `EventLoop`;
- определены ограничения первой версии;
- проверена чистая сборка обоих submodule до изменений.

Проверяемые invariants:

- event loop callback выполняется в одном потоке;
- socket fd владеет `TcpStream` или `TcpListener`, но не `EventHandle`;
- event handle снимается до закрытия socket fd;
- socket operations не блокируют event-loop thread;
- framing parser ограничивает выделение памяти через `maxFrameSize`.

## Этап 1. Подготовить miniruntime

Модули:

```text
external/miniruntime/include/miniruntime/event/eventloop.h
external/miniruntime/include/miniruntime/event/handle.h
external/miniruntime/src/event/eventloop.cpp
external/miniruntime/src/event/handle.cpp
external/miniruntime/src/CMakeLists.txt
external/miniruntime/tests/event/eventlooptest.cpp
```

Изменения:

1. Добавить изменение epoll-маски зарегистрированного raw fd.
2. Сделать изменение маски потокобезопасным.
3. Исправить CMake-пути, зависящие от `${CMAKE_SOURCE_DIR}`.
4. Добавить прямой include `<unordered_map>`.
5. Исправить ошибочную проверку результата `read()`.
6. Добавить настоящее пробуждение `epoll_wait()` из `EventLoop::stop()`.
7. Исправить moved-from поведение `TimerHandle::fired()`.
8. Добавить unit tests для изменения masks и shutdown.

Критерии готовности:

- `EPOLLOUT` можно включить после появления данных в output queue;
- `EPOLLOUT` можно отключить после полной отправки;
- `stop()` не ждет таймаут `epoll_wait()`;
- тесты существующего `miniruntime` не регрессируют.

Commit создается внутри repository `external/miniruntime`, затем root-проект
обновляет gitlink submodule.

## Этап 2. Подготовить mininetsockets к embedding

Модуль:

```text
external/mininetsockets/CMakeLists.txt
```

Изменения:

1. Заменить абсолютные от `${CMAKE_SOURCE_DIR}` include-пути на локальные.
2. Переименовать общий target `options` в namespaced/local target.
3. Добавить `cxx_std_20` через target property.
4. Проверить сборку через `add_subdirectory()`.

Публичный socket API на этом этапе не расширяется. Используются существующие
`TcpListener`, `TcpStream`, `PendingTcpStream`, `IoResult` и `fdView()`.

Критерии готовности:

- submodule собирается как standalone project;
- submodule подключается из root project;
- существующие socket tests проходят.

## Этап 3. Создать root project

Добавить структуру:

```text
CMakeLists.txt
include/miniasyncnetsockets/
src/
tests/
examples/
docs/
```

Root CMake должен:

- подключать оба submodule через `add_subdirectory()`;
- отключать standalone tests/examples зависимостей;
- собирать target `miniasyncnetsockets`;
- линковать target с `miniruntime` и `mininetsockets`;
- поддерживать C++20, Debug/Release и sanitizers;
- включать root tests и examples отдельными options;
- подготовить install/export targets.

Публичные заголовки:

```text
include/miniasyncnetsockets/errors.hpp
include/miniasyncnetsockets/tcpconnection.hpp
include/miniasyncnetsockets/tcpserver.hpp
include/miniasyncnetsockets/miniasyncnetsockets.hpp
```

Минимальная проверка:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Этап 4. Реализовать framing codec

Внутренние модули:

```text
src/detail/framecodec.hpp
src/detail/framecodec.cpp
tests/framecodectest.cpp
```

Parser должен поддерживать состояния:

```text
ReadingHeader
ReadingPayload
Closed
```

Требования:

1. Накопление 4-byte заголовка по частям.
2. Декодирование big-endian length.
3. Проверка `maxFrameSize` до allocation payload.
4. Накопление payload по частям.
5. Обработка нескольких frames в одном input buffer.
6. Передача готового `Frame` с ownership в callback.
7. Ошибка при EOF внутри header или payload.
8. Поддержка zero-length frame.

Unit tests:

- полный frame одним buffer;
- header по одному байту;
- payload несколькими fragments;
- несколько frames подряд;
- empty frame;
- frame выше лимита;
- EOF на каждой стадии parser.

## Этап 5. Реализовать output queue

Внутренние модули:

```text
src/detail/writequeue.hpp
src/detail/writequeue.cpp
tests/writequeuetest.cpp
```

Очередь хранит serialized frames, текущий buffer offset и общий размер
ожидающих данных.

Алгоритм:

1. `sendFrame()` добавляет 4-byte header и payload в очередь.
2. При пустой очереди регистрируется только `EPOLLIN`.
3. При появлении данных включается `EPOLLOUT`.
4. `writeNonBlocking()` отправляет доступную часть текущего buffer.
5. После полной отправки buffer очередь переходит к следующему.
6. После опустошения очереди `EPOLLOUT` отключается.
7. При превышении `maxPendingWriteBytes` генерируется `WriteQueueOverflow`.

Блокировка event-loop thread на ожидание места в очереди запрещена.

## Этап 6. Реализовать TcpConnection

Файлы:

```text
include/miniasyncnetsockets/tcpconnection.hpp
src/tcpconnection.cpp
src/detail/connectionstate.hpp
tests/tcpconnectiontest.cpp
```

`TcpConnection` объединяет:

- `mininetsockets::TcpStream`;
- `miniruntime::event::EventHandle`;
- framing parser;
- write queue;
- connection state;
- callbacks сервера.

Обработка `EPOLLIN`:

1. Повторять `readNonBlocking()` до `Blocked`.
2. Передавать bytes в framing parser.
3. Вызывать `onFrame` для каждого готового frame.
4. Обрабатывать `EndOfStream`.
5. Закрывать connection при `SocketError` или `ProtocolError`.

Обработка `EPOLLOUT`:

1. Вызывать write queue flush.
2. Сохранять offset частичной записи.
3. Отключать `EPOLLOUT` после полного flush.
4. Закрывать connection при socket error.

На первом этапе используется level-triggered epoll. `EPOLLET` и `EPOLLONESHOT`
не используются.

## Этап 7. Реализовать TcpServer

Файлы:

```text
include/miniasyncnetsockets/tcpserver.hpp
src/tcpserver.cpp
src/detail/serverstate.hpp
tests/tcpservertest.cpp
examples/framed-echoserver.cpp
```

`TcpServer` должен:

- владеть `EventLoop`;
- владеть event-loop thread;
- создать non-blocking listener;
- зарегистрировать listener fd;
- принять все доступные connections;
- создать `TcpConnection` для каждого fd;
- ограничить число connections при заданной настройке;
- корректно закрыть listener и connections при `stop()`.

Lifecycle первой версии:

```text
Constructed -> Running -> Stopped
```

`start()` разрешен один раз. `stop()` идемпотентен и безопасен из любого потока.
При вызове из внешнего потока `stop()` дожидается завершения event-loop thread.
При вызове из callback event loop он только запрашивает остановку и возвращает
управление, а cleanup завершается после выхода callback-а.

Интеграционные tests:

- один клиент и echo;
- несколько клиентов;
- несколько frames в одном write;
- fragmented frames;
- partial writes;
- protocol error;
- EOF и reset;
- server stop из callback;
- server stop из внешнего потока;
- проверка закрытия всех fd.

## Этап 8. Ошибки, lifecycle и hardening

Общие правила:

- ошибка старта listener/event registration выбрасывается из `start()`;
- ошибка connection передается через `onError`;
- исключение пользовательского callback не покидает event-loop thread;
- protocol error закрывает connection;
- `onClose` вызывается до уничтожения connection;
- event handle снимается до закрытия socket owner.

Добавить tests для:

- исключений из `onConnection`;
- исключений из `onFrame`;
- исключений из `onClose` и `onError`;
- уничтожения сервера при активных connections;
- отказа создания listener;
- превышения `maxConnections`;
- переполнения write queue.

## Этап 9. Реализовать клиент

Клиент разрабатывается только после завершения серверного vertical slice.

Основные операции:

1. Создать `PendingTcpStream`.
2. Зарегистрировать fd на `EPOLLOUT`.
3. Завершить подключение через `finishConnect()`.
4. Запустить connect timeout через timer.
5. Переиспользовать framing codec и write queue.
6. Реализовать тот же lifecycle собственного `EventLoop` и thread.

На первом этапе клиент принимает готовый `Endpoint`. DNS resolution не выполняется
в event-loop thread.

## Этап 10. Worker integration

Этап выполняется после серверного и клиентского socket paths.

Схема:

```text
EventLoop -> DynamicThreadPool -> result queue -> TriggerHandle -> EventLoop
```

Worker thread не изменяет `TcpStream` и connection state. Если connection закрыта
до окончания worker task, результат отбрасывается через weak state reference.

`TaskScheduler` не используется как socket reactor, поскольку его внутренний
`EventLoop` не экспортируется.

## Проверки проекта

Обычная сборка:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

ASAN/UBSAN:

```bash
cmake -S . -B build-asan -DENABLE_ASAN_UBSAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

TSAN:

```bash
cmake -S . -B build-tsan -DENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

## Commit boundaries

1. `miniruntime`: event mask update, wakeup и tests.
2. `mininetsockets`: CMake embedding fixes.
3. Root CMake и public headers.
4. Framing codec.
5. Write queue.
6. `TcpConnection`.
7. `TcpServer` и integration tests.
8. Documentation и examples.
9. `TcpClient`.
10. Worker integration.

Commits для submodule создаются внутри соответствующих repositories. После этого
в superproject фиксируется обновленный gitlink.
