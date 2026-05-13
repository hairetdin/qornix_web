# Async DB Roadmap Changelog

This changelog tracks implementation progress for `doc/project_doc/roadmap_async_db.md`.

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
| Phase 3 — MySQL async driver | Implemented, pending live DB validation | Code path and optional smoke test are added. A live MySQL/MariaDB DSN is required to validate runtime behavior. |
| Phase 5 — Query timeout and cancellation semantics | Partially done | MySQL path checks request deadlines/cancellation and discards expired/cancelled connections; strict mid-operation cancellation remains a hardening follow-up. |
| Phase 6 — Transactions and prepared statements | Partially done | MySQL prepared statements and transaction helpers are implemented; GCC 13 smoke test skips prepared/transaction runtime sections to avoid coroutine ICE patterns, matching the PostgreSQL workaround. |
| Phase 9 — Benchmarks | Partially done | MySQL benchmark matrix is documented; live benchmark results still need to be generated. |

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
| Phase 3 — MySQL async driver | Implemented, pending explicit live DB validation | The target now configures, builds and runs under CTest. The smoke binary still skips successfully when `QORNIX_ASYNC_MYSQL_URL` or `QORNIX_ASYNC_MYSQL_USER` is not configured, so Phase 3 should only be marked fully validated after a run against a live MySQL/MariaDB DSN is confirmed. |
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
