# async_orm_server

Minimal HTTP server using the ORM-facing `AsyncDatabaseInterface` and `AsyncTableManager` on the mock async driver. It is self-contained and useful for checking coroutine route wiring without a live DB server.

Build:

```bash
cmake -S . -B build/async-orm \
  -DQORNIX_BUILD_EXAMPLES=ON \
  -DQORNIX_ENABLE_ASYNC_DB=ON
cmake --build build/async-orm --target async_orm_server
```

Run:

```bash
./build/async-orm/example/async_orm_server/async_orm_server 8023 4
curl http://127.0.0.1:8023/users/42
curl http://127.0.0.1:8023/users
```
