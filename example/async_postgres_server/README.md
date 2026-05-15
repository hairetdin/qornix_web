# async_postgres_server

Minimal HTTP server using `qornix::db::AsyncDatabase` with the real libpq non-blocking PostgreSQL driver.

Build:

```bash
cmake -S . -B build/async-pg \
  -DQORNIX_BUILD_EXAMPLES=ON \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_POSTGRES=ON
cmake --build build/async-pg --target async_postgres_server
```

Run:

```bash
export QORNIX_ASYNC_POSTGRES_URL='postgresql://user:password@127.0.0.1:5432/dbname'
./build/async-pg/example/async_postgres_server/async_postgres_server 8021 4
curl http://127.0.0.1:8021/users/42
```
