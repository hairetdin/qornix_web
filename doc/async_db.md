# Async DB layer

Qornix now has a common coroutine-based DB layer for code that must not block HTTP `io_context` worker threads while waiting for database work.

## Status

Implemented in this iteration:

- common awaitable DB types in `include/db/*`;
- `IAsyncDatabaseDriver` contract;
- bounded `AsyncConnectionPool` with `max_connections`, `max_waiters` and acquire timeout;
- query timeout and cancellation token primitives;
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
options.max_connections = 32;
options.max_waiters = 1024;
options.acquire_timeout = std::chrono::milliseconds{200};
options.query_timeout = std::chrono::milliseconds{2000};

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

The effective timeout is the minimum of the query option timeout and the remaining cancellation token deadline.


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
