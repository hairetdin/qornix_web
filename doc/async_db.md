# Async DB layer

Qornix now has a common coroutine-based DB layer for code that must not block HTTP `io_context` worker threads while waiting for database work.

## Status

Implemented in this iteration:

- common awaitable DB types in `include/db/*`;
- `IAsyncDatabaseDriver` contract;
- bounded `AsyncConnectionPool` with `max_connections`, `max_waiters`, acquire timeout, FIFO waiters, idle/max-lifetime checks and graceful shutdown;
- Phase 5 query timeout/deadline resolution and cancellation token primitives;
- `AsyncDatabase`, `AsyncDbConnection` and `AsyncTransaction` facades;
- mock async driver for unit tests and examples;
- explicit sync/offloaded adapter for legacy blocking drivers;
- ORM-facing `AsyncDatabaseInterface` and `AsyncTableManager`;
- PostgreSQL async driver built on libpq non-blocking socket polling when `QORNIX_ENABLE_ASYNC_POSTGRES=ON`;
- MySQL async driver built on Boost.MySQL/Boost.Asio TCP when `QORNIX_ENABLE_ASYNC_MYSQL=ON`.

PostgreSQL is implemented as a real non-blocking driver path when enabled and has passed the integration smoke test with a live DSN. MySQL now has an Asio-compatible driver path and smoke test, but it still needs live MySQL/MariaDB validation and benchmark results before being marked fully validated. No PostgreSQL/MySQL path is silently wrapped in worker threads and advertised as real async.

## Basic usage

```cpp
boost::asio::io_context ioc;

qornix::db::AsyncPoolOptions options;
options.pool_name = "api-primary";
options.max_connections = 32;
options.max_waiters = 1024;
options.acquire_timeout = std::chrono::milliseconds{200};
options.query_timeout = std::chrono::milliseconds{2000};
options.idle_timeout = std::chrono::milliseconds{30000};
options.max_lifetime = std::chrono::milliseconds{1800000};
options.shutdown_timeout = std::chrono::milliseconds{5000};

auto db = std::make_shared<qornix::db::AsyncDatabase>(
    ioc.get_executor(),
    qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}),
    options
);

boost::asio::co_spawn(ioc, [db]() -> boost::asio::awaitable<void> {
    auto row = co_await db->query_one(
        "SELECT id, name FROM users WHERE id = $1",
        {qornix::db::QueryParam::text("42")}
    );
}, boost::asio::detached);
```

## ORM facade usage

```cpp
auto db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options);

auto user = co_await db->table("users").findById("42");

auto tx = co_await db->beginTransaction();
co_await tx.execute(
    "INSERT INTO audit_log(message) VALUES ($1)",
    {qornix::db::QueryParam::text("created user")}
);
co_await tx.commit();
```

## Timeout and cancellation

Use `qornix::db::CancellationToken` to propagate request-level cancellation and deadline information into DB operations.

```cpp
qornix::db::CancellationToken token;
token.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

auto result = co_await db->query(
    "SELECT * FROM users WHERE id = $1",
    {qornix::db::QueryParam::text("42")},
    {},
    token
);
```

The effective timeout is resolved before the query starts as:

```text
effective_query_timeout = min(
    QueryOptions.timeout,
    AsyncPoolOptions.query_timeout,
    CancellationToken.deadline - now
)
```

The selected timeout source is stored in `QueryOptions::timeout_source` for metrics classification. `DbErrorCode::Timeout` now covers query-option timeout, pool-default query timeout and request-deadline timeout; `AsyncDbMetricsSnapshot` exposes separate counters for `query_option_timeouts`, `query_pool_default_timeouts` and `request_deadline_timeouts`. Pool acquisition timeouts remain separate as `DbErrorCode::PoolTimeout` plus `acquire_timeouts`, so overload and slow-query failures are not conflated. Explicit cancellation still maps to `DbErrorCode::Cancelled` and increments `query_cancelled`.

If a DB operation times out or is cancelled after it has been handed to a driver, `AsyncDbConnection` marks the checked-out connection non-reusable before returning it to the pool. PostgreSQL requests cancellation and marks the libpq connection unusable; MySQL closes the connection on timeout/cancel/error paths where the backend operation reports back; sync/offloaded operations are cooperative and timed-out checked-out connections are discarded rather than returned to the async pool.

## Pool hardening and configuration

`AsyncConnectionPool` now keeps metadata for each pooled connection: connection id, creation time, last-used time and driver name. The pool enforces:

- bounded active connections and bounded waiters;
- FIFO waiter fairness, so new acquires do not jump ahead of queued requests;
- lazy idle connection validation on acquire;
- `idle_timeout` and `max_lifetime` checks before returning a connection to the pool;
- close/shutdown mode that rejects new acquires, closes idle connections and waits for active connections up to `shutdown_timeout`;
- metrics for created, closed, discarded and failed connection attempts, timeout/cancel source counters, plus query latency p50/p95/p99 over the in-process sample window.

YAML/INI configuration can use the Phase 4 pool layout:

```yaml
db:
  driver: postgresql
  async: true
  pool:
    name: api-primary
    min_connections: 2
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
    idle_timeout_ms: 30000
    max_lifetime_ms: 1800000
    health_check_interval_ms: 10000
    shutdown_timeout_ms: 5000
    validate_idle_on_acquire: true
```

Use `AsyncDatabaseInterface::poolOptionsFromConfig()` to materialize these settings into `qornix::db::AsyncPoolOptions`:

```cpp
Config::getInstance().loadFromFile("config.yaml");
auto pool_options = AsyncDatabaseInterface::poolOptionsFromConfig();
```

`health_check_interval_ms` is parsed and stored in `AsyncPoolOptions`; proactive background health checks are intentionally not started yet, so health validation remains lazy on acquire/release in this pass. Active queries are not force-cancelled by `close()` because the pool does not own an out-of-band cancellation handle for already checked-out connections; released active connections are discarded while the pool is closing.


## PostgreSQL async driver

`QORNIX_ENABLE_ASYNC_POSTGRES=ON` builds the real PostgreSQL async driver based on libpq non-blocking socket I/O.

Build example:

```bash
cmake -S . -B cmake-build-debug \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_POSTGRES=ON
cmake --build cmake-build-debug --target qornix_orm
```

Application usage:

```cpp
qornix::db::PostgresAsyncOptions pg;
pg.connection_info = "postgresql://qornix:qornix@127.0.0.1:5432/qornix";
pg.connect_timeout = std::chrono::milliseconds{5000};
pg.default_query_timeout = std::chrono::milliseconds{2000};

auto db = std::make_shared<qornix::db::AsyncDatabase>(
    ioc.get_executor(),
    qornix::db::make_postgres_async_driver_factory(pg),
    options
);

auto result = co_await db->query(
    "SELECT $1::text AS value",
    {qornix::db::QueryParam::text("qornix")}
);
```

Prepared statements and transactions use the same awaitable connection API:

```cpp
auto conn = co_await db->acquire();
co_await conn.prepare("select_user", "SELECT id, name FROM users WHERE id = $1");

qornix::db::QueryOptions prepared;
prepared.prepared = true;
prepared.statement_name = "select_user";
auto user = co_await conn.query("", {qornix::db::QueryParam::text("42")}, prepared);

auto tx = co_await db->begin_transaction();
co_await tx.execute("INSERT INTO audit_log(message) VALUES ($1)", {qornix::db::QueryParam::text("created user")});
co_await tx.commit();
```

Integration smoke test:

```bash
export QORNIX_ASYNC_POSTGRES_URL='postgresql://user:password@127.0.0.1:5432/dbname'
ctest --test-dir cmake-build-debug -R qornix_orm_async_postgres_smoke_test --output-on-failure
```

For IDE runners such as CLion that do not inherit shell `export` values, pass the DSN at configure time so CTest receives it through test properties:

```bash
cmake -S . -B cmake-build-debug \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_POSTGRES=ON \
  -DQORNIX_ASYNC_POSTGRES_TEST_URL='postgresql://user:password@127.0.0.1:5432/dbname'
```

If `QORNIX_ASYNC_POSTGRES_URL` is not set, the smoke test exits successfully with a skip message so local builds do not require a PostgreSQL server by default.

## MySQL async driver

`QORNIX_ENABLE_ASYNC_MYSQL=ON` builds the MySQL async driver based on Boost.MySQL and Boost.Asio TCP I/O.

Build example:

```bash
cmake -S . -B cmake-build-debug \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_MYSQL=ON
cmake --build cmake-build-debug --target qornix_orm
```

Application usage:

```cpp
qornix::db::MySqlAsyncOptions mysql;
mysql.host = "127.0.0.1";
mysql.port = "3306";
mysql.username = "qornix";
mysql.password = "qornix";
mysql.database = "qornix";
mysql.connect_timeout = std::chrono::milliseconds{5000};
mysql.default_query_timeout = std::chrono::milliseconds{2000};

auto db = std::make_shared<qornix::db::AsyncDatabase>(
    ioc.get_executor(),
    qornix::db::make_mysql_async_driver_factory(mysql),
    options
);

auto result = co_await db->query(
    "SELECT ? AS value",
    {qornix::db::QueryParam::text("qornix")}
);
```

The driver also accepts a basic DSN form:

```cpp
mysql.connection_info = "mysql://qornix:qornix@127.0.0.1:3306/qornix";
```

Prepared statements and transactions use the same awaitable connection API:

```cpp
auto conn = co_await db->acquire();
co_await conn.prepare("select_user", "SELECT id, name FROM users WHERE id = ?");

qornix::db::QueryOptions prepared;
prepared.prepared = true;
prepared.statement_name = "select_user";
auto user = co_await conn.query("", {qornix::db::QueryParam::text("42")}, prepared);

auto tx = co_await db->begin_transaction();
co_await tx.execute("INSERT INTO audit_log(message) VALUES (?)", {qornix::db::QueryParam::text("created user")});
co_await tx.commit();
```

Integration smoke test:

```bash
export QORNIX_ASYNC_MYSQL_URL='mysql://user:password@127.0.0.1:3306/dbname'
ctest --test-dir cmake-build-debug -R qornix_orm_async_mysql_smoke_test --output-on-failure
```

or:

```bash
export QORNIX_ASYNC_MYSQL_HOST=127.0.0.1
export QORNIX_ASYNC_MYSQL_PORT=3306
export QORNIX_ASYNC_MYSQL_USER=user
export QORNIX_ASYNC_MYSQL_PASSWORD=password
export QORNIX_ASYNC_MYSQL_DATABASE=dbname
ctest --test-dir cmake-build-debug -R qornix_orm_async_mysql_smoke_test --output-on-failure
```

For IDE runners such as CLion that do not inherit shell `export` values, pass the DSN or explicit fields at configure time:

```bash
cmake -S . -B cmake-build-debug \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_MYSQL=ON \
  -DQORNIX_ASYNC_MYSQL_TEST_URL='mysql://user:password@127.0.0.1:3306/dbname'
```

Equivalent explicit-field cache variables are also supported:

```bash
-DQORNIX_ASYNC_MYSQL_TEST_HOST=127.0.0.1
-DQORNIX_ASYNC_MYSQL_TEST_PORT=3306
-DQORNIX_ASYNC_MYSQL_TEST_USER=user
-DQORNIX_ASYNC_MYSQL_TEST_PASSWORD=password
-DQORNIX_ASYNC_MYSQL_TEST_DATABASE=dbname
```

If MySQL environment variables are not set, the smoke test exits successfully with a skip message so local builds do not require a MySQL server by default.

## Real async vs offloaded sync

- Real async PostgreSQL/MySQL drivers must use non-blocking DB socket I/O.
- `SyncOffloadedAsyncDriver` is intentionally named as offloaded. It is useful for SQLite or legacy blocking drivers, but it is not a real async DB driver.
- User documentation should only call a backend real async after its integration tests and DB benchmarks pass.

## Project documentation

Implementation status is tracked in `doc/project_doc/roadmap_async_db_changelog.md`.
