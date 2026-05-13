# Qornix Async DB Roadmap

## Назначение документа

Этот roadmap описывает отдельную работу по полной асинхронности database layer в Qornix после завершения общей async HTTP-инфраструктуры.

Важно: текущий `qornix::async::AsyncDbPool` из async HTTP roadmap является migration facade и benchmark/mock layer. Он показывает целевой coroutine-style API, лимиты, timeout'ы и backpressure, но не делает PostgreSQL/MySQL запросы настоящими non-blocking запросами.

Цель этого roadmap — довести DB слой до состояния, где PostgreSQL/MySQL операции не блокируют `io_context` worker threads и могут использоваться напрямую из async HTTP handlers.

---

## Текущее состояние

Сейчас в проекте есть:

- C++20 coroutine-based HTTP route API.
- `RequestContext`, timeout/cancellation/backpressure.
- `BlockingTaskPool` для безопасного offload blocking операций.
- `AsyncDbPool` / `AsyncDbConnection` facade в `include/async_runtime.h`.
- Benchmark endpoint `/users/{id}`, который имитирует DB работу через async delay.
- Существующий `qornix_orm` с sync интерфейсами:
  - `IDatabase`;
  - `DatabaseInterface`;
  - `TableManager`;
  - sync drivers for SQLite/PostgreSQL/MySQL where enabled by build options.

Ограничение текущего состояния:

- `AsyncDbConnection::fetch_user(...)` не выполняет реальный SQL запрос.
- Existing `IDatabase` methods are synchronous.
- Реальные PostgreSQL/MySQL запросы через текущий ORM нельзя вызывать напрямую из async handler без риска блокировки `io_context` thread.
- SQLite должен считаться blocking driver, если не будет отдельного специализированного решения.

---

## Финальная цель

Целевой пользовательский API:

```cpp
app.get_async("/users/{id}",
    [&db](Request req, Url url, Params params) -> net::awaitable<Response> {
        auto conn = co_await db.acquire(ctx);
        auto row = co_await conn.query_one(
            "select id, name, email from users where id = $1",
            {params["id"]},
            ctx
        );
        co_return response::json(row.to_json());
    }
);
```

Целевые свойства:

1. PostgreSQL/MySQL network I/O выполняется через non-blocking socket operations.
2. DB query не занимает HTTP worker thread на время ожидания результата.
3. Connection pool поддерживает async acquire/release.
4. Query timeout, cancellation и request deadline прокидываются в driver layer.
5. Backpressure работает на уровне:
   - pool size;
   - wait queue;
   - per-route limits;
   - per-query timeout;
   - overload response mapping.
6. Existing sync DB API сохраняется для backward compatibility.
7. Blocking DB drivers не маскируются под full async; они запускаются только через `BlockingTaskPool` или явно помечаются как sync/offloaded.

---

## Definition of Done для полной async DB

Работа считается завершенной только когда выполнены все пункты:

- Есть общий awaitable DB API для application code.
- PostgreSQL driver выполняет connect/query/prepare/transaction/fetch без blocking `io_context` threads.
- MySQL driver выполняет connect/query/prepare/transaction/fetch без blocking `io_context` threads.
- Connection pool не выдает больше активных соединений, чем задано в config.
- Очередь ожидания соединений ограничена и возвращает controlled overload error.
- Query timeout приводит к cancel/close конкретной DB операции или соединения.
- Request cancellation propagates to DB operation where supported.
- Ошибки DB мапятся в понятные категории: connection, timeout, cancelled, constraint, syntax, conflict, unavailable, unknown.
- Есть integration tests для PostgreSQL и MySQL.
- Есть benchmark report для normal и overload DB сценариев.
- Dynamic API / ORM integration имеет понятный путь миграции с sync на async.
- Пользовательская документация не обещает async PostgreSQL/MySQL до прохождения integration tests.

---

## Не делаем в рамках этого roadmap

- Не переписываем весь ORM одним большим PR.
- Не удаляем sync `IDatabase` API до отдельного breaking-change решения.
- Не называем SQLite fully async без отдельной реализации non-blocking/actor-based модели.
- Не запускаем PostgreSQL/MySQL запросы в `BlockingTaskPool` как финальное решение для full async.
- Не делаем transparent auto-conversion sync API -> async API, если это скрывает blocking behavior.

---

## Архитектурная модель

### Слои

```text
HTTP async handler
  -> AsyncDatabase / AsyncDbPool
  -> AsyncConnection
  -> AsyncDriver interface
  -> driver implementation:
       PostgreSQL libpq non-blocking
       MySQL Asio-compatible client
       SQLite sync/offloaded fallback
```

### Новый API слой

Предлагаемые новые файлы:

```text
include/db/async_db.h
include/db/async_db_types.h
include/db/async_db_pool.h
include/db/async_db_driver.h
include/db/async_db_result.h
include/db/async_db_errors.h

qornix_orm/database/async_database_interface.h
qornix_orm/database/async_table_manager.h
qornix_orm/database/drivers/postgres_async_driver.h
qornix_orm/database/drivers/mysql_async_driver.h
qornix_orm/database/drivers/sqlite_offloaded_driver.h
```

### Базовые типы

```cpp
namespace qornix::db {

struct QueryParam {
    std::string value;
    bool is_null = false;
};

using QueryParams = std::vector<QueryParam>;

struct Row {
    std::unordered_map<std::string, std::string> columns;
};

struct QueryResult {
    std::vector<Row> rows;
    std::uint64_t affected_rows = 0;
    std::string command_tag;
};

struct QueryOptions {
    std::chrono::milliseconds timeout{2000};
    bool single_row = false;
    std::size_t max_rows = 0;
};

} // namespace qornix::db
```

### Driver interface

```cpp
class IAsyncDatabaseDriver {
public:
    virtual ~IAsyncDatabaseDriver() = default;

    virtual net::awaitable<void> connect(RequestContext* ctx = nullptr) = 0;
    virtual net::awaitable<void> close() = 0;
    virtual bool is_open() const = 0;

    virtual net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        RequestContext* ctx = nullptr
    ) = 0;

    virtual net::awaitable<void> begin(RequestContext* ctx = nullptr) = 0;
    virtual net::awaitable<void> commit(RequestContext* ctx = nullptr) = 0;
    virtual net::awaitable<void> rollback(RequestContext* ctx = nullptr) = 0;
};
```

---

# Phase 0 — Audit and design freeze

## Цель

Зафиксировать текущий sync DB слой и спроектировать совместимый async API без поломки существующего ORM.

## Задачи

- Проанализировать:
  - `qornix_orm/database/IDatabase.h`;
  - `DatabaseInterface`;
  - `TableManager`;
  - existing PostgreSQL/MySQL/SQLite drivers;
  - dynamic API usage of DB layer.
- Разделить методы на категории:
  - connection lifecycle;
  - raw query;
  - parameterized query;
  - schema introspection;
  - mutation;
  - transaction;
  - table manager convenience methods.
- Согласовать новые namespace и file layout.
- Определить, какие sync методы будут иметь async equivalents.
- Определить compatibility policy:
  - sync API remains stable;
  - async API added side-by-side;
  - no hidden blocking in async API.
- Добавить ADR в проектную документацию, если возникнет спорный выбор driver backend.

## Acceptance criteria

- Есть утвержденный public API draft.
- Понятно, какие старые методы остаются sync-only.
- Понятно, какие методы мигрируют в async first.
- Документирован статус SQLite.

---

# Phase 1 — Common async DB contracts

## Цель

Добавить общие типы и interfaces для async DB layer без подключения реальных драйверов.

## Задачи

- Добавить common DB types:
  - `QueryParam`;
  - `QueryParams`;
  - `Row`;
  - `QueryResult`;
  - `QueryOptions`;
  - `DbError` / `DbErrorCode`.
- Добавить `IAsyncDatabaseDriver`.
- Добавить `AsyncDatabase` high-level facade.
- Добавить `AsyncConnectionPool`:
  - max connections;
  - max waiters;
  - acquire timeout;
  - idle timeout;
  - connection max lifetime;
  - health check interval.
- Добавить metrics:
  - active connections;
  - idle connections;
  - queued waiters;
  - rejected acquires;
  - query total/success/error/timeout/cancelled;
  - query latency p50/p95/p99.
- Добавить mock async driver для unit tests.

## Acceptance criteria

- Async DB API компилируется отдельно от конкретных DB libraries.
- Mock driver проходит unit tests.
- Existing HTTP async handlers могут использовать новый facade.
- Старый `AsyncDbPool` из `async_runtime.h` либо остается как compatibility shim, либо начинает делегировать в новый common layer.

---

# Phase 2 — PostgreSQL async driver

## Цель

Реализовать настоящий non-blocking PostgreSQL driver.

## Driver backend

Рекомендуемый backend: `libpq` в non-blocking mode.

Ключевые primitives:

- async connect через `PQconnectStart` / `PQconnectPoll`;
- перевод соединения в non-blocking mode через `PQsetnonblocking`;
- получение native socket через `PQsocket`;
- ожидание readiness через Asio descriptor adapter на Linux server backend;
- query dispatch через `PQsendQueryParams` / `PQsendPrepare` / `PQsendQueryPrepared`;
- result drain через `PQconsumeInput`, `PQisBusy`, `PQgetResult`;
- cancellation через `PQcancel` или close/reconnect strategy, если cancel path нестабилен для конкретного состояния.

## Задачи

- Добавить CMake option:

```cmake
option(QORNIX_ENABLE_ASYNC_POSTGRES "Enable async PostgreSQL driver" ON)
```

- Добавить feature detection для `libpq`.
- Добавить `PostgresAsyncConnection`:
  - async connect;
  - async close;
  - async query;
  - async prepared query;
  - transaction operations.
- Добавить socket wait abstraction:
  - wait readable;
  - wait writable;
  - timeout;
  - cancellation.
- Реализовать state machine:
  - connecting;
  - idle;
  - sending;
  - waiting_result;
  - draining_result;
  - failed;
  - closing.
- Реализовать parameter binding.
- Реализовать result conversion в `QueryResult`.
- Реализовать error mapping.
- Реализовать connection reset on fatal error.
- Добавить integration tests behind env variables:
  - `POSTGRES_TEST_HOST`;
  - `POSTGRES_TEST_PORT`;
  - `POSTGRES_TEST_USER`;
  - `POSTGRES_TEST_PASSWORD`;
  - `POSTGRES_TEST_DB`.

## Acceptance criteria

- 1000 concurrent async PostgreSQL SELECT запросов не блокируют HTTP worker threads.
- При query timeout соединение не возвращается в pool в неопределенном состоянии.
- Если cancel безопасен и подтвержден, соединение остается reusable; иначе оно закрывается и создается заново.
- Integration tests проходят локально при заданных env variables.
- Benchmark содержит PostgreSQL normal/overload сценарии.

---

# Phase 3 — MySQL async driver

## Цель

Реализовать настоящий async MySQL/MariaDB driver.

## Driver backend

Рекомендуемый backend: Asio-compatible MySQL client.

Primary candidate: Boost.MySQL, так как он следует Boost.Asio async model и поддерживает completion tokens / coroutine-style async operations.

Fallback decision:

- Если Boost.MySQL недоступен в минимальной поддерживаемой версии Boost, driver добавляется как optional external dependency.
- Если проект остается на `mysqlclient`, такой driver нельзя считать fully async без отдельной non-blocking state machine.

## Задачи

- Добавить CMake option:

```cmake
option(QORNIX_ENABLE_ASYNC_MYSQL "Enable async MySQL driver" ON)
```

- Добавить feature detection для выбранного MySQL async backend.
- Добавить `MysqlAsyncConnection`:
  - async resolve/connect/handshake;
  - async query;
  - async prepared statements;
  - transaction operations;
  - close/reset.
- Добавить mapping MySQL resultset -> `QueryResult`.
- Добавить mapping ошибок:
  - duplicate key;
  - foreign key violation;
  - deadlock;
  - lock wait timeout;
  - connection lost;
  - syntax error;
  - auth error.
- Добавить integration tests behind env variables:
  - `MYSQL_TEST_HOST`;
  - `MYSQL_TEST_PORT`;
  - `MYSQL_TEST_USER`;
  - `MYSQL_TEST_PASSWORD`;
  - `MYSQL_TEST_DB`.

## Acceptance criteria

- 1000 concurrent async MySQL SELECT запросов не блокируют HTTP worker threads.
- Prepared statements работают через awaitable API.
- Transactions rollback on error/cancellation.
- Integration tests проходят локально при заданных env variables.
- Benchmark содержит MySQL normal/overload сценарии.

---

# Phase 4 — Pool hardening

## Цель

Сделать production-grade async connection pool для PostgreSQL/MySQL.

## Задачи

- Добавить pool configuration:

```yaml
db:
  driver: postgresql
  async: true
  pool:
    min_connections: 2
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
    idle_timeout_ms: 30000
    max_lifetime_ms: 1800000
    health_check_interval_ms: 10000
```

- Реализовать lifecycle:
  - warmup;
  - lazy create;
  - acquire;
  - release;
  - quarantine failed connection;
  - reconnect;
  - graceful close.
- Реализовать fairness:
  - FIFO waiters by default;
  - optional priority later, if needed.
- Реализовать pool shutdown:
  - reject new acquires;
  - wait active queries until deadline;
  - cancel/close remaining connections.
- Добавить metrics and logs:
  - pool name;
  - driver;
  - connection id;
  - query duration;
  - error category;
  - timeout/cancel reason.

## Acceptance criteria

- Pool выдерживает overload без unbounded memory growth.
- Pool shutdown не оставляет leaked sockets.
- Metrics показывают active/idle/waiting/rejected/timeouts.
- DB overload benchmark показывает controlled rejects/timeouts.

---

# Phase 5 — Query timeout and cancellation semantics

## Цель

Согласовать DB deadline с HTTP request lifecycle.

## Задачи

- Прокинуть `RequestContext` или lightweight `CancellationToken` в DB operations.
- Поддержать deadline resolution:

```text
effective_query_timeout = min(
    route timeout remaining,
    QueryOptions.timeout,
    pool default query_timeout
)
```

- Реализовать behavior on timeout:
  - PostgreSQL: try cancel; if unsafe/failed, close connection;
  - MySQL: use backend cancellation/reset semantics if available; otherwise close connection;
  - SQLite/offloaded: cooperative cancel only where possible, otherwise do not return connection to async pool model.
- Исключить late result reuse after request cancellation.
- Добавить explicit errors:
  - `DbErrorCode::Timeout`;
  - `DbErrorCode::Cancelled`;
  - `DbErrorCode::PoolRejected`;
  - `DbErrorCode::ConnectionLost`.

## Acceptance criteria

- Timeout DB query не портит следующего пользователя connection pool.
- HTTP timeout не приводит к late write response.
- Metrics correctly distinguish query timeout vs pool acquire timeout vs HTTP route timeout.

---

# Phase 6 — Transactions and prepared statements

## Цель

Добавить безопасные async transaction и prepared statement primitives.

## Задачи

- Добавить RAII-like async transaction helper:

```cpp
auto tx = co_await conn.begin_transaction(ctx);
co_await conn.query("insert ...", params, {}, ctx);
co_await tx.commit(ctx);
```

- Auto rollback если transaction object destroyed without commit.
- Добавить prepared statement cache:
  - per connection;
  - statement name generation;
  - schema invalidation hooks later;
  - max cache size;
  - LRU eviction.
- Добавить `query_one`, `query_optional`, `execute`, `execute_returning`.
- Добавить SQL placeholder abstraction:
  - PostgreSQL `$1`, `$2`;
  - MySQL `?`.

## Acceptance criteria

- Commit/rollback работают async.
- Cancellation внутри transaction приводит к rollback или connection discard.
- Prepared statements не ломаются после reconnect.
- Tests покрывают success/error/cancel paths.

---

# Phase 7 — ORM async integration

## Цель

Добавить async equivalents для ORM convenience layer.

## Задачи

- Добавить `AsyncDatabaseInterface` рядом с `DatabaseInterface`.
- Добавить `AsyncTableManager` рядом с `TableManager`.
- Добавить async методы:
  - `findById`;
  - `findAll`;
  - `insert`;
  - `update`;
  - `remove`;
  - raw `query`;
  - raw `execute`.
- Сохранить sync `DatabaseInterface` без breaking changes.
- Добавить adapters:
  - async PostgreSQL/MySQL drivers;
  - offloaded sync driver for SQLite and legacy drivers, explicitly named as offloaded.
- Обновить query builder to produce parameterized statements compatible with async drivers.

## Acceptance criteria

- Existing sync ORM tests pass.
- New async ORM tests pass.
- Async ORM does not call sync driver directly on `io_context` thread.
- Documentation clearly marks which drivers are real async and which are offloaded sync.

---

# Phase 8 — Dynamic API async integration

## Цель

Перевести dynamic API request path на async DB where available.

## Задачи

- Найти все dynamic API handlers, которые делают DB операции.
- Добавить async handler path for dynamic API.
- Подключить `AsyncDatabaseInterface`.
- Добавить route-level DB options:
  - query timeout;
  - max result rows;
  - max body size;
  - max concurrent DB operations.
- Добавить error mapping HTTP layer:
  - pool rejected -> `503 Service Unavailable` or `429 Too Many Requests`, depending policy;
  - query timeout -> `504 Gateway Timeout`;
  - constraint violation -> `409 Conflict` or `400 Bad Request`;
  - syntax/config error -> `500 Internal Server Error`.

## Acceptance criteria

- Dynamic API can run with async PostgreSQL/MySQL drivers.
- Load test proves dynamic API does not block worker threads during DB wait.
- Error responses are stable and documented.

---

# Phase 9 — Benchmarks

## Цель

Зафиксировать производительность real async DB layer.

## Задачи

- Расширить `scripts/baseline_benchmark.py` или добавить отдельный `scripts/db_benchmark.py`.
- Добавить сценарии:
  - PostgreSQL normal SELECT;
  - PostgreSQL prepared SELECT;
  - PostgreSQL transaction insert/rollback;
  - PostgreSQL overload pool;
  - MySQL normal SELECT;
  - MySQL prepared SELECT;
  - MySQL transaction insert/rollback;
  - MySQL overload pool;
  - SQLite offloaded baseline.
- Добавить report fields:
  - driver;
  - pool size;
  - waiters;
  - query timeout;
  - 2xx/4xx/5xx;
  - DB success/error/timeout/cancel/reject deltas;
  - p50/p95/p99 query latency;
  - HTTP latency;
  - RSS/CPU/fd/threads.
- Сохранять результат в:

```text
doc/benchmark_async_db.md
```

## Acceptance criteria

- Normal DB scenarios have 0 errors.
- Overload scenarios show controlled rejects/timeouts.
- Thread count remains bounded under DB wait load.
- Results are documented with exact command and environment.

---

# Phase 10 — Documentation and migration guide

## Цель

Подготовить пользовательскую документацию и migration guide.

## Задачи

- Обновить README:
  - how to enable async DB;
  - config examples;
  - sync vs async DB usage;
  - warning about blocking drivers.
- Добавить docs:

```text
doc/async_db.md
doc/benchmark_async_db.md
doc/project_doc/roadmap_async_db_changelog.md
```

- Добавить examples:

```text
example/async_postgres_server
example/async_mysql_server
example/async_orm_server
```

- Добавить migration guide:
  - sync `DatabaseInterface` -> async `AsyncDatabaseInterface`;
  - blocking custom driver -> offloaded driver;
  - raw SQL -> parameterized async query;
  - transaction migration.

## Acceptance criteria

- Пользователь может включить async PostgreSQL/MySQL по документации.
- Есть runnable examples.
- Есть known limitations.
- Project changelog честно фиксирует, какие DB drivers real async.

---

## Suggested implementation order

Рекомендуемый порядок PR:

1. Common async DB contracts + mock driver.
2. Pool hardening on mock driver.
3. PostgreSQL async connect/query MVP.
4. PostgreSQL timeout/cancel/error mapping.
5. PostgreSQL transactions/prepared statements.
6. MySQL async connect/query MVP.
7. MySQL timeout/cancel/error mapping.
8. MySQL transactions/prepared statements.
9. ORM async interface.
10. Dynamic API async DB path.
11. DB benchmarks and docs.

---

## Risk register

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Driver silently blocks despite async API | High | Test with thread count, synthetic slow query, and CPU/latency comparison. |
| Timeout leaves connection in dirty state | High | Discard connection on uncertain cancel result. |
| Pool wait queue grows unbounded | High | Hard max waiters and controlled overload response. |
| Prepared statement cache breaks after reconnect | Medium | Cache is per-connection and cleared on reconnect. |
| PostgreSQL/MySQL error codes mapped inconsistently | Medium | Central `DbErrorCode` and driver-specific mapping tests. |
| SQLite is mistaken for real async | Medium | Name SQLite path `offloaded`, document limitations. |
| Dynamic API mixes sync and async DB paths | Medium | Explicit config flag and metrics label `db_mode`. |
| Cancellation is cooperative and late result arrives | Medium | Late result suppression and connection discard after cancellation. |

---

## Metrics required before declaring success

Minimum benchmark set:

```text
postgres_normal_select_128
postgres_normal_select_1000
postgres_pool_overload_10000
postgres_slow_query_timeout
mysql_normal_select_128
mysql_normal_select_1000
mysql_pool_overload_10000
mysql_slow_query_timeout
sqlite_offloaded_baseline_128
```

Success criteria:

- normal scenarios: `errors = 0`;
- timeout scenarios: exact timeout delta equals number of expected timeout responses;
- overload scenarios: controlled `rejected/timeouts`, no crash, no fd leak;
- thread count bounded and not proportional to DB concurrency;
- RSS stable across repeated benchmark runs;
- DB query wait does not reduce fast `/health` throughput catastrophically.

---

## Notes for maintainers

- Full async DB means async network I/O, not just coroutine wrapper around blocking query.
- A coroutine function can still block if it calls blocking driver code before `co_await` or inside an awaitable body.
- For PostgreSQL, prefer explicit state machine around `libpq` non-blocking calls.
- For MySQL, prefer Asio-native client integration if dependency policy allows it.
- For SQLite and legacy sync drivers, keep the offload path available but document it as sync/offloaded.
- Do not merge a driver as real async until benchmark proves DB wait does not consume HTTP worker threads.

---

## Implementation note — 2026-05-13

The first implementation pass completed the common async DB foundation and created `doc/project_doc/roadmap_async_db_changelog.md`. The common API, pool, mock driver, explicit sync/offloaded driver and ORM facade are now implemented. Real PostgreSQL/MySQL non-blocking drivers are still tracked as open Phase 2/3 work and must not be marked complete until integration tests and `doc/benchmark_async_db.md` results are available.
