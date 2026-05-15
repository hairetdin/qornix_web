# qornix_orm Configuration

This document describes configuration conventions for standalone ORM and async DB usage.

## SQLite local development

```yaml
database:
  driver: sqlite
  path: ./app.sqlite3
```

SQLite is the default local/test driver. In async HTTP paths it should be treated as a sync/offloaded backend, not as a native non-blocking database driver.

## PostgreSQL async

```yaml
database:
  driver: postgres
  host: 127.0.0.1
  port: 5432
  database: qornix
  user: qornix
  password: ${QORNIX_POSTGRES_PASSWORD}
  sslmode: disable
  pool:
    min_connections: 0
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
    idle_timeout_ms: 30000
    max_lifetime_ms: 1800000
    prepared_cache_size: 64
```

Build with:

```bash
cmake -S . -B build/postgres   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_ASYNC_POSTGRES=ON
```

Live tests use `QORNIX_ASYNC_POSTGRES_URL`.

## MySQL async

```yaml
database:
  driver: mysql
  host: 127.0.0.1
  port: 3306
  database: qornix
  user: qornix
  password: ${QORNIX_MYSQL_PASSWORD}
  pool:
    min_connections: 0
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
    idle_timeout_ms: 30000
    max_lifetime_ms: 1800000
    prepared_cache_size: 64
```

Build with:

```bash
cmake -S . -B build/mysql   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_ASYNC_MYSQL=ON
```

Live tests can use either `QORNIX_ASYNC_MYSQL_URL` or the split variables documented in `testing.md`.

## Environment-based secrets

Do not commit real passwords in `config.yaml`. Prefer environment variables or deployment-specific files:

```bash
export QORNIX_ASYNC_POSTGRES_URL='postgresql://qornix:secret@127.0.0.1:5432/qornix'
export QORNIX_ASYNC_MYSQL_URL='mysql://qornix:secret@127.0.0.1:3306/qornix'
```

## Template defaults

`templates/dynamic_api_app/config.yaml` includes SQLite defaults and async route/pool limits for first-run demos. Switch to PostgreSQL/MySQL for real async DB backend validation.
