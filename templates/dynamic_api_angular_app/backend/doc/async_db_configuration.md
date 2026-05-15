# Async DB configuration for Dynamic API template

The Dynamic API template enables `QORNIX_ENABLE_ASYNC_DB=ON` in its generated CMake project. CRUD routes can run through the async DB path while schema manager, metadata and OpenAPI paths remain sync-compatible.

## Default SQLite demo

```yaml
database:
  driver: sqlite
  path: app.sqlite3
dynamic_api:
  async:
    query_timeout_ms: 2000
    route_timeout_ms: 30000
    max_body_size: 1048576
    max_concurrent_db_operations: 128
    prepared_queries: false
```

SQLite uses the explicit sync-offloaded adapter in async route mode. This keeps HTTP coroutine flow non-blocking at the route boundary, but SQLite itself is not a native non-blocking driver.

## PostgreSQL real async driver

Configure the generated project with:

```bash
cmake -S . -B build   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_ASYNC_POSTGRES=ON
```

Example config:

```yaml
database:
  driver: postgres
  host: 127.0.0.1
  port: 5432
  database: qornix
  user: qornix
  password: ${QORNIX_POSTGRES_PASSWORD}
  pool:
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
```

## MySQL real async driver

Configure the generated project with:

```bash
cmake -S . -B build   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_ASYNC_MYSQL=ON
```

Example config:

```yaml
database:
  driver: mysql
  host: 127.0.0.1
  port: 3306
  database: qornix
  user: qornix
  password: ${QORNIX_MYSQL_PASSWORD}
  pool:
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
```

MySQL-style drivers use `?` placeholders and do not use PostgreSQL `RETURNING` clauses.


## Async DB health endpoint

When the generated project is built with `QORNIX_ENABLE_ASYNC_DB=ON`, the Dynamic API template also registers:

```text
GET /api/dynamic/async-db/health
```

The endpoint runs a lightweight `SELECT 1` through the configured async database interface and returns JSON status. It is separate from `GET /health`: `/health` proves that the process/router is alive, while `/api/dynamic/async-db/health` proves that the async DB layer can acquire a connection and execute a query.

Expected success shape:

```json
{"status":"ok","template":"dynamic_api","async_db":"ok","rows":1}
```

If the DB is not configured or the driver is unavailable, the endpoint returns `503 Service Unavailable` with an error JSON body.

## Local smoke checklist

```bash
./create_new_project.sh ../my_api --with-dynamic-api
cd ../my_api
cmake -S . -B build
cmake --build build --parallel
cd build/deploy/my_api
./my_api
```

Open `/`, `/schema-manager`, `/health`, `/api/dynamic/async-db/health` and Dynamic API CRUD/query routes after applying the demo schema.
