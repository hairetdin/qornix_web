# Qornix Async Roadmap

## Цель

Перевести Qornix на C++20 coroutine-based async architecture без поломки текущих sync-хендлеров.

Финальная модель:

```cpp
app.get("/health", sync_handler);

app.get_async("/users/{id}",
    [](Request req, Url url, Params params) -> net::awaitable<Response> {
        auto user = co_await db.fetch_user(params["id"]);
        co_return Response::json(user);
    }
);
```

## Async gate status note

`doc/project_doc/roadmap_async_gate.md` is the current merge/release gate for the async branch. The HTTP roadmap below is historical sprint planning; async DB work, documentation synchronization, dependency updates and template readiness are tracked by the gate and by `doc/project_doc/roadmap_async_gate_changelog.md`.

Implemented async DB work is documented in `doc/async_db.md` and `doc/project_doc/roadmap_async_db_changelog.md`. Do not read older Sprint 6-12 future wording as a statement that timeout/backpressure/DB async work was not done later in the branch.

## Базовые принципы

1. Сначала перейти на C++20.
2. Оставить старые sync-хендлеры.
3. Добавить async API рядом: `get_async`, `post_async`, etc.
4. Не блокировать `io_context` threads.
5. Любые БД/API/Redis/file операции должны быть async или вынесены в отдельный worker pool.
6. Для 10k concurrent обязательны timeout, cancellation и backpressure.
7. Каждый этап должен быть отдельным PR с тестами.
8. После выполнения спринта, фиксировать изменения в doc/project_doc/roadmap_async_changelog.md

## Формат

- Длина спринта: 2 недели.
- Каждый спринт должен завершаться собираемым проектом.
- Старый API должен продолжать работать до явного решения о breaking changes.

---

# Sprint 0 — Baseline и аудит

## Цель

Зафиксировать текущее состояние Qornix перед async-миграцией.

## Задачи

- Проверить сборку:
  - core;
  - examples;
  - qornix_auth;
  - qornix_orm;
  - qornix_dynamic_api;
  - qornix_rag, если он входит в обязательную сборку.
- Добавить smoke tests:
  - `/health`;
  - обычный route;
  - route с path params;
  - 404;
  - keep-alive.
- Добавить benchmark-сценарии:
  - быстрый endpoint;
  - endpoint с delay 10 ms;
  - endpoint с delay 100 ms;
  - 1k concurrent;
  - 10k idle keep-alive, если позволяют лимиты ОС.
- Зафиксировать:
  - RPS;
  - p50/p95/p99 latency;
  - memory RSS;
  - number of threads;
  - open file descriptors.

## Definition of Done

- Проект собирается.
- Есть baseline tests.
- Есть baseline benchmark report.
- Дальнейшие изменения можно сравнивать с baseline.

---

# Sprint 1 — Переход на C++20

## Цель

Перевести Qornix на C++20 без изменения runtime-логики.

## Задачи

- Обновить root `CMakeLists.txt`:
  - `cmake_minimum_required(VERSION 3.20)`;
  - `CMAKE_CXX_STANDARD 20`;
  - `CMAKE_CXX_STANDARD_REQUIRED ON`;
  - `CMAKE_CXX_EXTENSIONS OFF`.
- Заменить:
  - `cxx_std_17` -> `cxx_std_20`.
- Обновить CMake в модулях:
  - `qornix_orm`;
  - `qornix_auth`;
  - `qornix_dynamic_api`;
  - `qornix_rag`.
- Обновить шаблоны:
  - `templates/app/CMakeLists.txt.in`;
  - `templates/dynamic_api_app/CMakeLists.txt.in`.
- Проверить Boost:
  - желательно Boost >= 1.81;
  - лучше Boost >= 1.83.
- Исправить C++20 warnings/compatibility issues.

## Не делаем

- Не добавляем async handlers.
- Не меняем публичный route API.
- Не меняем Session lifecycle.

## Definition of Done

- Sync routes работают как раньше.
- Examples собираются.
- Templates создают C++20-проекты.
- Baseline tests проходят.
- Нет заметной просадки на быстрых endpoint'ах.

---

# Sprint 2 — Response pipeline refactor

## Цель

Подготовить pipeline к delayed response, когда handler вернёт ответ не сразу.

## Задачи

- Ввести базовые aliases:

```cpp
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
using Params = std::map<std::string, std::string>;
```

- Вынести отправку ответа:

```cpp
void Session::send_response(Response res);
```

- Перевести sync route execution на общий flow:

```text
async_read
-> resolve route
-> call sync handler
-> produce Response
-> send_response(Response)
```

- Централизовать стандартные ответы:
  - 200;
  - 400;
  - 404;
  - 500.
- Добавить helpers:
  - `make_text_response`;
  - `make_json_response`;
  - `make_error_response`.

## Не делаем

- Не добавляем `get_async`.
- Не добавляем `co_spawn`.
- Не добавляем timeout/cancellation.

## Definition of Done

- Все ответы проходят через `send_response`.
- Старый sync API работает.
- Keep-alive работает.
- 404/500 обрабатываются централизованно.

---

# Sprint 3 — Session strand

## Цель

Сделать Session безопасной при многопоточном `io_context`.

## Задачи

- Добавить strand на Session:

```cpp
using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
```

- Привязать read/write callbacks к strand.
- Проверить `shared_from_this()` lifecycle.
- Убедиться, что операции одной Session не выполняются параллельно:
  - read;
  - write;
  - route processing;
  - close.
- Добавить тесты на несколько keep-alive запросов по одному connection.

## Definition of Done

- Все callbacks Session проходят через strand.
- Нет data race на `req_`, `res_`, `socket_`, `buffer_`.
- Sync routes работают.
- Keep-alive стабилен.

## Выполнено 2026-05-13

- Добавлен alias `Strand = boost::asio::strand<boost::asio::io_context::executor_type>`.
- `Session` получила собственный `strand_`, инициализированный от executor `io_context` принятого socket.
- `Session::run()` стартует первый `do_read()` через `net::dispatch(strand_, ...)`.
- Completion callbacks `http::async_read` и `http::async_write` привязаны через `net::bind_executor(strand_, ...)`.
- Lifecycle `shared_from_this()` сохранен: `self` удерживается для dispatch, read и write callbacks.
- `http_server_smoke_test` теперь запускает серверный `io_context` в 4 потоках.
- Добавлен keep-alive regression test на 24 последовательных запроса по одному TCP connection.

## Проверка Sprint 3

- `cmake -S . -B build/async_sprint3 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint3 --parallel` - ok.
- `ctest --test-dir build/async_sprint3 --output-on-failure` - `19/19` passed.

---

# Sprint 4 — Первый async route API

## Цель

Добавить `get_async` на C++20 coroutines.

## Предлагаемые типы

```cpp
using AsyncRouteHandler = std::function<boost::asio::awaitable<Response>(
    Request,
    Url,
    Params
)>;

using SyncRouteHandler = std::function<void(
    const Request&,
    Response&,
    const urls::url_view&,
    const Params&
)>;
```

## Задачи

- Добавить route storage:
  - отдельный список async routes;
  - или `std::variant<SyncRouteHandler, AsyncRouteHandler>`.
- Добавить методы:
  - `add_async_route`;
  - `get_async`.
- Запускать async handler через `boost::asio::co_spawn`.
- Передавать `Request`, `Url`, `Params` по значению.
- Ловить exceptions внутри coroutine и превращать их в 500.

## Тестовый endpoint

```cpp
app.get_async("/sleep",
    [](Request req, Url url, Params params) -> net::awaitable<Response> {
        auto executor = co_await net::this_coro::executor;

        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds(100));
        co_await timer.async_wait(net::use_awaitable);

        co_return make_text_response("ok");
    }
);
```

## Definition of Done

- Sync routes работают.
- Async `/sleep` работает.
- Async handler не блокирует другие запросы.
- Exception внутри async handler превращается в 500.
- `Request`, `Url`, `Params` безопасны после `co_await`.

## Выполнено 2026-05-13

- Добавлены публичные типы:
  - `Url = urls::url`;
  - `SyncRouteHandler`;
  - `AsyncRouteHandler = std::function<net::awaitable<Response>(Request, Url, Params)>`.
- Route storage переведен на единый `RouteEntry` с `std::variant<SyncRouteHandler, AsyncRouteHandler>`.
- Порядок регистрации sync/async routes сохранен в одной таблице маршрутов.
- Добавлены методы:
  - `HttpServer::add_async_route(pattern, handler)`;
  - `HttpServer::get_async(pattern, handler)`.
- Добавлен convenience overload `make_text_response(body, version = 11)` для простых `200 OK` text responses.
- `get_async` регистрирует GET-only async route через optional method filter.
- `Session::handle_request()` теперь управляет отправкой ответа:
  - sync route продолжает возвращать response через `send_response`;
  - async route запускается и отправляет response после завершения coroutine.
- Добавлен `Session::start_async_route_handler()`:
  - запускает handler через `net::co_spawn(strand_, ...)`;
  - передает `Request`, owning `Url` и `Params` по значению;
  - ловит exceptions из coroutine и переводит их в `500`.
- Smoke-test расширен:
  - `/async/sleep` проверяет базовый async route;
  - `/async/items/{id}?source=test` проверяет безопасность `Request`, `Url`, `Params` после `co_await`;
  - `/async/boom` проверяет exception -> `500`;
  - отдельный single-thread server проверяет, что pending async timer не блокирует другой connection.

## Проверка Sprint 4

- `cmake -S . -B build/async_sprint4 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint4 --parallel` - ok.
- `ctest --test-dir build/async_sprint4 --output-on-failure` - `19/19` passed.
- `cmake -S route_extensions -B build/async_sprint4_route_extensions -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_sprint4_route_extensions --parallel` - ok.
- `cmake -S . -B build/async_sprint4_rag -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_EXAMPLES=OFF -DQORNIX_BUILD_TESTS=OFF -DQORNIX_BUILD_APP=OFF -DQORNIX_ENABLE_ORM=OFF && cmake --build build/async_sprint4_rag --parallel` - ok. ONNX Runtime не найден, RAG собран с TF-IDF fallback.

## Не делалось в Sprint 4

- Полные async verb helpers (`post_async`, `put_async`, etc.) не добавлялись.
- Timeout, cancellation и backpressure не добавлялись.
- Middleware API остался sync; async-aware middleware continuation/post-processing не менялись.

---

# Sprint 5 — Полный async HTTP API и examples

## Цель

Сделать async API удобным для пользователей framework'а.

## Задачи

- Добавить:
  - `post_async`;
  - `put_async`;
  - `patch_async`;
  - `delete_async`;
  - возможно `any_async`.
- Добавить ergonomic response helpers:
  - `Response::text`;
  - `Response::json`;
  - `Response::status`;
  - `Response::redirect`, если нужно.
- Добавить examples:
  - async sleep;
  - async JSON;
  - mixed sync + async app.
- Обновить README:
  - когда использовать sync;
  - когда использовать async;
  - почему blocking code внутри async handler опасен.
- Добавить документацию по lifetime:
  - не хранить ссылки после `co_await`;
  - async handlers возвращают `Response` value.

## Definition of Done

- Все основные HTTP methods имеют async-вариант.
- Есть runnable example.
- Старый API не сломан.
- README содержит async guide.

## Выполнено 2026-05-13

- Добавлены async route helpers:
  - `post_async`;
  - `put_async`;
  - `patch_async`;
  - `delete_async`;
  - `head_async`;
  - `options_async`;
  - `any_async`.
- `add_async_route` теперь использует `any_async` semantics без method filter.
- Внутри `HttpServer` добавлен общий `add_async_route_for_method(...)` для регистрации async routes с optional `http::verb`.
- Добавлены ergonomic response helpers в namespace `response`:
  - `response::text`;
  - `response::json`;
  - `response::status`;
  - `response::redirect`.
- Существующий `Response` оставлен Beast alias для backward compatibility; helper namespace выбран вместо breaking wrapper-типа `Response::text`.
- Добавлен runnable example `example/async_routes_server`:
  - sync `/health`;
  - async `/sleep/{ms}`;
  - async JSON `/api/status`;
  - async POST `/api/echo`;
  - mixed any-method `/api/method`.
- Root examples build подключает `example/async_routes_server`.
- README обновлен:
  - когда использовать sync routes;
  - когда использовать async routes;
  - почему blocking code внутри async handler опасен;
  - lifetime rules для `Request`, `Url`, `Params` после `co_await`;
  - пример сборки и запуска `async_routes_server`.
- Smoke-test расширен:
  - async POST/PUT/PATCH/DELETE/HEAD/OPTIONS;
  - `any_async`;
  - `response::text/json/status/redirect`;
  - OPTIONS `Allow` header.

## Проверка Sprint 5

- `cmake -S . -B build/async_sprint5 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint5 --parallel` - ok.
- `ctest --test-dir build/async_sprint5 --output-on-failure` - `19/19` passed.
- `cmake -S route_extensions -B build/async_sprint5_route_extensions -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_sprint5_route_extensions --parallel` - ok.
- `cmake -S . -B build/async_sprint5_rag -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_EXAMPLES=OFF -DQORNIX_BUILD_TESTS=OFF -DQORNIX_BUILD_APP=OFF -DQORNIX_ENABLE_ORM=OFF && cmake --build build/async_sprint5_rag --parallel` - ok. ONNX Runtime не найден, RAG собран с TF-IDF fallback.
- Manual example smoke:
  - `build/async_sprint5/example/async_routes_server/async_routes_server 18091 1` - started;
  - `curl -s http://127.0.0.1:18091/sleep/10` -> `slept:10`;
  - `curl -s http://127.0.0.1:18091/api/status` -> `{"status":"ok","mode":"async"}`;
  - `curl -s -X POST http://127.0.0.1:18091/api/echo -d hello` -> `{"echo":"hello"}`.

## Не делалось в Sprint 5

- Timeout, cancellation и backpressure не добавлялись.
- Async middleware API не добавлялся.
- Literal `Response::text` static methods не добавлялись, чтобы не заменять публичный `Response` alias wrapper-типом.

---

# Sprint 6 — Timeout и basic cancellation

## Цель

Не позволять async handler'ам висеть бесконечно.

## Задачи

- Добавить global route timeout.
- Добавить per-route timeout override.
- Добавить timeout response:
  - обычно `504 Gateway Timeout`;
  - для overload можно `503 Service Unavailable`.
- Добавить helper:

```cpp
co_await with_timeout(operation, std::chrono::seconds(3));
```

- Добавить read timeout.
- Добавить write timeout.
- Отслеживать client disconnect.
- При disconnect:
  - не отправлять response;
  - помечать context как cancelled;
  - освобождать ресурсы.
- Ввести `RequestContext`, если он нужен для deadline/cancellation state.

## Definition of Done

- Долгий `/sleep` завершается по timeout.
- Timeout возвращает корректный HTTP response.
- Client disconnect не приводит к crash/use-after-free.
- Timeout покрыт тестами.

---

# Sprint 7 — Backpressure и лимиты

## Цель

Защитить Qornix и downstream-сервисы от перегруза.

## Задачи

- Добавить global concurrent request limit.
- Добавить per-route concurrent limit.
- Добавить async semaphore:

```cpp
class AsyncSemaphore {
public:
    boost::asio::awaitable<Permit> acquire();
};
```

- Добавить max queued waiters.
- При перегрузе возвращать:
  - `429 Too Many Requests`;
  - или `503 Service Unavailable`.
- Добавить лимиты:
  - max active requests;
  - max queued requests;
  - max active DB operations;
  - max request body size.
- Добавить метрики:
  - active requests;
  - queued requests;
  - rejected requests;
  - timed out requests.

## Definition of Done

- При перегрузе сервер не падает.
- Память не растёт бесконтрольно.
- Есть overload tests.
- Есть benchmark overload-сценария.

---

# Sprint 8 — Blocking DB offload pool

## Цель

Дать безопасный промежуточный путь для существующих blocking DB-драйверов.

## Проблема

Плохо:

```cpp
app.get_async("/users/{id}",
    [](Request req, Url url, Params params) -> net::awaitable<Response> {
        auto user = blocking_db.fetch_user(params["id"]); // блокирует io_context thread
        co_return Response::json(user);
    }
);
```

## Решение

Вынести blocking операции в отдельный worker pool:

```cpp
auto user = co_await blocking_pool.submit([id] {
    return db.fetch_user(id);
});
```

## Задачи

- Добавить отдельный thread pool для blocking tasks.
- Ограничить размер pool.
- Ограничить очередь задач.
- Добавить timeout на ожидание результата.
- Добавить metrics:
  - active blocking tasks;
  - queued blocking tasks;
  - rejected blocking tasks.
- Добавить docs:
  - когда использовать offload pool;
  - почему нельзя blocking DB внутри async handler.

## Definition of Done

- Blocking task не блокирует `io_context`.
- При переполнении очереди возвращается 503/429.
- DB mock test показывает работу pool limit.
- Нагрузочный тест показывает стабильную память.

---

# Sprint 9 — Настоящий async DB layer

## Цель

Подготовить Qornix к real async DB workloads.

## Задачи

- Выбрать стратегию:
  - async PostgreSQL client;
  - async MySQL client;
  - Asio-compatible DB library;
  - собственный thin wrapper.
- Спроектировать DB pool:
  - max connections;
  - max waiters;
  - acquisition timeout;
  - query timeout.
- API:

```cpp
auto conn = co_await db_pool.acquire();
auto user = co_await conn.fetch_user(id);
```

- Transaction API:

```cpp
auto tx = co_await db.begin();
auto user = co_await tx.get_user(id);
co_await tx.update_last_seen(id);
co_await tx.commit();
```

- Обработать:
  - rollback при exception;
  - rollback при cancellation;
  - pool exhaustion;
  - query timeout.

## Definition of Done

- Async DB операции не занимают `io_context` thread во время ожидания.
- DB pool не создаёт 10k connections.
- Есть controlled behavior при pool exhaustion.
- Есть example endpoint с async DB.

---

# Sprint 10 — Async middleware

## Цель

Разрешить middleware выполнять async операции.

## API-вариант

```cpp
using AsyncMiddleware = std::function<boost::asio::awaitable<std::optional<Response>>(
    RequestContext&
)>;
```

Middleware может:

```text
return std::nullopt -> продолжить pipeline
return Response     -> остановить pipeline
```

## Задачи

- Ввести `RequestContext`:
  - request;
  - url;
  - params;
  - state;
  - request id;
  - deadline;
  - cancellation flag.
- Поддержать sync middleware и async middleware.
- Определить порядок:
  - global middleware;
  - route middleware;
  - route handler.
- Добавить examples:
  - async auth middleware;
  - async rate limiter;
  - request id middleware.
- Обработать exceptions.

## Definition of Done

- Sync middleware работает как раньше.
- Async middleware может делать `co_await`.
- Middleware может остановить pipeline.
- Timeout распространяется на middleware + handler.

---

# Sprint 11 — Observability и graceful shutdown

## Цель

Сделать async Qornix пригодным для production.

## Задачи

- Добавить request metrics:
  - total;
  - active;
  - completed;
  - failed;
  - timed out;
  - cancelled;
  - rejected.
- Добавить latency histograms:
  - p50;
  - p95;
  - p99.
- Добавить queue metrics:
  - DB queue;
  - blocking pool queue;
  - route semaphore queue.
- Добавить structured logs:
  - request id;
  - route;
  - status;
  - duration;
  - timeout/cancellation reason.
- Добавить graceful shutdown:
  - stop accepting new connections;
  - finish active requests until deadline;
  - cancel remaining requests;
  - close sockets.
- Проверить:
  - slow clients;
  - request body size limit;
  - file descriptor limits.

## Definition of Done

- Есть базовые метрики.
- Есть structured access logs.
- Graceful shutdown работает.
- Overload поведение видно через метрики.

---

# Sprint 12 — Load testing 10k concurrent

## Цель

Проверить целевую нагрузку.

## Сценарии

### 1. Fast endpoint

```text
GET /health
concurrency: 1k, 5k, 10k
```

### 2. Async sleep

```text
GET /sleep?ms=100
concurrency: 1k, 5k, 10k
```

### 3. DB pool limited endpoint

```text
GET /users/{id}
HTTP concurrency: 10k
DB pool: 100 connections
```

### 4. Overload

```text
incoming: 20k concurrent
max active requests: 10k
max queued DB waiters: fixed limit
```

### 5. Client disconnect

```text
client disconnects while DB request is pending
```

### 6. Timeout

```text
DB delay > route timeout
```

## Метрики

- RPS.
- p50/p95/p99 latency.
- RSS memory.
- CPU utilization.
- Number of threads.
- Open file descriptors.
- Active coroutines.
- Active requests.
- Queue length.
- DB pool saturation.
- Timeout rate.
- Rejection rate.

## Definition of Done

- 10k concurrent тесты проходят стабильно.
- Нет uncontrolled memory growth.
- Нет crash при disconnect/timeout.
- Backpressure работает.
- Есть финальный performance report.

---

# Рекомендуемая последовательность PR

```text
PR 01: Baseline tests and benchmarks
PR 02: C++20 migration
PR 03: Response pipeline refactor
PR 04: Session strand
PR 05: get_async with awaitable<Response>
PR 06: Async HTTP methods and examples
PR 07: Timeout and basic cancellation
PR 08: Backpressure and async semaphore
PR 09: Blocking DB offload pool
PR 10: True async DB layer
PR 11: Async middleware
PR 12: Observability and graceful shutdown
PR 13: 10k concurrent load testing
```

---

# Основные риски

## Blocking code внутри async handler

Решение:

- docs warning;
- blocking offload pool;
- позже настоящий async DB layer.

## Lifetime объектов

Решение:

- async handlers принимают `Request`, `Url`, `Params` по значению;
- async handlers возвращают `Response`;
- не использовать `Response&` в async API.

## Отсутствие backpressure

Решение:

- global request limit;
- route limit;
- DB pool limit;
- max queued waiters;
- 429/503 при overload.

## Race conditions в Session

Решение:

- strand на каждую Session;
- read/write/session callbacks через strand.

## Сложная cancellation model

Решение:

- начать с timeout + cancellation flag;
- не отправлять response после disconnect;
- позже добавить cooperative cancellation для DB/API clients.

---

# Production defaults для 10k concurrent

Стартовые значения для тестирования:

```text
io_context threads:
    number of CPU cores

max active HTTP requests:
    5k-20k depending on memory

DB pool:
    50-300 connections depending on DB

max queued DB waiters:
    1k-5k

route timeout:
    1-5 seconds

external API timeout:
    1-3 seconds

request body limit:
    explicit per route

overload response:
    429 or 503

graceful shutdown deadline:
    10-30 seconds
```

Главное правило:

```text
Qornix может держать 10k HTTP-запросов,
но не должен превращать их в 10k одновременных запросов к БД.
```
