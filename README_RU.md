# miniasyncnetsockets

[README in English](README.md)

> **Note:** Код был написан с использованием agentic workflow с [opencode](https://opencode.ai), модель GPT 5.6 Luna, навык `cpp-core-guidelines`, и инструкции из `AGENTS.md`. Ревью кода было выполнено вручную.

Библиотека фреймированного TCP-сервера и клиента для Linux, построенная на базе [miniruntime](https://github.com/rackotmell/miniruntime) (асинхронное выполнение задач) и [mininetsockets](https://github.com/rackotmell/mininetsockets) (TCP-операции).

## Возможности

- Фреймированное TCP-соединение с 4-байтовым префиксом длины (big-endian)
- Асинхронный неблокирующий I/O через event loop на epoll
- Потокобезопасное планирование задач для обработки соединений
- Автоматическое управление жизненным циклом соединений
- Настраиваемые лимиты размера фрейма и очереди записи
- Иерархия ошибок с поддержкой исключений

## Компоненты

| Модуль | Заголовок | Описание |
|--------|-----------|----------|
| **TcpServer** | `tcpserver.hpp` | Фреймированный TCP-сервер: bind, listen, accept, обработка фреймов |
| **TcpClient** | `tcpclient.hpp` | Фреймированный TCP-клиент: connect, отправка/получение фреймов |
| **TcpConnection** | `tcpconnection.hpp` | TCP-соединение: чтение/запись фреймов, закрытие |
| **errors** | `errors.hpp` | Иерархия исключений для ошибок протокола и состояния |

## Технологии

- C++20 (`std::format`, `std::span`)
- CMake 3.20+
- Только Linux (epoll, eventfd, timerfd)
- Зависимости: [miniruntime](https://github.com/rackotmell/miniruntime), [mininetsockets](https://github.com/rackotmell/mininetsockets)

## Структура проекта

```
miniasyncnetsockets/
├── include/miniasyncnetsockets/   # Публичные заголовки
├── src/                           # Реализация
├── tests/                         # Юнит-тесты (GoogleTest)
├── examples/                      # Примеры программ
├── external/                      # Git-субмодули
│   ├── miniruntime/               # Библиотека асинхронного runtime
│   └── mininetsockets/            # Обёртка для TCP-сокетов
└── patches/                       # Патчи для субмодулей
```

## Документация

Документация API предоставлена в виде **Doxygen-комментариев** непосредственно в заголовочных файлах. Каждый публичный класс, метод и важный параметр документированы тегами `@brief`, `@param`, `@return` и `@throws`.

## Сборка

### Требования

- C++20 совместимый компилятор (GCC 10+ или Clang 12+)
- CMake 3.20+
- Linux (epoll/timerfd/eventfd специфичны для Linux)

### Команды сборки

```bash
cmake -B build && cmake --build build
```

### Опции CMake

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `MINIASYNCNETSOCKETS_BUILD_TESTS` | ON | Сборка юнит-тестов |
| `MINIASYNCNETSOCKETS_BUILD_EXAMPLES` | OFF | Сборка примеров программ |
| `ENABLE_ASAN_UBSAN` | OFF | Сборка с AddressSanitizer + UndefinedBehaviorSanitizer |
| `ENABLE_TSAN` | OFF | Сборка с ThreadSanitizer |

### Санитайзеры

ASAN+UBSAN и TSAN взаимоисключающие:

```bash
# ASAN + UBSAN
cmake -B build-asan -DENABLE_ASAN_UBSAN=ON && cmake --build build-asan

# TSAN only
cmake -B build-tsan -DENABLE_TSAN=ON && cmake --build build-tsan
```

## Запуск тестов

```bash
ctest --test-dir build --output-on-failure
```

## Примеры

### Фреймированный Echo-сервер

Слушает на порту 12345, принимает клиентов и эхом возвращает все полученные фреймы.

```bash
./build/examples/framed-echoserver
```

## Патчи субмодулей

Проект включает патчи для субмодулей. Для их применения:

```bash
./patches/apply.sh
```

## Лицензия

Это учебный проект. Используйте как сочтёте нужным.