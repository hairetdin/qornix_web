# Async DB layer

Qornix now has a common coroutine-based DB layer for code that must not block HTTP `io_context` worker threads while waiting for database work.

## Status

Implemented in this iteration:

- common awaitable DB types in `include/db/*`;
- `IAsyncDatabaseDriver` contract;
- bounded `AsyncConnectionPool` with `max_connections`, `max_waiters`, acquire timeout, FIFO waiters, idle/max-lifetime checks and graceful shutdown;
- Phase 5 query timeout/deadline resolution and cancellation token primitives;
- Phase 6A hardened `AsyncTransaction` lifecycle with explicit commit/rollback, active-state checks and abandoned-transaction rollback/discard;
- Phase 6B prepared statement cache with generated per-connection names, LRU eviction, placeholder helpers and `query_optional`/`execute_returning` convenience helpers;
- `AsyncDatabase`, `AsyncDbConnection` and `AsyncTransaction` facades;
- mock async driver for unit tests and examples;
- explicit sync/offloaded adapter for legacy blocking drivers;
- ORM-facing `AsyncDatabaseInterface` and Phase 7A `AsyncTableManager` CRUD helpers with driver-aware placeholders;
- Phase 7B `AsyncQueryBuilder` path for dynamic/query-builder-style requests with dialect-aware placeholders and async execution;
- Phase 8 Dynamic API async CRUD registration path with route-level DB limits and stable DB error mapping;
- Phase 9 benchmark tooling through `async_db_benchmark_server` and `scripts/db_benchmark.py`;
- Phase 10 runnable examples under `example/async_postgres_server`, `example/async_mysql_server` and `example/async_orm_server`;
- PostgreSQL async driver built on libpq non-blocking socket polling when `QORNIX_ENABLE_ASYNC_POSTGRES=ON`;
- MySQL async driver built on Boost.MySQL/Boost.Asio TCP when `QORNIX_ENABLE_ASYNC_MYSQL=ON`.

PostgreSQL is implemented as a real non-blocking driver path when enabled and has passed the integration smoke test with a live DSN. MySQL has an Asio-compatible driver path and live smoke validation in the roadmap changelog. The benchmark runner required for Phase 9 is available; `doc/benchmark_async_db.md` should be regenerated with live PostgreSQL/MySQL numbers before a release note claims production performance characteristics. No PostgreSQL/MySQL path is silently wrapped in worker threads and advertised as real async.

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
options.prepared_cache_size = 64;

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

`AsyncDatabaseInterface` and `AsyncTableManager` provide coroutine equivalents for the common ORM convenience path without calling a sync driver on the `io_context` thread. The async table manager builds parameterized SQL using the configured driver dialect: PostgreSQL-style drivers use `$1`, `$2`, while MySQL-style drivers use `?`.

```cpp
auto db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options);

auto user = co_await db->table("users").findById("42");

auto active_users = co_await db->table("users")
    .filter("status", "active")
    .select({"id", "email"})
    .order_by("id DESC")
    .limit(10)
    .prepared()
    .all();

auto created = co_await db->table("users").create({
    {"email", "alice@example.test"},
    {"name", "Alice"}
});

co_await db->table("users").update("42", {{"name", "Alice Smith"}});
co_await db->table("users").delete_("42");

auto tx = co_await db->beginTransaction();
co_await tx.execute(
    "INSERT INTO audit_log(message) VALUES ($1)",
    {qornix::db::QueryParam::text("created user")}
);
co_await tx.commit();
```

For PostgreSQL-like drivers, `insert`/`create` and `update` append `RETURNING *` and use `executeReturning`. For MySQL-style drivers, the async table manager emits non-returning `INSERT`/`UPDATE` statements and returns affected-row metadata instead.

## Async query builder usage

`AsyncQueryBuilder` mirrors the dynamic `QueryBuilder` request shape while executing through `AsyncDatabaseInterface`. It reuses the existing validation and parameter extraction logic, but formats placeholders through the configured async driver dialect and disables `RETURNING` for MySQL-style drivers.

```cpp
auto db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options);

AsyncQueryBuilder builder(db);
builder.setMethod("GET")
    .setTable("users")
    .addValue("id")
    .addValue("email")
    .addFilter("email='alice@example.test'")
    .addOrderBy("id DESC")
    .setLimit(1)
    .prepared();

ResponseData response = co_await builder.exec();
std::string body = co_await builder.getJsonResponse();
```

For a PostgreSQL-style driver this generates `... WHERE email=$1`; for a MySQL-style driver it generates `... WHERE email=?`. `POST` through a MySQL-style driver emits `INSERT INTO ... VALUES (?, ?)` without `RETURNING`, while PostgreSQL-style drivers keep the existing returning behavior.

## Async transactions

`AsyncTransaction` owns a checked-out connection until the transaction is finished. `commit()` and `rollback()` are awaitable operations and release the connection back to the pool immediately after success. Calling `query`, `query_one`, `execute`, `commit` or `rollback` after the transaction has already finished throws `DbErrorCode::QueryRejected` instead of silently reusing the connection.

```cpp
auto tx = co_await db->begin_transaction();
co_await tx.execute(
    "INSERT INTO audit_log(message) VALUES ($1)",
    {qornix::db::QueryParam::text("created user")}
);
co_await tx.commit();
```

If a transaction object is destroyed while still active, Qornix schedules a best-effort async `ROLLBACK` and marks the connection non-reusable before release. This preserves pool safety even though a C++ destructor cannot `co_await`. Explicit `co_await tx.rollback()` is still preferred because it gives the caller deterministic error handling and can keep the connection reusable after a successful rollback. Timeout, cancellation or connection-loss errors inside a transaction discard the checked-out connection rather than returning an uncertain transaction state to the pool.

## Prepared statements and helpers

`QueryOptions::prepared = true` now works without a manually supplied statement name. The common connection wrapper generates a stable per-connection statement name, prepares the SQL once on that physical connection and reuses it while the connection remains in the pool. The cache is held in connection metadata, so reconnect/discard naturally starts with an empty cache and prepared statements do not survive across physical connections.

```cpp
qornix::db::QueryOptions prepared;
prepared.prepared = true;

auto user = co_await db->query_one(
    "SELECT id, name FROM users WHERE id = $1",
    {qornix::db::QueryParam::text("42")},
    prepared
);

auto maybe_user = co_await db->query_optional(
    "SELECT id, name FROM users WHERE id = $1",
    {qornix::db::QueryParam::text("42")},
    prepared
);

auto inserted = co_await db->execute_returning(
    "INSERT INTO users(name) VALUES ($1) RETURNING id, name",
    {qornix::db::QueryParam::text("alice")},
    prepared
);
```

Manual names remain supported for callers that need explicit statement lifecycle control:

```cpp
auto conn = co_await db->acquire();
co_await conn.prepare("select_user", "SELECT id, name FROM users WHERE id = $1");

qornix::db::QueryOptions named;
named.prepared = true;
named.statement_name = "select_user";
auto user = co_await conn.query("", {qornix::db::QueryParam::text("42")}, named);
```

The cache size is controlled by `AsyncPoolOptions::prepared_cache_size` / `db.pool.prepared_cache_size`. When the cache is full, the least-recently-used SQL entry is evicted from the client-side cache. Backends may keep the server-side statement until the physical connection is closed; eviction only means the common layer may prepare that SQL again later. `AsyncDbMetricsSnapshot` exposes `prepared_cache_hits`, `prepared_cache_misses` and `prepared_cache_evictions` for validation and benchmarks.

Placeholder helpers are available for portable SQL construction:

```cpp
auto pg_placeholder = qornix::db::sql_placeholder_for_driver("postgres_async", 1); // "$1"
auto my_placeholder = qornix::db::sql_placeholder_for_driver("mysql_async", 1);    // "?"
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
- metrics for created, closed, discarded and failed connection attempts, timeout/cancel source counters, prepared-cache hit/miss/eviction counters, plus query latency p50/p95/p99 over the in-process sample window.

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
    prepared_cache_size: 64
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

Prepared statements and transactions use the same awaitable connection API. `prepared = true` enables the per-connection cache and generated statement names; explicit commit/rollback releases the connection, while abandoned transactions are rolled back best-effort and discarded for safety:

```cpp
qornix::db::QueryOptions prepared;
prepared.prepared = true;
auto user = co_await db->query_one(
    "SELECT id, name FROM users WHERE id = $1",
    {qornix::db::QueryParam::text("42")},
    prepared
);

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

Prepared statements and transactions use the same awaitable connection API. `prepared = true` enables the per-connection cache and generated statement names; explicit commit/rollback releases the connection, while abandoned transactions are rolled back best-effort and discarded for safety:

```cpp
qornix::db::QueryOptions prepared;
prepared.prepared = true;
auto user = co_await db->query_one(
    "SELECT id, name FROM users WHERE id = ?",
    {qornix::db::QueryParam::text("42")},
    prepared
);

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
- User documentation should separate implementation/smoke-test status from benchmarked production performance claims.

## Benchmarks and examples

Build the async DB benchmark server with the backend you want to validate:

```bash
cmake -S . -B build/async-db-perf \
  -DQORNIX_BUILD_TESTS=ON \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_POSTGRES=ON
cmake --build build/async-db-perf --target async_db_benchmark_server --parallel
```

Run PostgreSQL scenarios:

```bash
export QORNIX_ASYNC_POSTGRES_URL='postgresql://user:password@127.0.0.1:5432/dbname'
scripts/db_benchmark.py \
  --server build/async-db-perf/async_db_benchmark_server \
  --driver postgres \
  --extended
```

For MySQL, configure with `QORNIX_ENABLE_ASYNC_MYSQL=ON`, export `QORNIX_ASYNC_MYSQL_URL` or explicit MySQL fields, and pass `--driver mysql`. The benchmark report is written to `doc/benchmark_async_db.md`.

The runner reports both raw non-2xx `errors` and scenario-level `unexpected` responses. Timeout and overload scenarios intentionally produce 503/504 responses, while normal scenarios should keep `unexpected = 0`. The default benchmark acquire timeout is `1000ms` so the extended `*_normal_select_1000` case can wait behind a 32-connection pool instead of being measured as immediate pool saturation.

Runnable examples:

- `example/async_postgres_server`: real async PostgreSQL route using `AsyncDatabase`;
- `example/async_mysql_server`: real async MySQL route using `AsyncDatabase`;
- `example/async_orm_server`: self-contained ORM facade example using the mock async driver.

## Project documentation

Implementation status is tracked in `doc/project_doc/roadmap_async_db_changelog.md`.

## Dynamic API async CRUD path

Phase 8 adds an explicit async registration path for schema-driven Dynamic API CRUD routes. The sync Dynamic API registration remains available and unchanged; the async registration accepts a preloaded `DynamicSchemaAllowlist` so request execution does not have to open a sync `DatabaseInterface` on the HTTP worker path.

```cpp
qornix_dynamic_api::DynamicApiConfig config;
config.apiPrefix = "/api/dynamic";
config.defaultLimit = 100;
config.maxLimit = 1000;
config.dynamicQueryTimeout = std::chrono::milliseconds{2000};
config.dynamicRouteTimeout = std::chrono::milliseconds{30000};
config.maxDynamicBodySize = 1024 * 1024;
config.maxConcurrentDbOperations = 128;
config.preparedDynamicQueries = true;

auto asyncDatabase = qornix_dynamic_api::makeDynamicAsyncDatabaseInterface(
    server.executor(),
    config.databaseConfig
);
auto allowlist = qornix_dynamic_api::loadDynamicAsyncAllowlist(config);

qornix_dynamic_api::addSchemaDrivenDynamicApiAsyncRoutes(
    server,
    config,
    asyncDatabase,
    std::move(allowlist)
);
```

Include `qornix_dynamic_api/dynamic_api_async_db.h` for the helper functions. `example/dynamic_web_query_builder_server` and `templates/dynamic_api_app` use this wiring when `QORNIX_ENABLE_ASYNC_DB=ON`. Their default SQLite setup runs through the explicit `sqlite_sync_offloaded` adapter, so it is async at the HTTP route boundary but not a native non-blocking SQLite driver; PostgreSQL/MySQL use the native async drivers when their backend flags are enabled.

The async CRUD handler uses `AsyncQueryBuilder` and `AsyncDatabaseInterface`; it does not call the sync `DatabaseInterface` to execute dynamic CRUD queries. It also applies route-level controls from `DynamicApiConfig`:

- `dynamicQueryTimeout` maps to async DB `QueryOptions::timeout` and the DB cancellation token deadline;
- `maxLimit` maps to async DB `QueryOptions::max_rows` and query-builder `LIMIT` clamping;
- `maxDynamicBodySize` maps to the HTTP route body limit and an explicit payload-size check;
- `maxConcurrentDbOperations` maps to the HTTP route concurrency limiter for dynamic CRUD routes;
- `preparedDynamicQueries` enables the common per-connection prepared-statement cache.

DB errors keep the Phase 7B HTTP mapping: unavailable/pool/connection errors become `503`, timeout/cancel becomes `504`, constraint/conflict becomes `409`, query syntax/rejection becomes `400`, and unknown failures become `500`.
