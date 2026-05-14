# Routing and handlers

Qornix Web routes map URL patterns to handler objects.

The generated app registers routes in `routes.h`:

```cpp
inline void setupRoutes(HttpServer& server) {
    server.add_route("/", makeHomePageHandler());
    server.add_route("/health", std::make_shared<HealthHandler>());
}
```

## Handler basics

Handlers usually derive from `HandlerBase` and override one or more HTTP method functions.

```cpp
class HelloHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>&,
        http::response<http::string_body>& res,
        const urls::url_view&,
        const std::map<std::string, std::string>&
    ) override {
        buildJsonResponse(res, http::status::ok, R"({"hello":"qornix"})");
    }
};
```

Register it:

```cpp
server.add_route("/hello", std::make_shared<HelloHandler>());
```

## Path parameters

Route patterns can include placeholders:

```cpp
server.add_route("/users/{id}", std::make_shared<UserHandler>());
```

The handler receives resolved values through `path_params`:

```cpp
auto id = path_params.at("id");
```

## Static files

The default app registers:

```text
GET /static/{filename}
```

Files are served from the `static/` directory inside the application root.

## HTML pages

The generated template includes `AppPageHandler`, which loads HTML files from `templates/`. This is useful for simple landing pages, docs pages and server-rendered UI.

## JSON responses

Use helper functions from `HandlerBase`:

```cpp
buildJsonResponse(res, http::status::ok, json);
buildHtmlResponse(res, http::status::ok, html);
buildTextResponse(res, http::status::ok, text);
```

## Next step

Create a new handler, register a route and rebuild. This is the fastest way to understand the framework flow.

## Async route handlers

Use `get_async`, `post_async`, `put_async`, `patch_async`, `delete_async`, `head_async`, `options_async` or `any_async` for coroutine handlers:

```cpp
server.get_async("/async/ping", makeAsyncPingHandler());
server.get_async("/async/sleep/{ms}", makeAsyncSleepHandler());
```

The generated template implements these in `handlers/async_handlers.h`. `/async/sleep/{ms}` uses `boost::asio::steady_timer` so the delay does not block other connections.

Use async routes for database calls, network calls, timers and slow downstream work. Use sync handlers only for immediate CPU-local responses.
