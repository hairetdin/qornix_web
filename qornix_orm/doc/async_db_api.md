# qornix_orm Async DB API

The async DB API is a C++20 coroutine layer built around Boost.Asio awaitables. It exists so HTTP handlers and service code can perform database work without blocking `io_context` worker threads.

## Main types

| Type | Location | Purpose |
| --- | --- | --- |
| `qornix::db::IAsyncDatabaseDriver` | `include/db/async_db_driver.h` | Driver contract: connect, close, query, execute, prepare, begin, commit, rollback. |
| `qornix::db::AsyncConnectionPool` | `include/db/async_db_pool.h` | Bounded pool with max connections, max waiters, acquire timeout, idle/lifetime checks and metrics. |
| `qornix::db::AsyncDatabase` | `include/db/async_db.h` | Facade over the pool for query/execute/transaction helpers. |
| `qornix::db::AsyncDbConnection` | `include/db/async_db_pool.h` | Checked-out connection wrapper with prepared statement cache helpers. |
| `qornix::db::AsyncTransaction` | `include/db/async_db.h` | Transaction lifecycle wrapper. |
| `AsyncDatabaseInterface` | `qornix_orm/database/async_database_interface.h` | ORM-facing async facade. |
| `AsyncTableManager` | `qornix_orm/database/async_table_manager.h` | ORM-style async CRUD helper for table operations. |

## Driver contract

A driver implements:

```cpp
boost::asio::awaitable<void> connect(qornix::db::CancellationToken token);
boost::asio::awaitable<qornix::db::QueryResult> query(
    std::string sql,
    qornix::db::QueryParams params,
    qornix::db::QueryOptions options,
    qornix::db::CancellationToken token);
boost::asio::awaitable<qornix::db::QueryResult> execute(...);
boost::asio::awaitable<void> begin(...);
boost::asio::awaitable<void> commit(...);
boost::asio::awaitable<void> rollback(...);
```

The common layer owns pool limits, timeout resolution, prepared statement cache metrics and connection reuse/discard decisions. Drivers own backend-specific socket/protocol behavior.

## Minimal query example

```cpp
boost::asio::io_context ioc;

qornix::db::AsyncPoolOptions options;
options.pool_name = "main";
options.max_connections = 32;
options.max_waiters = 1024;
options.acquire_timeout = std::chrono::milliseconds{200};
options.query_timeout = std::chrono::milliseconds{2000};
options.prepared_cache_size = 64;

auto db = std::make_shared<qornix::db::AsyncDatabase>(
    ioc.get_executor(),
    qornix::db::make_mock_async_driver_factory(std::chrono::milliseconds{1}),
    options);

boost::asio::co_spawn(ioc, [db]() -> boost::asio::awaitable<void> {
    auto result = co_await db->query(
        "SELECT id, name FROM users WHERE id = $1",
        {qornix::db::QueryParam::text("42")});
}, boost::asio::detached);

ioc.run();
```

## ORM facade example

```cpp
auto orm_db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options);

auto users = co_await orm_db->table("users")
    .filter("status", "active")
    .select({"id", "email"})
    .orderBy("id DESC")
    .limit(10)
    .prepared()
    .findAll();
```

## Transactions

```cpp
auto tx = co_await orm_db->beginTransaction();
co_await tx.execute(
    "INSERT INTO audit_log(message) VALUES ($1)",
    {qornix::db::QueryParam::text("created user")});
co_await tx.commit();
```

If an active transaction is abandoned, the common layer schedules a best-effort rollback and discards uncertain connections when timeout/cancel/connection errors occur.

## Prepared statements

```cpp
qornix::db::QueryOptions prepared;
prepared.prepared = true;

auto user = co_await db->query_one(
    "SELECT id, name FROM users WHERE id = $1",
    {qornix::db::QueryParam::text("42")},
    prepared);
```

The per-connection cache is controlled by `AsyncPoolOptions::prepared_cache_size`. Metrics expose hits, misses and evictions.

## Timeout and cancellation

```cpp
qornix::db::CancellationToken token;
token.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

auto row = co_await db->query_one(sql, params, {}, token);
```

Effective query timeout is the minimum of query option timeout, pool default query timeout and request deadline. Pool acquire timeout is tracked separately from query timeout. On timeout/cancel/error, uncertain checked-out connections are not returned to the idle pool.

## PostgreSQL vs MySQL placeholders

Use helper functions or `AsyncTableManager` when possible:

```cpp
auto pg = qornix::db::sql_placeholder_for_driver("postgres_async", 1); // $1
auto my = qornix::db::sql_placeholder_for_driver("mysql_async", 1);    // ?
```

MySQL-style drivers do not support `RETURNING`; PostgreSQL-style drivers do.

## Error handling

Common async DB errors are represented by `DbError` and `DbErrorCode` in `include/db/async_db_errors.h`. Route-level code should map them to controlled HTTP statuses, for example timeout to 504 and pool saturation to 503.
