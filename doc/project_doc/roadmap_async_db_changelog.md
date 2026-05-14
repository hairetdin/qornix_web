# Async DB Roadmap Changelog

This changelog tracks implementation progress for `doc/project_doc/roadmap_async_db.md`.

## 2026-05-14 — Benchmark report classification fix

### Completed

- Updated `scripts/db_benchmark.py` to distinguish raw non-2xx `errors` from scenario-level `unexpected` responses.
- Added per-scenario 503/504, acquire-timeout and failed-connect deltas to the generated benchmark report.
- Raised the benchmark runner's default acquire timeout to `1000ms` so the extended `*_normal_select_1000` scenario can queue behind a 32-connection pool instead of immediately measuring pool-acquire saturation.
- Adjusted the slow-query benchmark endpoint so the short timeout applies to the SQL operation, while pool acquisition still uses the normal request/query budget.

### Root cause

The previous benchmark defaults mixed two behaviors: `postgres_normal_select_1000` used `concurrency=1000`, `pool_size=32` and `acquire_timeout=200ms`, which made normal traffic fail with `PoolTimeout`; timeout and overload scenarios also counted expected 503/504 responses as generic errors. The async DB implementation was exercising backpressure correctly, but the report hid acquire-timeout deltas and made expected stress responses look like unexpected failures.

## 2026-05-14 — Dynamic API async wiring in examples/templates

### Completed

- Added `qornix_dynamic_api/dynamic_api_async_db.h` with helper wiring for `AsyncDatabaseInterface` and preloaded `DynamicSchemaAllowlist`.
- Exposed `HttpServer::executor()` so application route setup can build async DB pools on the server `io_context`.
- Updated `example/dynamic_web_query_builder_server` to register Dynamic API CRUD routes through `addSchemaDrivenDynamicApiAsyncRoutes(...)` when `QORNIX_ENABLE_ASYNC_DB=ON`.
- Updated `templates/dynamic_api_app` to enable `QORNIX_ENABLE_ASYNC_DB`, parse async Dynamic API route settings and register async CRUD routes.
- Documented that the default SQLite demo path uses the explicit `sqlite_sync_offloaded` adapter, while PostgreSQL/MySQL require the corresponding native async backend flags.

### Root cause

Phase 8 had added the library-level async CRUD path, but the visible generated/demo applications still called the sync `addSchemaDrivenDynamicApiRoutes(...)` entrypoint. That made the feature hard to validate from the Dynamic API examples even though the underlying async handler path existed.

## 2026-05-13 — Async DB foundation pass

### Completed

- Created a dedicated async DB implementation changelog.
- Added common async DB public contracts:
  - `QueryParam` / `QueryParams`;
  - `Row`;
  - `QueryResult`;
  - `QueryOptions`;
  - `CancellationToken`;
  - `DbError` / `DbErrorCode`.
- Added `IAsyncDatabaseDriver` as the common awaitable driver interface.
- Added `AsyncConnectionPool`:
  - bounded `max_connections`;
  - bounded `max_waiters`;
  - acquire timeout;
  - idle/active/waiter metrics;
  - controlled `PoolRejected` and `PoolTimeout` errors.
- Added high-level coroutine facades:
  - `AsyncDatabase`;
  - `AsyncDbConnection`;
  - `AsyncTransaction`.
- Added `MockAsyncDriver` for deterministic unit tests and API examples.
- Added `SyncOffloadedAsyncDriver` for legacy blocking drivers with explicit `sync_offloaded` naming.
- Added ORM-facing async facade:
  - `AsyncDatabaseInterface`;
  - `AsyncTableManager`.
- Added `qornix_orm_async_db_mock_test` to validate:
  - pool warmup;
  - query/queryOne;
  - table facade `findById`;
  - transaction commit path;
  - query timeout mapping.
- Added user-facing async DB documentation in `doc/async_db.md`.
- Added benchmark placeholder and acceptance matrix in `doc/benchmark_async_db.md`.

### Roadmap phase status

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 0 — Audit and design freeze | Done | Public async API draft is now represented by code and docs. |
| Phase 1 — Common async DB contracts | Done | Contracts, pool, metrics, mock driver and test added. |
| Phase 2 — PostgreSQL async driver | Not done | Needs real libpq non-blocking state machine and integration tests. |
| Phase 3 — MySQL async driver | Not done | Needs Asio-compatible backend integration and tests. |
| Phase 4 — Pool hardening | Partially done | Core bounds/metrics/acquire timeout added; idle lifetime/health check remain future work. |
| Phase 5 — Query timeout and cancellation semantics | Partially done | Common token/timeout semantics added; backend-specific cancel/discard remains driver work. |
| Phase 6 — Transactions and prepared statements | Partially done | Common transaction facade added; backend statement cache remains future work. |
| Phase 7 — ORM async integration | Partially done | Async facade and table manager added; full ORM method parity remains future work. |
| Phase 8 — Dynamic API async integration | Not done | Requires real async drivers and Dynamic API route migration. |
| Phase 9 — Benchmarks | Partially done | Benchmark acceptance doc added; real DB benchmark runner/results remain future work. |
| Phase 10 — Documentation and migration guide | Partially done | Initial docs added; driver-specific docs await real PostgreSQL/MySQL drivers. |

### Important limitation

This pass does **not** complete real PostgreSQL/MySQL async I/O. It completes the common async DB layer required before those drivers can be implemented safely. PostgreSQL/MySQL must not be documented as fully async until Phase 2/3 integration tests and DB benchmarks pass.


## 2026-05-13 — Phase 2 build compatibility fix

### Completed

- Fixed qornix_orm CMake include directories so async DB public headers under `include/db/*` are visible to ORM sources and tests when building through CMake.
- Normalized ORM async facade includes from relative `../../include/db/...` paths to public `db/...` includes.
- Reworked `AsyncTableManager` coroutine methods to materialize SQL strings and parameter vectors before `co_await`; this avoids a GCC 13 coroutine internal compiler error triggered by `co_return co_await` with temporary initializer-list parameters.

### Validation expected

Rebuild the PostgreSQL smoke target after applying this patch:

```bash
cmake --build cmake-build-debug --target async_postgres_smoke_test
```

## 2026-05-13 — Phase 2 PostgreSQL async driver pass

### Completed

- Added `PostgresAsyncDriver` behind `QORNIX_ENABLE_ASYNC_POSTGRES`.
- Added `PostgresAsyncOptions` and `make_postgres_async_driver_factory(...)`.
- Implemented non-blocking PostgreSQL connect flow using `PQconnectStart`, `PQsetnonblocking`, `PQconnectPoll` and Boost.Asio socket readiness waits.
- Implemented parameterized async query execution using `PQsendQueryParams`, `PQflush`, `PQconsumeInput` and `PQgetResult`.
- Implemented prepared statement support using `PQsendPrepare` and `PQsendQueryPrepared`.
- Implemented transaction helpers through async `BEGIN`, `COMMIT` and `ROLLBACK`.
- Added SQLSTATE-to-`DbErrorCode` mapping for common PostgreSQL classes: duplicate key, foreign key, constraints, syntax, conflicts, auth, connection and unavailable errors.
- Added query/connect timeout integration with `CancellationToken` deadlines.
- Added timeout/cancellation safety behavior: timed out or cancelled PostgreSQL operations request cancellation and mark the connection non-reusable so it is not returned to the pool.
- Added optional integration smoke test `qornix_orm_async_postgres_smoke_test`, gated by `QORNIX_ASYNC_POSTGRES_URL`.
- Updated async DB documentation with PostgreSQL build, usage and smoke-test instructions.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 2 — PostgreSQL async driver | Implemented, pending live DB validation | Code path and optional smoke test are added. The container validation compiled the driver and test, but no live PostgreSQL server was available for runtime verification. |
| Phase 3 — MySQL async driver | Not done | Still requires Asio-compatible MySQL backend integration and tests. |
| Phase 9 — Benchmarks | Partially done | PostgreSQL benchmark matrix updated; real PostgreSQL benchmark numbers still need a live DB run. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -c qornix_orm/database/drivers/PostgreSQL/PostgresAsyncDriver.cpp -o /tmp/PostgresAsyncDriver.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -c qornix_orm/tests/async_postgres_smoke_test.cpp -o /tmp/async_postgres_smoke_test.o
g++ /tmp/async_postgres_smoke_test.o /tmp/PostgresAsyncDriver.o $(pkg-config --libs libpq) -lpthread -o /tmp/async_postgres_smoke_test
/tmp/async_postgres_smoke_test
```

The smoke test skipped as expected when `QORNIX_ASYNC_POSTGRES_URL` was not set.

### Remaining limitations

- Live PostgreSQL integration was not executed in the container because no PostgreSQL server/DSN was available.
- PostgreSQL connection health-check/lifetime management is still handled by the common pool basics; proactive health checks remain future hardening.
- PostgreSQL benchmark results are not yet recorded in `doc/benchmark_async_db.md`.
- MySQL async driver remains open.

## 2026-05-13 — Phase 2 GCC 13 smoke-test compatibility fix

### Completed

- Reworked `async_postgres_smoke_test.cpp` to avoid GCC 13 coroutine internal compiler errors triggered by move-only RAII connection/transaction locals in one coroutine frame.
- The GCC 13 build path now validates PostgreSQL async connect and parameterized query execution.
- Prepared statement and transaction smoke sections remain enabled for non-GCC-13 compilers; GCC 13 skips only those smoke-test sections while the production driver code remains compiled.
- Reworked `AsyncDatabaseInterface` forwarding coroutine methods to materialize awaited values before `co_return`, matching the previous `AsyncTableManager` workaround style.

### Validation expected

```bash
cmake --build cmake-build-debug --target async_postgres_smoke_test
```

When running with GCC 13 and a live `QORNIX_ASYNC_POSTGRES_URL`, the smoke test will print that the GCC 13 workaround is active and skip prepared/transaction smoke sections. Full prepared/transaction runtime validation should be run with GCC 14/Clang or covered by a follow-up GCC-13-safe test harness.

## Phase 2 follow-up: PostgreSQL smoke test connection string lifetime

- Fixed `qornix_orm_async_postgres_smoke_test` so `QORNIX_ASYNC_POSTGRES_URL` is copied into an owned `std::string` before coroutine execution.
- Root cause: the smoke coroutine accepted `const std::string&`; passing an environment `const char*` created a temporary `std::string` whose lifetime ended before the coroutine used it. libpq then reported corrupted connection info such as `missing "=" after ...`.

## 2026-05-13 — Async DB mock test GCC 13 compatibility fix

### Completed

- Reworked `async_db_mock_test.cpp` to avoid GCC 13 coroutine internal compiler errors triggered by inline coroutine and completion-handler lambdas.
- The mock test now uses a named `run_mock_test(...)` awaitable and `net::detached`, with exceptions captured inside the coroutine into shared test state.
- Materialized query parameter vectors before `co_await` to keep the test consistent with the GCC 13-safe coroutine style used in the ORM async facade and PostgreSQL smoke test.

### Validation expected

```bash
cmake --build cmake-build-debug --target async_db_mock_test
cmake --build cmake-build-debug --target all
```

## 2026-05-13 — Phase 2 PostgreSQL live smoke validation

### Completed

- Confirmed `qornix_orm_async_postgres_smoke_test` passes locally against a live PostgreSQL configuration.
- Validation command reported:

```text
Test #21: qornix_orm_async_postgres_smoke_test ... Passed
100% tests passed, 0 tests failed out of 1
```

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 2 — PostgreSQL async driver | Validated | Real PostgreSQL async connect and parameterized query path passed the integration smoke test. GCC 13 still skips prepared/transaction smoke sections; those paths remain compiled and should be runtime-validated with GCC 14/Clang or a future GCC-13-safe harness. |
| Phase 9 — Benchmarks | Partially done | PostgreSQL smoke is validated; benchmark numbers against live PostgreSQL still need to be recorded in `doc/benchmark_async_db.md`. |

## 2026-05-13 — Phase 3 MySQL async driver pass

### Completed

- Added `MySqlAsyncDriver` behind `QORNIX_ENABLE_ASYNC_MYSQL`.
- Added `MySqlAsyncOptions` and `make_mysql_async_driver_factory(...)`.
- Integrated Boost.MySQL/Boost.Asio TCP client path for real async MySQL socket I/O.
- Added DSN parsing for the basic form `mysql://user:password@host:port/database` and explicit environment/config fields.
- Implemented async MySQL connect through resolver + MySQL handshake.
- Implemented async text query execution.
- Implemented parameterized query execution by preparing temporary server-side statements for calls with `QueryParams`.
- Implemented named prepared statement support through the common `prepare(...)` + `QueryOptions::statement_name` API.
- Implemented transaction helpers through async `BEGIN`, `COMMIT` and `ROLLBACK`.
- Added MySQL error mapping for common auth, duplicate-key, syntax, conflict, foreign-key and connection classes.
- Added timeout/cancellation checkpoints around MySQL operations; expired/cancelled operations mark the connection non-reusable.
- Added optional integration smoke test `qornix_orm_async_mysql_smoke_test`, gated by `QORNIX_ASYNC_MYSQL_URL` or explicit `QORNIX_ASYNC_MYSQL_*` environment variables.
- Updated async DB documentation with MySQL build, usage and smoke-test instructions.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 3 — MySQL async driver | Implemented and live-validated by async gate | Code path, optional smoke test and post-gate live benchmark validation are recorded in `doc/project_doc/async_gate_mysql_benchmark_final_validation_2026_05_14.md`. |
| Phase 5 — Query timeout and cancellation semantics | Partially done | MySQL path checks request deadlines/cancellation and discards expired/cancelled connections; strict mid-operation cancellation remains a hardening follow-up. |
| Phase 6 — Transactions and prepared statements | Partially done | MySQL prepared statements and transaction helpers are implemented; GCC 13 smoke test skips prepared/transaction runtime sections to avoid coroutine ICE patterns, matching the PostgreSQL workaround. |
| Phase 9 — Benchmarks | Done for gate policy | MySQL live benchmark results were generated after the temporary prepared statement close fix; high-concurrency 1000 uses documented `200/503` saturation semantics. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_mysql_smoke_test.cpp
```

### Validation pending

```bash
cmake -S . -B cmake-build-debug \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_MYSQL=ON
cmake --build cmake-build-debug --target async_mysql_smoke_test
ctest --test-dir cmake-build-debug -R qornix_orm_async_mysql_smoke_test --output-on-failure
```

Use either a URL:

```bash
export QORNIX_ASYNC_MYSQL_URL='mysql://user:password@127.0.0.1:3306/dbname'
```

or explicit fields:

```bash
export QORNIX_ASYNC_MYSQL_HOST=127.0.0.1
export QORNIX_ASYNC_MYSQL_PORT=3306
export QORNIX_ASYNC_MYSQL_USER=user
export QORNIX_ASYNC_MYSQL_PASSWORD=password
export QORNIX_ASYNC_MYSQL_DATABASE=dbname
```

## 2026-05-13 — Phase 3 MySQL CMake dependency probe fix

### Completed

- Fixed the `QORNIX_ENABLE_ASYNC_MYSQL` CMake probe so it validates `boost/mysql.hpp` with the OpenSSL link dependencies required by Boost.MySQL.
- Linked `qornix_orm` with `OpenSSL::SSL` and `OpenSSL::Crypto` when the async MySQL driver is enabled.
- Used a new CMake cache variable for the OpenSSL-aware probe so existing build directories with a stale failed `QORNIX_HAS_BOOST_MYSQL` result do not need manual cache cleanup.

### Root cause

`boost/mysql.hpp` was present on the system, but `check_include_file_cxx()` tried to link a probe executable without OpenSSL. The probe failed on unresolved `SHA1`, `SHA256` and `ERR_*` symbols, then reported the misleading message that `boost/mysql.hpp` was missing.

### Validation performed

```bash
cmake -S . -B /tmp/qornix-cmake-mysql-probe-fixed \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_MYSQL=ON \
  -Wno-dev
cmake --build /tmp/qornix-cmake-mysql-probe-fixed --target async_mysql_smoke_test -j 4
ctest --test-dir /tmp/qornix-cmake-mysql-probe-fixed -R qornix_orm_async_mysql_smoke_test --output-on-failure
```

The same configure/build/test path was also validated in `cmake-build-debug` using the CLion CMake/Ninja configuration.

The CTest smoke binary passed in skip mode because no live `QORNIX_ASYNC_MYSQL_URL` was configured.

## 2026-05-13 — Phase 3 MySQL CTest smoke target validation

### Completed

- Confirmed the async MySQL smoke target is registered in `cmake-build-debug` and passes through CTest after the CMake/OpenSSL dependency probe fix.
- User-reported validation command:

```bash
ctest --test-dir cmake-build-debug \
  -R qornix_orm_async_mysql_smoke_test \
  --output-on-failure
```

- Reported result:

```text
Test #22: qornix_orm_async_mysql_smoke_test ... Passed
100% tests passed, 0 tests failed out of 1
```

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 3 — MySQL async driver | Implemented and explicitly live-validated by async gate | The target configures, builds and passed live smoke. Final gate benchmark validation is recorded in `doc/project_doc/async_gate_mysql_benchmark_final_validation_2026_05_14.md`. |
| Phase 4 — Pool hardening | Ready to start | Phase 4 can proceed in parallel with the remaining live MySQL validation because the common pool exists and MySQL compile/CTest wiring is now stable. |

## 2026-05-13 — Full CTest suite validation after Phase 3 wiring

### Completed

- Confirmed the full `cmake-build-debug` CTest suite passes after the async MySQL CMake/OpenSSL dependency fix and smoke target wiring.
- User-reported validation command:

```bash
/home/damir/Programs/CLion-2025.3.3/clion-2025.3.3/bin/cmake/linux/x64/bin/ctest --extra-verbose
```

- Reported result:

```text
100% tests passed, 0 tests failed out of 22
Total Test time (real) = 0.85 sec
```

### Notes

- The suite includes the async DB tests and both PostgreSQL/MySQL integration smoke targets as registered CTest entries.
- The MySQL integration label reported `0.00 sec`, so this run is recorded as full-suite build/test validation, not as explicit live MySQL runtime validation.

## 2026-05-13 — Phase 3 MySQL live smoke validation

### Completed

- Confirmed the async MySQL smoke test reaches a live MySQL 8.0 server using `QORNIX_ASYNC_MYSQL_URL`.
- Fixed MySQL result metadata handling:
  - Boost.MySQL defaults to `metadata_mode::minimal`, where `metadata::column_name()` can be empty;
  - the async MySQL driver now enables `metadata_mode::full` on each connection;
  - result conversion now falls back to `column_N` for empty or duplicate column names.
- Improved `async_mysql_smoke_test` failure messages so unexpected rows include serialized result data.
- Reworked the MySQL smoke test so GCC 13 validates prepared statements and transaction helpers through `MySqlAsyncDriver` directly instead of skipping those runtime sections.

### Validation performed

```bash
QORNIX_ASYNC_MYSQL_URL='mysql://test_user:<password>@127.0.0.1:3306/test_db' \
  ./cmake-build-debug/qornix_orm/async_mysql_smoke_test
```

Reported result:

```text
async MySQL smoke test passed
```

```bash
QORNIX_ASYNC_MYSQL_URL='mysql://test_user:<password>@127.0.0.1:3306/test_db' \
  ctest --test-dir cmake-build-debug -R qornix_orm_async_mysql_smoke_test -V
```

Reported result:

```text
22: async MySQL smoke test passed
100% tests passed, 0 tests failed out of 1
```

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 3 — MySQL async driver | Validated | Live MySQL smoke now validates connect, parameterized query, named prepared statement execution and transaction helpers. |
| Phase 4 — Pool hardening | Ready to start | Phase 3 smoke validation is complete; benchmark numbers remain tracked under Phase 9. |
| Phase 9 — Benchmarks | Partially done | MySQL live smoke is validated, but MySQL benchmark results still need to be generated and recorded in `doc/benchmark_async_db.md`. |

### Full suite validation

```bash
QORNIX_ASYNC_MYSQL_URL='mysql://test_user:<password>@127.0.0.1:3306/test_db' \
  ctest --test-dir cmake-build-debug --output-on-failure
```

Reported result:

```text
100% tests passed, 0 tests failed out of 22
Total Test time (real) = 0.86 sec
```

## 2026-05-13 — Phase 2 PostgreSQL smoke validation follow-up

### Completed

- Re-ran the async PostgreSQL smoke test against a live PostgreSQL server using the same local test credentials as MySQL.
- Reworked `async_postgres_smoke_test` so GCC 13 validates prepared statements and transaction helpers through `PostgresAsyncDriver` directly instead of skipping those runtime sections.
- Improved PostgreSQL smoke failure messages so unexpected rows include serialized result data.

### Validation performed

```bash
QORNIX_ASYNC_POSTGRES_URL='postgresql://test_user:<password>@127.0.0.1:5432/test_db' \
  ./cmake-build-debug/qornix_orm/async_postgres_smoke_test
```

Reported result:

```text
async PostgreSQL smoke test passed
```

```bash
QORNIX_ASYNC_POSTGRES_URL='postgresql://test_user:<password>@127.0.0.1:5432/test_db' \
  ctest --test-dir cmake-build-debug -R qornix_orm_async_postgres_smoke_test -V
```

Reported result:

```text
21: async PostgreSQL smoke test passed
100% tests passed, 0 tests failed out of 1
```

### Full suite validation

```bash
QORNIX_ASYNC_POSTGRES_URL='postgresql://test_user:<password>@127.0.0.1:5432/test_db' \
QORNIX_ASYNC_MYSQL_URL='mysql://test_user:<password>@127.0.0.1:3306/test_db' \
  ctest --test-dir cmake-build-debug --output-on-failure
```

Reported result:

```text
100% tests passed, 0 tests failed out of 22
Total Test time (real) = 0.87 sec
```

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 2 — PostgreSQL async driver | Validated | Live PostgreSQL smoke now validates connect, parameterized query, named prepared statement execution and transaction helpers under GCC 13. |
| Phase 4 — Pool hardening | Ready to start | PostgreSQL and MySQL live smoke validation are both green; benchmark numbers remain tracked under Phase 9. |

## 2026-05-13 — CTest environment propagation for IDE runners

### Completed

- Added CMake cache variables for async DB integration smoke credentials:
  - `QORNIX_ASYNC_POSTGRES_TEST_URL`;
  - `QORNIX_ASYNC_MYSQL_TEST_URL`;
  - `QORNIX_ASYNC_MYSQL_TEST_HOST`;
  - `QORNIX_ASYNC_MYSQL_TEST_PORT`;
  - `QORNIX_ASYNC_MYSQL_TEST_USER`;
  - `QORNIX_ASYNC_MYSQL_TEST_PASSWORD`;
  - `QORNIX_ASYNC_MYSQL_TEST_DATABASE`.
- The cache variables default from the corresponding shell environment variables at configure time.
- CMake now passes configured values to the PostgreSQL/MySQL smoke test processes through CTest `ENVIRONMENT` properties.
- This makes CLion/IDE CTest runs execute live smoke tests after the DSN is set in the CMake profile, instead of silently taking the skip path because the IDE process did not inherit shell exports.

### Validation performed

```bash
env -u QORNIX_ASYNC_POSTGRES_URL \
    -u QORNIX_ASYNC_MYSQL_URL \
    -u QORNIX_ASYNC_MYSQL_HOST \
    -u QORNIX_ASYNC_MYSQL_PORT \
    -u QORNIX_ASYNC_MYSQL_USER \
    -u QORNIX_ASYNC_MYSQL_PASSWORD \
    -u QORNIX_ASYNC_MYSQL_DATABASE \
  ctest --test-dir cmake-build-debug -R 'qornix_orm_async_(postgres|mysql)_smoke_test' -V
```

Reported result:

```text
21: async PostgreSQL smoke test passed
22: async MySQL smoke test passed
100% tests passed, 0 tests failed out of 2
```

Full suite was also validated with shell DB environment variables removed:

```text
100% tests passed, 0 tests failed out of 22
Total Test time (real) = 0.87 sec
```

## 2026-05-13 — Phase 4 async pool hardening pass

### Completed

- Hardened `AsyncConnectionPool` connection lifecycle metadata:
  - assigned stable per-pool connection ids;
  - tracked created-at and last-used timestamps;
  - retained driver name metadata for diagnostics.
- Added lazy idle/lifetime validation:
  - stale idle connections are discarded before reuse;
  - `idle_timeout` and `max_lifetime` are enforced by the pool;
  - closed or invalid idle drivers are not handed out.
- Added FIFO waiter fairness:
  - queued acquires are ordered by ticket;
  - direct/new acquires do not bypass an existing waiter queue;
  - `max_waiters` still produces controlled `PoolRejected` errors.
- Hardened shutdown behavior:
  - `close()` marks the pool closing and rejects new acquires;
  - idle connections are closed explicitly;
  - active connections are waited on up to `shutdown_timeout` and then discarded on release.
- Extended pool options and config materialization:
  - `pool_name`;
  - `driver_name`;
  - `shutdown_timeout`;
  - `validate_idle_on_acquire`;
  - `AsyncDatabaseInterface::poolOptionsFromConfig(...)` for `db.pool.*` / `database.pool.*` settings.
- Extended metrics:
  - created/closed/discarded connection counters;
  - failed connect counter;
  - query latency p50/p95/p99 snapshot fields.
- Updated `qornix_orm_async_db_mock_test` with pool config parsing, idle expiry/reconnect, shutdown reject and new metric assertions.
- Kept the mock test in a GCC-safe coroutine shape using compiler fences around move-only connection/transaction locals.
- Updated async DB documentation and benchmark-report field guidance for Phase 4 pool metrics.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 4 — Pool hardening | Implemented basic production safeguards | Warmup/lazy create/acquire/release/quarantine-by-discard, FIFO waiter fairness, shutdown reject/wait and pool lifecycle metrics are implemented. Proactive background health checks remain a follow-up; validation is lazy on acquire/release. |
| Phase 5 — Query timeout and cancellation semantics | Partially improved | High-level `AsyncDatabase` now applies pool default query timeout as part of effective query options and records query latency. Driver-specific mid-operation cancellation semantics remain backend work. |
| Phase 9 — Benchmarks | Partially improved | New lifecycle and latency metrics are exposed for future DB benchmark scripts; no live DB benchmark numbers were generated in this pass. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_db_mock_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/database/async_database_interface.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/database/async_table_manager.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core \
  qornix_orm/tests/async_db_mock_test.cpp \
  qornix_orm/database/async_database_interface.cpp \
  qornix_orm/database/async_table_manager.cpp \
  qornix_orm/database/config.cpp \
  -lpthread -o /tmp/async_db_mock_test_phase4
/tmp/async_db_mock_test_phase4
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_mysql_smoke_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_postgres_smoke_test.cpp
```

### Validation limitations

- Full CMake configure/build was not possible in this container because the root project requires yaml-cpp and the standalone ORM configure could not find the `Boost::json` CMake component package. Direct g++ validation and the linked mock runtime test passed.
- No live PostgreSQL/MySQL benchmark run was performed in this pass.

## 2026-05-13 — Phase 5 query timeout and cancellation semantics pass

### Completed

- Added explicit query timeout-source classification through `DbTimeoutSource` on `QueryOptions`:
  - query option timeout;
  - pool default query timeout;
  - request deadline timeout.
- Added `CancellationToken::deadline_expired()` and high-level pre-flight checks so DB work is not started after an already-expired request deadline.
- Implemented the Phase 5 effective timeout resolution in `AsyncDatabase`:

```text
effective_query_timeout = min(
    QueryOptions.timeout,
    AsyncPoolOptions.query_timeout,
    CancellationToken.deadline - now
)
```

- Hardened checked-out connection behavior after timeout/cancellation:
  - `AsyncDbConnection` now marks connections non-reusable after `Timeout`, `Cancelled`, `Connection` or `ConnectionLost` driver errors;
  - timed-out/cancelled mock driver operations close the mock connection so tests validate discard/reconnect behavior;
  - pool acquisition stopped by request deadline now reports `DbErrorCode::Timeout` instead of being counted as pool overload.
- Fixed metrics classification:
  - `PoolTimeout` is no longer counted as query timeout;
  - `acquire_timeouts` remains the pool-overload/acquire-timeout counter;
  - added `query_option_timeouts`, `query_pool_default_timeouts` and `request_deadline_timeouts`;
  - explicit token cancellation continues to increment `query_cancelled`.
- Expanded `qornix_orm_async_db_mock_test` to cover:
  - query-option timeout metrics;
  - pool-default timeout metrics;
  - request-deadline timeout metrics;
  - explicit cancellation metrics;
  - pool acquisition timeout metrics without incrementing query timeout;
  - connection discard after timeout/cancellation paths.
- Updated `doc/async_db.md` and `doc/benchmark_async_db.md` with Phase 5 timeout/cancellation semantics and benchmark fields.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 5 — Query timeout and cancellation semantics | Implemented common semantics | Effective timeout resolution, request-deadline pre-flight checks, non-reusable connection discard and metrics classification are implemented in the common layer. PostgreSQL already closes/discards timed-out/cancelled operations through libpq cancellation. MySQL closes/discards failed/expired operations once control returns from Boost.MySQL; strict out-of-band mid-operation cancellation remains backend-specific hardening work. |
| Phase 9 — Benchmarks | Partially improved | Benchmark field list now separates query-option, pool-default, request-deadline and acquire timeout counters. Live DB benchmark numbers still need to be generated. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_db_mock_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/tests/async_db_mock_test.cpp -o /tmp/async_db_mock_test.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_database_interface.cpp -o /tmp/async_database_interface.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_table_manager.cpp -o /tmp/async_table_manager.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/config.cpp -o /tmp/config.o
g++ /tmp/async_db_mock_test.o /tmp/async_database_interface.o /tmp/async_table_manager.o /tmp/config.o -lpthread -o /tmp/async_db_mock_test_phase5
/tmp/async_db_mock_test_phase5
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -fsyntax-only qornix_orm/tests/async_postgres_smoke_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_mysql_smoke_test.cpp
```

Reported result:

```text
async_db_mock_test passed
```

### Validation limitations

- Full CMake configure/build was still not run in this container because the same environment limitations from Phase 4 remain: root configure requires yaml-cpp and the standalone ORM configure could not find the `Boost::json` CMake component package.
- No live PostgreSQL/MySQL benchmark run was performed in this pass.

## 2026-05-13 — Phase 6A async transaction lifecycle hardening pass

### Completed

- Hardened the common `AsyncTransaction` facade:
  - tracks explicit transaction state (`Active`, `Committed`, `RolledBack`, `Failed`, `Abandoned`);
  - rejects use-after-commit/rollback with `DbErrorCode::QueryRejected`;
  - releases the checked-out connection immediately after successful `commit()` or `rollback()`;
  - discards the checked-out connection on timeout, cancellation, connection and connection-lost errors inside a transaction.
- Added best-effort abandoned transaction cleanup:
  - destroying an active transaction schedules async `ROLLBACK` on the pool executor;
  - the abandoned connection is marked non-reusable before release, so uncertain transaction state is never returned to the pool;
  - explicit `co_await tx.rollback()` remains the preferred deterministic path.
- Added `AsyncDbConnection::rollback_abandoned(...)` and pool executor access needed for safe destructor-triggered rollback scheduling.
- Extended `MockAsyncDriver` with optional shared transaction stats for deterministic tests:
  - begin/commit/rollback counters;
  - active transaction counter;
  - query/prepare/connect/close counters.
- Expanded `qornix_orm_async_db_mock_test` to cover:
  - transaction commit success and use-after-commit rejection;
  - explicit rollback success;
  - abandoned transaction best-effort rollback and discard;
  - cancellation inside a transaction causing discard/reconnect;
  - timeout inside a transaction causing discard/reconnect.
- Updated async DB user documentation and benchmark matrix with Phase 6A transaction lifecycle semantics.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 6 — Transactions and prepared statements | Partially implemented | Common async transaction lifecycle is now hardened and tested. Prepared statement cache, placeholder abstraction helpers and higher-level `query_optional`/`execute_returning` helpers remain for Phase 6B. |
| Phase 9 — Benchmarks | Partially improved | Benchmark matrix now calls out transaction commit/rollback/cancel scenarios; live benchmark numbers still need to be generated. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_db_mock_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/tests/async_db_mock_test.cpp -o /tmp/async_db_mock_test.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_database_interface.cpp -o /tmp/async_database_interface.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_table_manager.cpp -o /tmp/async_table_manager.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/config.cpp -o /tmp/config.o
g++ /tmp/async_db_mock_test.o /tmp/async_database_interface.o /tmp/async_table_manager.o /tmp/config.o -lpthread -o /tmp/async_db_mock_test_phase6a
/tmp/async_db_mock_test_phase6a
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -fsyntax-only qornix_orm/tests/async_postgres_smoke_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_mysql_smoke_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -c qornix_orm/database/drivers/PostgreSQL/PostgresAsyncDriver.cpp -o /tmp/PostgresAsyncDriver_phase6a.o
```

Reported result:

```text
async_db_mock_test passed
```

### Validation limitations

- Full CMake configure/build was still not run in this container because the same environment limitations from Phase 4/5 remain: root configure requires yaml-cpp and the standalone ORM configure could not find the `Boost::json` CMake component package.
- A direct `g++ -fsyntax-only` check of the full MySQL async driver translation unit was not included in the validation list because Boost.MySQL template expansion exceeded the container time budget; the MySQL smoke-test source syntax check passed.
- No live PostgreSQL/MySQL transaction smoke run was performed in this pass.

## 2026-05-13 — Phase 6B prepared statement cache and helper pass

### Completed

- Added common per-connection prepared statement cache:
  - generated statement names use the physical connection id and a per-connection sequence;
  - cache metadata is stored inside `AsyncPooledConnection`, so reconnect/discard naturally invalidates cached prepared statements;
  - `AsyncPoolOptions::prepared_cache_size` controls the client-side cache size;
  - least-recently-used SQL entries are evicted when the cache exceeds the configured size.
- Added automatic prepared execution:
  - `QueryOptions::prepared = true` no longer requires callers to pre-populate `statement_name`;
  - the common `AsyncDbConnection` wrapper prepares once per physical connection and reuses the generated statement name;
  - explicit named `prepare(...)` + `QueryOptions::statement_name` remains supported.
- Added helper APIs:
  - `QueryResult::optional_one()`;
  - `AsyncDbConnection::query_optional(...)`;
  - `AsyncDbConnection::execute_returning(...)`;
  - `AsyncDatabase::query_optional(...)`;
  - `AsyncDatabase::execute_returning(...)`;
  - `AsyncDatabaseInterface::queryOptional(...)`;
  - `AsyncDatabaseInterface::executeReturning(...)`;
  - matching transaction helper methods for `query_optional(...)` and `execute_returning(...)`.
- Added SQL placeholder helpers:
  - PostgreSQL-like drivers use `$1`, `$2`, ...;
  - MySQL-like drivers use `?`.
- Extended config and metrics:
  - `db.pool.prepared_cache_size` / `database.pool.prepared_cache_size` are parsed by `AsyncDatabaseInterface::poolOptionsFromConfig(...)`;
  - `AsyncDbMetricsSnapshot` now exposes `prepared_cache_hits`, `prepared_cache_misses` and `prepared_cache_evictions`.
- Expanded `qornix_orm_async_db_mock_test` to cover:
  - generated prepared statement names;
  - cache hit reuse on the same physical connection;
  - LRU eviction with `prepared_cache_size = 1`;
  - reconnect/discard invalidation;
  - `queryOptional(...)` and `executeReturning(...)` helpers;
  - placeholder helper output;
  - prepared cache config parsing and metrics.
- Updated async DB user documentation and benchmark matrix with Phase 6B prepared-cache fields and examples.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 6 — Transactions and prepared statements | Implemented common layer | Async transaction lifecycle, abandoned rollback/discard, automatic prepared statement cache, placeholder helpers and convenience helpers are implemented and covered by mock tests. Backend-specific server-side deallocate hooks are not implemented; cache eviction is client-side and physical connection close remains the server-side cleanup boundary. |
| Phase 9 — Benchmarks | Partially improved | Benchmark matrix now includes prepared-cache hit/miss/eviction scenarios and fields. Live DB benchmark numbers still need to be generated. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_db_mock_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/tests/async_db_mock_test.cpp -o /tmp/async_db_mock_test_6b.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_database_interface.cpp -o /tmp/async_database_interface_6b.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_table_manager.cpp -o /tmp/async_table_manager_6b.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/config.cpp -o /tmp/config_6b.o
g++ /tmp/async_db_mock_test_6b.o /tmp/async_database_interface_6b.o /tmp/async_table_manager_6b.o /tmp/config_6b.o -lpthread -o /tmp/async_db_mock_test_6b
timeout 20s /tmp/async_db_mock_test_6b
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_mysql_smoke_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -fsyntax-only qornix_orm/tests/async_postgres_smoke_test.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -c qornix_orm/database/drivers/PostgreSQL/PostgresAsyncDriver.cpp -o /tmp/PostgresAsyncDriver_6b.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/database/drivers/MySQL/MySqlAsyncDriver.cpp
```

Reported result:

```text
async_db_mock_test passed
```

### Validation limitations

- Full CMake configure/build was still not run in this container because the same environment limitations from Phase 4/5/6A remain: root configure requires yaml-cpp and the standalone ORM configure could not find the `Boost::json` CMake component package.
- No live PostgreSQL/MySQL benchmark run was performed in this pass.

## 2026-05-13 — Phase 7A async ORM convenience integration pass

### Completed

- Extended the async ORM facade with driver/dialect inspection helpers:
  - `AsyncDatabase::options()` / `driver_name()` / `placeholder()` / `supports_returning()`;
  - `AsyncDatabaseInterface::poolOptions()` / `driverName()` / `placeholder()` / `supportsReturning()`.
- Updated `AsyncDatabaseInterface::createMock()` to set `AsyncPoolOptions::driver_name = "mock_async"` when no driver name is supplied, so ORM placeholder behavior is deterministic in tests and examples.
- Hardened `AsyncTableManager` CRUD SQL generation so it no longer hard-codes PostgreSQL `$1` placeholders:
  - PostgreSQL-like drivers use `$1`, `$2`, ...;
  - MySQL-like drivers use `?`;
  - MySQL-style `insert`/`update` avoid `RETURNING *` and return affected-row metadata.
- Added async ORM convenience aliases and controls:
  - `filter(map)`;
  - sync-style aliases `order_by`, `values`, `all`, `create`, `delete_`, `raw_sql`;
  - `prepared()`, `timeout(...)` and `queryOptions(...)` propagation into table operations;
  - `findOptionalById(...)` using the common `queryOptional(...)` helper.
- Extended `MockAsyncDriverStats` with last-query SQL tracking for dialect-generation assertions.
- Expanded `qornix_orm_async_db_mock_test` coverage for:
  - table `findById` and `findOptionalById`;
  - filtered/selected/ordered/limited `all()` with prepared query options;
  - create alias;
  - MySQL-style placeholder generation;
  - MySQL-style non-returning insert/update/delete SQL.
- Updated `doc/async_db.md` and `doc/benchmark_async_db.md` with Phase 7A ORM facade behavior and benchmark matrix entries.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 7 — ORM async integration | Partially implemented | Async ORM CRUD convenience methods exist and now use driver-aware placeholders plus MySQL-safe mutation SQL. Remaining work is deeper query-builder/dynamic schema integration and real PostgreSQL/MySQL async ORM smoke coverage beyond the common mock layer. |
| Phase 8 — Dynamic API async integration | Ready to start after Phase 7B | Dynamic API request-path migration should follow after the async ORM/query-builder compatibility layer is validated. |
| Phase 9 — Benchmarks | Partially improved | Benchmark matrix now includes async ORM CRUD dialect scenarios; live DB benchmark numbers still need to be generated. |

### Validation performed

```bash
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/database/async_table_manager.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/database/async_database_interface.cpp
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_db_mock_test.cpp
```

The mock runtime test was also linked and executed with section-garbage-collection to avoid requiring unavailable `yaml-cpp` link libraries for unused config-file loading symbols in this container:

```bash
g++ -std=c++20 -O0 -ffunction-sections -fdata-sections -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/tests/async_db_mock_test.cpp -o /tmp/qornix_phase7a_objs/test.o
g++ -std=c++20 -O0 -ffunction-sections -fdata-sections -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_database_interface.cpp -o /tmp/qornix_phase7a_objs/iface.o
g++ -std=c++20 -O0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/async_table_manager.cpp -o /tmp/qornix_phase7a_objs/table.o
g++ -std=c++20 -O0 -ffunction-sections -fdata-sections -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -c qornix_orm/database/config.cpp -o /tmp/qornix_phase7a_objs/config_gc.o
g++ /tmp/qornix_phase7a_objs/test.o /tmp/qornix_phase7a_objs/iface.o /tmp/qornix_phase7a_objs/table.o /tmp/qornix_phase7a_objs/config_gc.o -Wl,--gc-sections -lpthread -o /tmp/async_db_mock_test_phase7a
/tmp/async_db_mock_test_phase7a
```

Result:

```text
async_db_mock_test passed
```

### Remaining limitations

- Full CMake configure/build was still not run in this container because the previous environment limitations remain: root configure requires yaml-cpp and the standalone ORM configure could not find the `Boost::json` CMake component package.
- Phase 7B should still wire the richer query builder/schema-aware ORM paths to async parameter binding and add PostgreSQL/MySQL live async ORM smoke tests.

## 2026-05-13 — Phase 7B async query-builder integration pass

### Completed

- Extended the existing `QueryBuilder` SQL generation path with configurable placeholder formatting while keeping the legacy sync default as PostgreSQL-style `$1`, `$2`, ... placeholders.
- Added configurable `RETURNING` support to `QueryBuilder` generation so MySQL-style async paths can emit non-returning mutation SQL.
- Added `AsyncQueryBuilder` behind `QORNIX_ENABLE_ASYNC_DB`:
  - mirrors the existing dynamic `QueryBuilder` request shape;
  - uses `AsyncDatabaseInterface` for execution instead of sync `DatabaseInterface`;
  - converts extracted SQL parameters into `qornix::db::QueryParams`;
  - supports `QueryOptions`, `prepared()` and `timeout(...)`;
  - exposes awaitable `execute(...)`, `exec(...)` and `getJsonResponse(...)`.
- Added DB error to HTTP status mapping for async query-builder responses:
  - pool/connection unavailable -> 503;
  - timeout/cancelled -> 504;
  - constraint/conflict -> 409;
  - syntax/query rejection -> 400;
  - unknown errors -> 500.
- Added `qornix_orm_async_query_builder_test` as a separate CTest target to avoid re-growing the large GCC-13-sensitive `async_db_mock_test.cpp` translation unit.
- Covered mock dialect scenarios:
  - PostgreSQL-style `GET` query builder output uses `$1` and preserves parameter order;
  - MySQL-style `POST` emits `?` placeholders without `RETURNING`;
  - MySQL-style `PATCH` keeps data parameters before filter parameters and returns affected-row metadata.
- Updated async DB documentation and benchmark matrix with Phase 7B query-builder behavior.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 7 — ORM async integration | Implemented common async ORM/query-builder path | Async table CRUD and async query-builder execution now use driver-aware placeholders and no sync database calls on the async path. Remaining deeper Dynamic API request-path migration is tracked under Phase 8. |
| Phase 8 — Dynamic API async integration | Ready to start | The async query-builder path provides the missing building block for dynamic API request handling without direct sync DB calls on `io_context` threads. |
| Phase 9 — Benchmarks | Partially improved | Benchmark matrix now includes async query-builder dialect/parameterization checks; live DB benchmark numbers still need to be generated. |

### Validation performed

```bash
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/database/query_builder.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_query_builder_test.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_orm/tests/async_db_mock_test.cpp
```

### Remaining limitations

- Full CMake configure/build was still not run in this container because the standalone ORM configure could not find the `Boost::json` CMake component package.
- Runtime validation of the new `qornix_orm_async_query_builder_test` should be performed locally where the full CMake dependency set is available.
- Phase 8 still needs to wire Dynamic API handlers to `AsyncQueryBuilder`/`AsyncDatabaseInterface` and add route-level DB timeout/concurrency/error-mapping policy.

## 2026-05-13 — Phase 8 dynamic API async CRUD integration pass

### Completed

- Added route-level Dynamic API async DB configuration knobs to `DynamicApiConfig`:
  - `dynamicQueryTimeout`;
  - `dynamicRouteTimeout`;
  - `maxDynamicBodySize`;
  - `maxConcurrentDbOperations`;
  - `preparedDynamicQueries`.
- Added `DynamicSchemaAllowlist::addTable(...)` so tests and applications can build a preloaded allowlist without requiring sync DB introspection during request execution.
- Added an async Dynamic API query service path behind `QORNIX_ENABLE_ASYNC_DB`:
  - `DynamicQueryService(config, AsyncDatabaseInterface, DynamicSchemaAllowlist)`;
  - `executeAsync(...)` returning `boost::asio::awaitable<DynamicApiResponse>`;
  - async execution uses `AsyncQueryBuilder` and `AsyncDatabaseInterface` instead of sync `DatabaseInterface`.
- Added async CRUD route registration:
  - `addSchemaDrivenDynamicApiAsyncRoutes(...)` registers schema/metadata/OpenAPI compatibility routes and async CRUD routes;
  - CRUD routes use `HttpServer::any_async(...)` with route timeout, body-size and concurrency limits from `DynamicApiConfig`.
- Preserved the Phase 7B DB error to HTTP status mapping on Dynamic API async CRUD responses.
- Added `dynamic_async_query_service_test` covering:
  - PostgreSQL-style async dynamic GET with `$1` placeholders and prepared query flag;
  - MySQL-style async dynamic POST with `?` placeholders and no `RETURNING`;
  - allowlist validation rejecting disallowed write fields before a DB query is issued.
- Updated `doc/async_db.md`, `doc/benchmark_async_db.md` and `qornix_dynamic_api/README.md` with Phase 8 async Dynamic API usage and benchmark matrix entries.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 7 — ORM async integration | Implemented common async ORM/query-builder path | Dynamic API now consumes the async query-builder path for CRUD execution. |
| Phase 8 — Dynamic API async integration | Implemented common async CRUD path | CRUD request execution can now run through `AsyncDatabaseInterface` with preloaded allowlist, route-level limits and stable DB error mapping. Metadata/schema management routes remain on the existing sync services for now. |
| Phase 9 — Benchmarks | Ready for dynamic API scenarios | Benchmark matrix now includes dynamic API async CRUD scenarios for PostgreSQL/MySQL. |

### Validation performed

```bash
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -I. -Iinclude -Iqornix_dynamic_api -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_dynamic_api/dynamic_api_handlers.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -I. -Iinclude -Iqornix_dynamic_api -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_dynamic_api/dynamic_query_service.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -I. -Iinclude -Iqornix_dynamic_api -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only tests/dynamic_async_query_service_test.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=0 -I. -Iinclude -Iqornix_dynamic_api -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_dynamic_api/dynamic_query_service.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=0 -I. -Iinclude -Iqornix_dynamic_api -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only qornix_dynamic_api/dynamic_api_handlers.cpp
```

### Remaining limitations

- Full CMake configure/build was still not run in this container because the standalone ORM configure could not find the `Boost::json` CMake component package.
- Runtime validation of `dynamic_async_query_service_test` should be performed locally where the full CMake dependency set is available.
- Phase 8 currently moves Dynamic API CRUD execution to the async DB path. Metadata and schema-management endpoints still use the existing sync services and should be considered a separate migration decision because they perform schema introspection and DDL planning.

## 2026-05-13 — Phase 9/10 benchmark tooling and documentation pass

### Completed

- Added `async_db_benchmark_server` for live async DB benchmark runs:
  - supports `mock`, `postgres` and `mysql` drivers;
  - uses real async PostgreSQL/MySQL driver factories when the corresponding CMake flags are enabled;
  - exposes `/db/select`, `/db/prepared`, `/db/transaction`, `/db/slow` and `/db/metrics`;
  - reports `AsyncDbMetricsSnapshot` counters for benchmark delta capture.
- Added `scripts/db_benchmark.py`:
  - starts `async_db_benchmark_server`;
  - drives normal, prepared, transaction, timeout and optional high-concurrency/overload HTTP scenarios;
  - captures HTTP status counts, latency percentiles, process RSS/thread/fd/CPU metrics and DB metric deltas;
  - writes the report to `doc/benchmark_async_db.md` by default.
- Added runnable Phase 10 examples:
  - `example/async_postgres_server`;
  - `example/async_mysql_server`;
  - `example/async_orm_server`.
- Wired examples into `QORNIX_BUILD_EXAMPLES` with backend-specific guards.
- Updated `README.md`, `doc/async_db.md` and `doc/benchmark_async_db.md` with benchmark commands and example locations.

### Roadmap phase status update

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 9 — Benchmarks | Tooling implemented, live numbers pending | The benchmark server and runner are available. `doc/benchmark_async_db.md` still needs to be regenerated with live PostgreSQL/MySQL runs for release-grade benchmark numbers. |
| Phase 10 — Documentation and migration guide | Implemented for current async DB surface | User docs, benchmark instructions and runnable examples exist. Deeper production deployment guidance can continue as ordinary docs maintenance. |

### Validation performed

```bash
python3 -m py_compile scripts/db_benchmark.py
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -DQORNIX_ENABLE_ASYNC_POSTGRES=0 -DQORNIX_ENABLE_ASYNC_MYSQL=0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only tests/async_db_benchmark_server.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -DQORNIX_ENABLE_ASYNC_POSTGRES=0 -DQORNIX_ENABLE_ASYNC_MYSQL=0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only example/async_orm_server/main.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -DQORNIX_ENABLE_ASYNC_POSTGRES=1 -DQORNIX_ENABLE_ASYNC_MYSQL=0 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core $(pkg-config --cflags libpq) -fsyntax-only example/async_postgres_server/main.cpp
g++ -std=c++20 -O0 -DQORNIX_ENABLE_ASYNC_DB=1 -DQORNIX_ENABLE_ASYNC_POSTGRES=0 -DQORNIX_ENABLE_ASYNC_MYSQL=1 -I. -Iinclude -Iqornix_orm -Iqornix_orm/database -Iqornix_orm/core -fsyntax-only example/async_mysql_server/main.cpp
cmake --build cmake-build-debug --target async_db_benchmark_server -j 4
scripts/db_benchmark.py --server cmake-build-debug/async_db_benchmark_server --driver mock --duration 0.2 --normal-concurrency 4 --timeout-concurrency 2 --pool-size 2 --max-waiters 4 --output /tmp/qornix_async_db_mock_benchmark.md --quiet-server
ctest --test-dir cmake-build-debug -R 'qornix_orm_async_db_mock_test|qornix_orm_async_query_builder_test|dynamic_async_query_service_test' --output-on-failure
ctest --test-dir cmake-build-debug --output-on-failure
```

### Validation limitations

- Live PostgreSQL/MySQL benchmark numbers were not generated in this pass.
- The full `cmake-build-debug` CTest suite passed with `QORNIX_ENABLE_ASYNC_POSTGRES=OFF` and `QORNIX_ENABLE_ASYNC_MYSQL=OFF`; live driver benchmark validation still requires a separate build with the corresponding backend enabled and live DSNs.

## Async gate documentation/template synchronization

- Added `doc/project_doc/roadmap_async_gate.md` as the merge/release gate for the async branch.
- Added `doc/project_doc/roadmap_async_gate_changelog.md` for gate execution tracking.
- Expanded `qornix_orm` standalone and async DB documentation.
- Updated generated default app docs and routes with async HTTP examples.
- Added Dynamic API template async DB configuration docs.
- Standardized active Boost requirements on Boost 1.83.

## 2026-05-14 — Async gate MySQL live benchmark closure

Async gate follow-up closed the MySQL live validation gap:

```text
[x] qornix_orm_async_mysql_smoke_test passed against live MySQL/MariaDB DSN
[x] temporary non-cached prepared statements are closed after execution
[x] full MySQL benchmark matched expected status sets under documented 200/503 high-concurrency saturation policy
[x] final benchmark metrics reported active_connections=0 and queued_waiters=0
```

Evidence:

- `doc/project_doc/async_gate_mysql_benchmark_final_validation_2026_05_14.md`
- `doc/benchmark_async_db_mysql_gate.md`

Note: strict 200-only validation at `mysql_normal_select_1000` remains available via `scripts/db_benchmark.py --strict-high-concurrency`, but it is not the default gate policy for MySQL with pool-size 32 and acquire-timeout 1000ms.
