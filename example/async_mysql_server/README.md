# async_mysql_server

Minimal HTTP server using `qornix::db::AsyncDatabase` with the Boost.MySQL/Asio async driver.

Build:

```bash
cmake -S . -B build/async-mysql \
  -DQORNIX_BUILD_EXAMPLES=ON \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_MYSQL=ON
cmake --build build/async-mysql --target async_mysql_server
```

Run with a DSN:

```bash
export QORNIX_ASYNC_MYSQL_URL='mysql://user:password@127.0.0.1:3306/dbname'
./build/async-mysql/example/async_mysql_server/async_mysql_server 8022 4
curl http://127.0.0.1:8022/users/42
```

The server also accepts `QORNIX_ASYNC_MYSQL_HOST`, `QORNIX_ASYNC_MYSQL_PORT`, `QORNIX_ASYNC_MYSQL_USER`, `QORNIX_ASYNC_MYSQL_PASSWORD` and `QORNIX_ASYNC_MYSQL_DATABASE`.
