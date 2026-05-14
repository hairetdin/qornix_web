# qornix_orm Testing

## Self-contained tests

The default test profile uses SQLite/mock paths and does not require PostgreSQL or MySQL services.

```bash
cmake -S . -B build/orm-tests   -DQORNIX_ENABLE_ORM=ON   -DQORNIX_BUILD_ORM_TESTS=ON   -DQORNIX_ENABLE_SQLITE=ON   -DQORNIX_ENABLE_POSTGRES=OFF   -DQORNIX_ENABLE_MYSQL=OFF   -DQORNIX_ENABLE_ASYNC_DB=ON
cmake --build build/orm-tests --parallel
ctest --test-dir build/orm-tests --output-on-failure
```

Useful filters:

```bash
ctest --test-dir build/orm-tests -R async --output-on-failure
ctest --test-dir build/orm-tests -R schema --output-on-failure
ctest --test-dir build/orm-tests -R query_builder --output-on-failure
```

## PostgreSQL async live test

```bash
export QORNIX_ASYNC_POSTGRES_URL='postgresql://qornix:secret@127.0.0.1:5432/qornix'
cmake -S . -B build/orm-postgres   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_ASYNC_POSTGRES=ON   -DQORNIX_BUILD_ORM_TESTS=ON
cmake --build build/orm-postgres --parallel
ctest --test-dir build/orm-postgres -R postgres --output-on-failure
```

## MySQL async live test

```bash
export QORNIX_ASYNC_MYSQL_HOST=127.0.0.1
export QORNIX_ASYNC_MYSQL_PORT=3306
export QORNIX_ASYNC_MYSQL_USER=qornix
export QORNIX_ASYNC_MYSQL_PASSWORD=secret
export QORNIX_ASYNC_MYSQL_DATABASE=qornix
cmake -S . -B build/orm-mysql   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_ASYNC_MYSQL=ON   -DQORNIX_BUILD_ORM_TESTS=ON
cmake --build build/orm-mysql --parallel
ctest --test-dir build/orm-mysql -R mysql --output-on-failure
```

## Benchmarks

Run benchmark reports when you need local performance numbers:

```bash
python3 scripts/db_benchmark.py   --server build/async-db-perf/async_db_benchmark_server   --driver postgres   --duration 10   --normal-concurrency 128   --high-concurrency 1000   --overload-concurrency 10000   --pool-size 32   --max-waiters 1024   --extended   --output doc/benchmark_async_db_postgres.md
```

Repeat with `--driver mysql` and MySQL environment variables to collect MySQL numbers.

## Skip behavior

When live DSNs are not provided, PostgreSQL/MySQL integration tests should be skipped or excluded from the local build matrix.
