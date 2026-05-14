# Async routes in generated apps

The default generated app includes two async HTTP routes implemented in `handlers/async_handlers.h` and registered from `routes.h`:

```text
GET /async/ping
GET /async/sleep/{ms}
```

`/async/ping` returns a JSON response through `response::json`. `/async/sleep/{ms}` waits on `boost::asio::steady_timer` and returns a text response. The timer does not block the `io_context` worker thread.

## Add a new async route

```cpp
server.get_async("/users/{id}", [](Request req, Url, Params params) -> net::awaitable<Response> {
    const auto id = params.at("id");
    co_return response::json(R"({"id":")" + id + R"("})", req.version());
});
```

Use value-owned `Request`, `Url` and `Params` inside async handlers. Do not store references to request data across `co_await`.

## When to use sync vs async

Use sync handlers for immediate in-memory responses. Use async handlers for timers, DB calls, HTTP clients, file I/O wrappers, Redis, queues or any operation that may wait.
