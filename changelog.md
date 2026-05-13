# Changelog

## Async MySQL driver

- Added optional real async MySQL driver behind `QORNIX_ENABLE_ASYNC_MYSQL`.
- The driver uses Boost.MySQL over Boost.Asio TCP instead of worker-thread offload.
- Added async connect, text queries, parameterized queries through temporary server-side prepares, named prepared statements and transaction helpers.
- Added `qornix_orm_async_mysql_smoke_test`, gated by `QORNIX_ASYNC_MYSQL_URL` or explicit `QORNIX_ASYNC_MYSQL_*` environment variables.
- Fixed the async MySQL CMake dependency probe to validate Boost.MySQL with required OpenSSL link dependencies and to avoid stale failed cache results from the previous probe.
- Fixed Boost.MySQL result metadata handling by enabling full metadata mode and falling back to generated column names for empty or duplicate metadata names.
- Confirmed the live async MySQL smoke path passes against MySQL 8.0 for connect, parameterized query, named prepared statement execution and transaction helpers.
- Added CMake cache variables that pass async PostgreSQL/MySQL smoke-test DSNs into CTest `ENVIRONMENT`, so IDE runners like CLion can run live smoke tests without inheriting shell exports.
- MySQL benchmark numbers still need to be run against a real MySQL/MariaDB server and recorded before Phase 9 is complete.

## Async PostgreSQL live smoke validation

- Confirmed `qornix_orm_async_postgres_smoke_test` passes against a live PostgreSQL configuration.
- Phase 2 is now validated for async PostgreSQL connect and parameterized query execution.
- Reworked the PostgreSQL smoke test so GCC 13 also validates named prepared statement execution and transaction helpers directly through `PostgresAsyncDriver`.
- PostgreSQL live benchmark numbers still need to be generated and recorded in `doc/benchmark_async_db.md`.

## Async DB mock test GCC 13 compatibility

- Reworked `async_db_mock_test` to avoid GCC 13 coroutine internal compiler errors triggered by inline coroutine/completion-handler lambdas.
- The test now uses a named awaitable entrypoint, detached spawning and explicit in-coroutine exception capture while preserving the same mock DB assertions.

## Async PostgreSQL smoke-test connection string lifetime

- Fixed `async_postgres_smoke_test` to copy `QORNIX_ASYNC_POSTGRES_URL` into an owned `std::string` before entering the coroutine.
- This prevents libpq from receiving a dangling connection info buffer and failing with corrupted messages such as `missing "=" after ...`.


## Async PostgreSQL GCC 13 smoke-test compatibility

- Reworked `async_postgres_smoke_test` so the GCC 13 build path avoids a compiler internal error in coroutine frame generation.
- GCC 13 now builds the smoke target while still validating async PostgreSQL connect and parameterized query execution.
- Prepared statement and transaction smoke sections remain enabled on non-GCC-13 compilers; a follow-up GCC-13-safe harness should cover those runtime sections if GCC 13 remains the primary compiler.

## Async DB build compatibility

- Fixed CMake include propagation for the async DB headers used by qornix_orm and async PostgreSQL tests.
- Reworked `AsyncTableManager` coroutine methods to avoid a GCC 13 coroutine internal compiler error while keeping the public API unchanged.

## Async PostgreSQL driver

- Added an optional real PostgreSQL async driver behind `QORNIX_ENABLE_ASYNC_POSTGRES`.
- The driver uses libpq non-blocking connect/query primitives with Boost.Asio readiness waits instead of worker-thread offload.
- Added coroutine support for parameterized queries, prepared statements and transactions.
- Added SQLSTATE error mapping and timeout/cancellation handling that prevents timed-out connections from returning to the pool.
- Added `qornix_orm_async_postgres_smoke_test`, enabled when the build option is on and gated at runtime by `QORNIX_ASYNC_POSTGRES_URL`.
- PostgreSQL benchmark numbers still need to be generated against a live PostgreSQL server and recorded in `doc/benchmark_async_db.md`.

## Async DB foundation

- Added the first production-oriented async DB foundation: common awaitable query/result types, `IAsyncDatabaseDriver`, bounded `AsyncConnectionPool`, `AsyncDatabase`, `AsyncDbConnection` and `AsyncTransaction`.
- Added ORM-facing async facade with `AsyncDatabaseInterface` and `AsyncTableManager`.
- Added deterministic `MockAsyncDriver` for tests and examples.
- Added explicit `SyncOffloadedAsyncDriver` for legacy blocking DB drivers; this path is intentionally documented as offloaded sync, not real async DB.
- Added `doc/async_db.md`, `doc/benchmark_async_db.md` and `doc/project_doc/roadmap_async_db_changelog.md`.
- PostgreSQL/MySQL real non-blocking drivers remain tracked in project documentation and must pass integration benchmarks before being presented as fully async backends.


## 2026-05-13 - Async HTTP runtime improvements

- Added coroutine-friendly HTTP controls: route timeout, read/write timeout, cancellation tracking and late-response suppression.
- Added overload protection: global active request limit, per-route concurrency limit and request body size limits.
- Added async middleware support through `add_async_middleware(...)` while keeping the existing sync middleware API compatible.
- Added observability helpers: request metrics, latency percentiles, structured access logs and `/qornix/metrics` endpoint.
- Added graceful shutdown support for stopping accept, waiting for active requests and stopping the `io_context` after the deadline.
- Added `qornix::async::BlockingTaskPool` for offloading blocking database or external API calls away from `io_context` threads.
- Added `qornix::async::AsyncDbPool` facade for awaitable DB-style examples and migration toward real async database drivers.
- Extended the benchmark server with async sleep, DB-pool-limited, blocking-offload and metrics endpoints.
- Updated `scripts/baseline_benchmark.py` to write the latest performance report to `doc/benchmark.md` by default.

### Benchmark report updates

- Changed `/bench/delay/10` and `/bench/delay/100` in the benchmark server to use async timers instead of blocking worker threads.
- Split the DB benchmark into normal-load and overload scenarios.
- Updated the benchmark report with `2xx/4xx/5xx/client errors` columns and per-scenario rejected/timeout metric deltas.

### Reference performance result

The latest checked-in benchmark report is stored in `doc/benchmark.md`. It was generated with the full high-concurrency benchmark command from the README using 32 server threads, 10 second scenarios, `--db-normal-concurrency 128`, `--db-concurrency 10000` and `--idle-connections 10000`.

Highlights from the reference run:

- Fast route: `8799.87` RPS at 256 concurrency with `0` errors.
- Async 100 ms delay route: `2465.51` RPS, p50 `102.81 ms`, p95 `104.57 ms`, `0` errors.
- 10k active `/health` load: `6963.31` RPS with `0` errors.
- 10k active async sleep load: `3970.39` RPS with `0` errors.
- Normal DB-pool scenario at 128 concurrency: `6430.50` RPS, p50 `17.13 ms`, p95 `33.23 ms`, `0` errors.
- 10k idle keep-alive: `10000/10000` connections opened, `0` failures, `65` threads, about `75 MB` RSS.
- Overload and timeout scenarios are represented as controlled failures with rejected/timeout deltas in the report.
