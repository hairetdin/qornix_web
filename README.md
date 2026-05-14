# Qornix Web

## Schema-driven Dynamic API

Qornix Web is designed for C++ schema-driven backend applications. The central workflow is:

```text
XML schema -> validation -> diff -> plan -> SQL preview -> controlled apply -> dynamic CRUD/query API
```

This lets an application expose database-backed API resources from a validated XML model while keeping database changes explicit and reviewable.

Start with the showcase or generate an app:

```bash
./create_new_project.sh ../my_app --with-dynamic-api
```

See `doc/schema_driven_dynamic_api.md` and `example/schema_driven_backend`.

## Framework

**Qornix Web** is a C++20 framework for building HTTP servers, REST APIs and web applications on top of Boost.Beast, Boost.URL and Boost.JSON.

The framework provides a server core, routing, a middleware pipeline, a DI container, YAML-based configuration, dynamic route extensions and optional modules: ORM, Auth and RAG.

A separate focus area of Qornix Web is the **schema-driven backend**. A dynamic API and database workflow can be built from an XML schema through `qornix_orm`, `QueryBuilder` and the `example/dynamic_web_query_builder_server` example. This allows the XML schema to act as a portable data-model contract, export a schema from an existing database, apply it back to a database, and build dynamic CRUD/query endpoints without writing a dedicated handler for every table.

The main usage model is to create an application as a separate project and link Qornix Web as a CMake library:

```cmake
target_link_libraries(my_app PRIVATE qornix::web_core)
```

---

### Default application template is beginner-friendly

`./create_new_project.sh ../my_app` generates a default web application with a landing page, built-in docs, a health endpoint, static asset serving, a portable deploy bundle and a `runtime-Dockerfile`. After build, run the app from:

```bash
cd build/deploy/my_app
./my_app
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/docs
http://127.0.0.1:8008/health
```

See [`doc/default_app_template.md`](doc/default_app_template.md) for the default template guide.

From the generated project root, build a runtime container image around the deploy bundle:

```bash
docker build -f runtime-Dockerfile -t my_app:runtime .
docker run --rm -p 8008:8008 my_app:runtime
```

### Generated application previews

Default generated application created with:

```bash
./create_new_project.sh ../my_app
```

![Default Qornix generated app](doc/assets/qornix_app_snap.png)

Schema-driven Dynamic API application created with:

```bash
./create_new_project.sh ../my_app --with-dynamic-api
```

![Qornix dynamic API generated app](doc/assets/qornix_dynamyc_api_app_snap.png)

## Architecture components

| Part | Purpose |
|------|---------|
| `qornix_web_core` | Main CMake library of the framework |
| `qornix::web_core` | Alias target used by applications |
| `qornix_web` | Demo executable shipped with the framework |
| `templates/app` | New-application template |
| `create_new_project.sh` | Standalone application generator |
| `doc/qornix_create_new_app_instruction.md` | Detailed application creation guide |
| `doc/roadmap_step_by_step_example.md` | Step-by-step application example based on the framework |
| `qornix_orm` | ORM module located as a directory inside the repository |
| `example/dynamic_web_query_builder_server` | Schema-driven dynamic API, QueryBuilder UI and XML schema manager example |

Recommended workspace layout:

```text
workspace/
├── qornix_web/        # framework
└── my_app/            # separate application based on qornix_web
```

## Key advantages

### Native C++ backend

Qornix Web lets you build backend applications in C++ and ship them as native binaries. This is useful for services where performance, runtime control, integration with existing C++ code and a minimal number of external layers matter.

### Application separated from the framework

A user application is created as an independent CMake project and links the framework through `qornix::web_core`. Application code, routes, handlers, templates and configuration stay in a separate directory.

### Schema-driven backend

The combination of `qornix_orm`, an XML schema and `QueryBuilder` lets you describe the data model declaratively and use it for dynamic database and API work.

Schema-driven capabilities:

- the XML schema describes data structure as a portable application contract;
- the schema can be exported from an existing database;
- the schema can be applied to a database;
- `QueryBuilder` builds SELECT/INSERT/UPDATE/DELETE queries from metadata;
- the dynamic API can work with tables and fields without a separate handler per entity;
- the web interface can use metadata for the query builder, table browser, autocomplete and schema manager.

An end-to-end example is available in `example/dynamic_web_query_builder_server`.

### Modular architecture

The framework consists of independent parts: HTTP core, handlers, middleware, DI, configuration, route extensions, ORM, Auth and RAG. Applications can include only the required modules through CMake options.

### Explicit development model

The primary application model is simple:

```text
routes.h -> handlers/* -> services/models -> templates/static/config
```

When needed, an application can use a more dynamic model:

```text
XML schema -> ORM metadata -> QueryBuilder -> Dynamic API -> UI
```

## Quick start: creating a new application

A detailed step-by-step guide is available in [`doc/qornix_create_new_app_instruction.md`](doc/qornix_create_new_app_instruction.md). The end-to-end application development example is described in [`doc/roadmap_step_by_step_example.md`](doc/project_doc/roadmap_step_by_step_example.md).

Go to the framework directory:

```bash
cd qornix_web
```

Create an application next to the framework:

```bash
./create_new_project.sh ../my_app
```

Build the application:

```bash
cd ../my_app
mkdir -p build
cd build
cmake ..
cmake --build .
```

Run the application from the portable deploy bundle:

```bash
cd deploy/my_app
./my_app
```

Open the landing page and documentation:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/docs
```

Check the health endpoint:

```bash
curl http://127.0.0.1:8008/health
```

Expected response:

```json
{
  "status": "ok",
  "service": "my_app"
}
```

From the generated project root, build and run the generated runtime image:

```bash
docker build -f runtime-Dockerfile -t my_app:runtime .
docker run --rm -p 8008:8008 my_app:runtime
```

For a Dynamic API app, mount writable state outside the container:

```bash
mkdir -p docker/config data logs
cp build/deploy/my_app/config.yaml docker/config/config.yaml
```

Set runtime paths in `docker/config/config.yaml`:

```yaml
server:
  address: 0.0.0.0
  port: 8008
logging:
  to_file: true
  file_path: /logs/server
database:
  driver: sqlite
  path: /data/app.sqlite3
dynamic_api:
  schema:
    file: /data/schema/app.schema.xml
    exported_file: /data/schema/database.schema.xml
    history_file: /data/schema/schema_history.jsonl
```

Run with mounted config, data and logs:

```bash
docker run -d --name my_app \
  -p 8008:8008 \
  -v "$PWD/docker/config/config.yaml:/app/config.yaml:ro" \
  -v "$PWD/data:/data" \
  -v "$PWD/logs:/logs" \
  my_app:runtime
```

To move the image to another computer without a registry:

```bash
docker save my_app:runtime -o my_app-runtime.tar
```

Copy `my_app-runtime.tar` plus external `docker/config/config.yaml` and `data/` if you need to move SQLite data and schema history.

On the target machine:

```bash
docker load -i my_app-runtime.tar
```

## What `create_new_project.sh` does

The script creates an application from the `templates/app` template.

It performs the following actions:

1. accepts a project name or path;
2. validates the project name;
3. copies the application template;
4. substitutes the project name in `CMakeLists.txt` and the generated README;
5. writes the relative path to the `qornix_web` directory;
6. creates the `logs` directory;
7. includes `runtime-Dockerfile` for building a runtime image around `build/deploy/<app>`;
8. creates a project that links the framework through `qornix::web_core`;
9. prints build, run, deploy bundle and Docker image commands.

Examples:

```bash
./create_new_project.sh my_app
./create_new_project.sh ../my_app
./create_new_project.sh /absolute/path/to/my_app
```

The recommended option is to create the application next to the framework:

```bash
workspace/
├── qornix_web/
└── my_app/
```

## Generated application structure

After generation the application has this structure:

```text
my_app/
├── CMakeLists.txt
├── README.md
├── config.yaml
├── main.cpp
├── routes.h
├── app_paths.h
├── runtime-Dockerfile
├── handlers/
│   ├── health_handler.h
│   └── page_handlers.h
├── templates/
├── static/
├── doc/
├── route_extensions/
└── logs/
```

Main files:

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Builds the application and links `qornix::web_core` |
| `main.cpp` | Application entry point |
| `routes.h` | Application route registration |
| `handlers/health_handler.h` | Example handler |
| `config.yaml` | Address, port and logging configuration |
| `templates/404.html` | 404 HTML template |
| `route_extensions/` | Directory for dynamic route extensions |

## How the application links the framework

In the generated `CMakeLists.txt`, the application stores the path to the framework:

```cmake
set(QORNIX_WEB_ROOT "@QORNIX_WEB_ROOT@" CACHE PATH "Path to qornix_web framework")
```

The path is created automatically relative to the new application.

The application disables auxiliary framework targets:

```cmake
set(QORNIX_BUILD_APP OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_RAG OFF CACHE BOOL "" FORCE)
```

Then it includes the framework:

```cmake
add_subdirectory(${QORNIX_WEB_ROOT} ${CMAKE_BINARY_DIR}/qornix_web_build)
```

The application links the library:

```cmake
target_link_libraries(my_app PRIVATE qornix::web_core)
```

If the framework has a non-standard location, the path can be overridden during configuration:

```bash
cmake .. -DQORNIX_WEB_ROOT=/path/to/qornix_web
```

## Adding a new endpoint in an application

### 1. Create a handler

For example, `handlers/article_handler.h`:

```cpp
#pragma once

#include "handler_base.h"
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

namespace http = boost::beast::http;

class ArticleHandler : public HandlerBase {
public:
    http::response<http::string_body> do_get(
        const http::request<http::string_body>&,
        const std::unordered_map<std::string, std::string>& path_params) override {

        boost::json::object body;
        body["id"] = path_params.at("id");
        body["title"] = "Example article";

        http::response<http::string_body> res{http::status::ok, 11};
        res.set(http::field::content_type, "application/json");
        res.body() = boost::json::serialize(body);
        res.prepare_payload();
        return res;
    }
};
```

### 2. Register the route

In `routes.h`:

```cpp
#pragma once

#include "http_server.h"
#include "handlers/health_handler.h"
#include "handlers/article_handler.h"

#include <memory>

inline void setupRoutes(HttpServer& server) {
    server.add_route("GET", "/health", std::make_shared<HealthHandler>());
    server.add_route("GET", "/articles/{id}", std::make_shared<ArticleHandler>());
}
```

Path parameters are available through `path_params`:

```text
GET /articles/42
path_params["id"] == "42"
```

## Application entry point

The generated `main.cpp` uses the framework `ServerManager`:

```cpp
#include "server_manager.h"
#include "routes.h"

int main(int argc, char* argv[]) {
    ServerManager server(argc, argv);
    server.initialize();
    setupRoutes(server.getServer());
    server.run();
    return 0;
}
```

The main extension point for an application is the `setupRoutes(HttpServer& server)` function.

## Building the framework

To build the demo application shipped with the framework:

```bash
cd qornix_web
mkdir -p build
cd build
cmake ..
cmake --build .
```

The demo executable is built when this option is enabled:

```cmake
QORNIX_BUILD_APP=ON
```

To build only `qornix_web_core` without the demo application, examples, RAG, ORM, auth and tests:

```bash
cmake .. \
  -DQORNIX_BUILD_APP=OFF \
  -DQORNIX_BUILD_EXAMPLES=OFF \
  -DQORNIX_BUILD_RAG=OFF \
  -DQORNIX_BUILD_TESTS=OFF \
  -DQORNIX_ENABLE_ORM=OFF \
  -DENABLE_AUTH=OFF
cmake --build .
```

## Framework CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `QORNIX_BUILD_APP` | `ON` | Build the `qornix_web` demo executable |
| `QORNIX_BUILD_EXAMPLES` | `OFF` | Build examples from `example/` |
| `QORNIX_BUILD_RAG` | `OFF` | Build the `qornix_rag` module |
| `QORNIX_BUILD_TESTS` | `ON` | Build `qornix_web` tests |
| `QORNIX_ENABLE_ORM` | `ON` | Enable `qornix_orm` |
| `ENABLE_AUTH` | `ON` | Enable `qornix_auth` |
| `ENABLE_JWT` | `OFF` | Enable JWT support in `qornix_auth` |

Additional ORM options are defined inside `qornix_orm`:

| Option | Description |
|--------|-------------|
| `QORNIX_ENABLE_SQLITE` | SQLite driver |
| `QORNIX_ENABLE_POSTGRES` | PostgreSQL driver |
| `QORNIX_ENABLE_MYSQL` | MySQL driver |
| `QORNIX_BUILD_ORM_TESTS` | ORM tests |

## Generated application options

Each application creates its own CMake options with a prefix based on the project name.

For example, for `my_app`:

| Option | Default | Description |
|--------|---------|-------------|
| `MY_APP_ENABLE_AUTH` | `ON` | Enable `qornix_auth` |
| `MY_APP_ENABLE_ORM` | `OFF` | Enable `qornix_orm` |
| `MY_APP_ENABLE_JWT` | `OFF` | Enable JWT |

Examples:

```bash
# Enable ORM
cmake .. -DMY_APP_ENABLE_ORM=ON

# Enable JWT
cmake .. -DMY_APP_ENABLE_JWT=ON

# Disable auth
cmake .. -DMY_APP_ENABLE_AUTH=OFF
```

ORM is disabled by default in the application template to keep the initial build simple.

## Requirements


Minimum:

- C++20 compiler: GCC 10+, Clang 10+ or compatible;
- CMake 3.20+;
- Boost 1.83+ with `url`, `json`, `log` components;
- yaml-cpp.

For modules:

| Module | Additional dependencies |
|--------|-------------------------|
| `qornix_orm` | SQLite, PostgreSQL `libpq`, MySQL `mysqlclient`, depending on enabled drivers |
| `qornix_auth` | OpenSSL; optionally `jwt-cpp` for JWT |
| `qornix_rag` | Xapian and optional ONNX Runtime, depending on configuration |

Example installation of base dependencies on Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake \
  libboost-all-dev \
  libyaml-cpp-dev \
  libssl-dev
```

## Application configuration

Example `config.yaml`:

```yaml
server:
  address: 127.0.0.1
  port: 8008

logging:
  enabled: true
  level: info
  file: logs/app.log
```

Address and port are configured through `config.yaml`.

## Runtime CLI flags

| Flag | Description |
|------|-------------|
| `--log`, `-l` | Enable logging |
| `--log-level <level>` | Logging level: `trace`, `debug`, `info`, `warning`, `error`, `fatal` |
| `--log-file <filename>` | Write logs to a file |
| `--log-rotation <size>` | Log rotation size in MB |
| `--log-max-files <count>` | Maximum number of log files |

Examples:

```bash
./my_app --log
./my_app --log-level debug
./my_app --log-file logs/server.log
```

## Framework structure

```text
qornix_web/
├── include/              # public core headers
├── server/               # server core implementation
├── handlers/             # demo handlers
├── templates/            # demo templates
├── templates/app/        # new-application template
├── route_extensions/     # dynamic route extensions
├── qornix_orm/           # ORM module, project directory
├── qornix_auth/          # Auth module
├── qornix_rag/           # RAG module
├── example/              # example applications
├── tests/                # core tests
├── doc/                  # project documentation
├── CMakeLists.txt        # framework and qornix_web_core build
├── create_new_project.sh # application generator
└── README.md
```

Public core headers:

| File | Purpose |
|------|---------|
| `include/http_server.h` | HTTP server, routes and connection handling |
| `include/handler_base.h` | Base class for handlers |
| `include/middleware.h` | Base middleware class |
| `include/server_manager.h` | Server initialization, middleware, config and extensions |
| `include/di_container.h` | DI container |
| `include/config_parser.h` | YAML/CLI configuration |
| `include/log_initializer.h` | Logging initialization |
| `include/extension_interface.h` | Dynamic extension interface |
| `include/extension_loader.h` | Loading `.so` extensions |
| `include/template_loader.h` | Loading HTML templates |

## Main capabilities

### Routing

Routes are registered through `HttpServer::add_route`:

```cpp
server.add_route("GET", "/users/{id}", std::make_shared<UserHandler>());
```

Handlers inherit from `HandlerBase` and override the required HTTP methods:

```cpp
class UserHandler : public HandlerBase {
public:
    http::response<http::string_body> do_get(
        const http::request<http::string_body>& req,
        const std::unordered_map<std::string, std::string>& path_params) override;
};
```

### Async routes

Qornix also supports coroutine-based route handlers for operations that can suspend without blocking an `io_context` thread.

Use sync routes for CPU-cheap work that finishes immediately, static responses, and existing `HandlerBase` code. Use async routes for timers, async database clients, async HTTP clients, Redis, queues, or other I/O where the handler can `co_await` instead of sleeping or blocking a thread.

```cpp
server.get_async("/sleep/{ms}",
    [](Request req, Url, Params params) -> net::awaitable<Response> {
        auto executor = co_await net::this_coro::executor;

        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds(std::stoi(params.at("ms"))));
        co_await timer.async_wait(net::use_awaitable);

        co_return response::text("ok", req.version());
    });
```

Available async route helpers:

```cpp
server.get_async(path, handler);
server.post_async(path, handler);
server.put_async(path, handler);
server.patch_async(path, handler);
server.delete_async(path, handler);
server.head_async(path, handler);
server.options_async(path, handler);
server.any_async(path, handler);
```

Async handlers receive `Request`, owning `Url`, and `Params` by value:

```cpp
using AsyncRouteHandler = std::function<net::awaitable<Response>(
    Request,
    Url,
    Params
)>;
```

Do not keep references or pointers to request, URL, or params data across `co_await`. Copy values you need, or use the value parameters directly. Async handlers must return a `Response` value. Exceptions thrown inside an async handler are converted to `500 Internal Server Error`.

Do not put blocking work inside async handlers:

```cpp
// Avoid this inside async handlers.
std::this_thread::sleep_for(std::chrono::seconds(1));
blocking_database_query();
```

Blocking code holds an `io_context` thread and can stop other connections from making progress. Use native async APIs or move blocking work to a dedicated worker pool.

Response helpers for async and sync handlers:

```cpp
co_return response::text("hello", req.version());
co_return response::json(R"({"ok":true})", req.version());
co_return response::status(http::status::accepted, req.version(), "queued");
co_return response::redirect("/login", req.version());
```

Runnable example:

```bash
cmake -S . -B build/async_examples -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=OFF
cmake --build build/async_examples --target async_routes_server --parallel
./build/async_examples/example/async_routes_server/async_routes_server 8010
```

### Middleware

Middleware is implemented by inheriting from `Middleware` and attaching it to the server.

The core includes:

- logging middleware;
- auth middleware;
- a middleware pipeline inside `ServerManager`.

### DI container

`DIContainer` supports lifetime strategies:

| Lifetime | Behavior |
|----------|----------|
| `TRANSIENT` | New object per request |
| `SINGLETON` | One object for the whole application |
| `SCOPED` | One object per scope |

### Dynamic route extensions

`route_extensions/` lets you connect routes from `.so` plugins without changing the main application.

### Schema-driven Dynamic API

Qornix Web supports a scenario where the data structure and dynamic API are built around an XML schema and ORM metadata.

Main elements:

| Component | Purpose |
|-----------|---------|
| XML schema | Declarative data structure description |
| `qornix_orm` | Database, schema, entity API and metadata handling |
| `QueryBuilder` | Dynamic SELECT/INSERT/UPDATE/DELETE query construction |
| Dynamic API | Universal endpoints for tables and records |
| Schema manager | Exporting an XML schema from the database and applying an XML schema to the database |

The `example/dynamic_web_query_builder_server` example demonstrates:

- `/query-builder` — web interface for building queries;
- `/table` — paginated table browser;
- `/table/{table}/{id}` — record viewing and editing;
- `/schema-manager` — XML schema management;
- `/api/dynamic` — universal endpoint for SELECT/INSERT/UPDATE/DELETE;
- `/api/dynamic/{table}` — dynamic API with table in the path;
- `/api/dynamic/{table}/{id}` — dynamic API with table and id in the path;
- `/api/dynamic/meta/tables` — list of tables;
- `/api/dynamic/meta/{table}/fields` — list of table fields;
- `/api/dynamic/meta/autocomplete` — autocomplete metadata;
- `/api/dynamic/schema.xml` — export of the current database XML schema;
- `/api/dynamic/schema/apply` — applying an XML schema to the current database.

This approach is useful for admin panels, internal tools, low-code backends, data management systems and CRUD/API applications where the data model can evolve faster than application handler code.

## Modules

### `qornix_orm`

ORM module for database, XML schema and dynamic API work.

Capabilities:

- entity and model abstractions;
- `QueryBuilder`;
- XML schema as a declarative data-model description;
- export of an XML schema from an existing database;
- applying an XML schema to a database;
- automatic table creation;
- metadata for tables, fields and relationships;
- dynamic SELECT/INSERT/UPDATE/DELETE queries;
- SQLite, PostgreSQL and MySQL drivers;
- REST API controller for entities;
- foundation for the schema-driven dynamic API.

`qornix_orm` is located inside the repository as a regular directory:

```text
qornix_web/qornix_orm
```

When downloading the project archive, no additional git submodule commands are required.

### `qornix_auth`

Auth module for authentication.

Capabilities:

- user model;
- password hashing;
- session handling;
- auth manager;
- optional JWT support.

JWT is enabled with a separate option:

```bash
cmake .. -DENABLE_JWT=ON
```

Or in an application:

```bash
cmake .. -DMY_APP_ENABLE_JWT=ON
```

### `qornix_rag`

RAG module based on Xapian and ONNX Runtime.

It is not built by default:

```bash
cmake .. -DQORNIX_BUILD_RAG=ON
```

## Common issues

### `QORNIX_WEB_ROOT` points to the wrong directory

The generated application cannot find the framework directory.

Solution:

```bash
cmake .. -DQORNIX_WEB_ROOT=/absolute/path/to/qornix_web
```

### Missing `yaml-cpp`

The `yaml-cpp` dependency is not installed.

Solution on Debian/Ubuntu:

```bash
sudo apt install libyaml-cpp-dev
```

### Error in `qornix_orm/CMakeLists.txt`

ORM is enabled, but the `qornix_orm` directory is missing or incomplete.

Solutions:

```bash
# disable ORM
cmake .. -DQORNIX_ENABLE_ORM=OFF

# or for a generated application
cmake .. -DMY_APP_ENABLE_ORM=OFF
```

Or place the full `qornix_orm` directory at:

```text
qornix_web/qornix_orm
```

### Checking the `qornix::web_core` link

The application `CMakeLists.txt` should contain:

```cmake
add_subdirectory(${QORNIX_WEB_ROOT} ${CMAKE_BINARY_DIR}/qornix_web_build)
target_link_libraries(my_app PRIVATE qornix::web_core)
```

## Documentation

| Document | Purpose |
|----------|---------|
| [`doc/qornix_create_new_app_instruction.md`](doc/qornix_create_new_app_instruction.md) | Detailed guide for creating a standalone application based on Qornix Web |
| [`doc/benchmark.md`](doc/benchmark.md) | Last generated performance benchmark report |
| [`changelog.md`](changelog.md) | User-facing project changes and new capabilities |
| [`example/dynamic_web_query_builder_server/README.md`](example/dynamic_web_query_builder_server/README.md) | Dynamic API, QueryBuilder UI and XML schema manager example |
| `README.md` | Framework overview, quick start, architecture and main capabilities |

## Recommended development workflow

1. Keep `qornix_web` as a separate framework directory.
2. Create applications with `create_new_project.sh`.
3. Write business logic in the application `handlers/` directory.
4. Register routes in the application `routes.h`.
5. Configure the application through `config.yaml`.
6. Enable additional capabilities through CMake options and framework modules.

## Async production controls

The async server has a small production-oriented control layer for coroutine routes and middleware.
The defaults are intentionally conservative and can be tuned with `HttpServerOptions` or setter methods:

```cpp
HttpServerOptions options;
options.route_timeout = std::chrono::seconds{3};
options.read_timeout = std::chrono::seconds{30};
options.write_timeout = std::chrono::seconds{30};
options.max_active_requests = 10000;
options.max_request_body_size = 1024 * 1024;
options.structured_access_log = true;

HttpServer server(ioc, endpoint, options);
server.set_route_timeout("/sleep", std::chrono::milliseconds{250});
server.set_route_concurrency_limit("/users/{id}", 100);
server.add_metrics_route(); // GET /qornix/metrics
```

Route timeout expiry returns `504 Gateway Timeout`. Global overload returns `503 Service Unavailable`, while a per-route concurrency limit returns `429 Too Many Requests`. The session tracks cancellation state and suppresses late coroutine responses after a timeout or disconnect.

Async middleware can perform coroutine work before the route handler:

```cpp
server.add_async_middleware([](RequestContext& ctx)
    -> net::awaitable<std::optional<Response>> {
    if (ctx.url.path() == "/private" && !authorized(ctx)) {
        co_return response::status(http::status::unauthorized,
                                   ctx.request->version(),
                                   "unauthorized");
    }
    co_return std::nullopt; // continue pipeline
});
```

For existing blocking drivers, use the offload pool instead of blocking an `io_context` thread:

```cpp
auto blocking_pool = std::make_shared<qornix::async::BlockingTaskPool>(8, 1024, server.metrics_ptr());

server.get_async("/users/{id}", [blocking_pool](Request req, Url, Params params)
    -> net::awaitable<Response> {
    auto user_json = co_await blocking_pool->submit([id = params.at("id")] {
        return blocking_db_fetch_user(id);
    }, std::chrono::seconds{1});
    co_return response::json(user_json, req.version());
});
```

`qornix::async::AsyncDbPool` is a thin awaitable pool facade for async DB-style workloads and examples. It models bounded connection acquisition, waiter limits and query timeout behavior so application code can be migrated to the dedicated async DB layer without creating one database connection per HTTP request.

### Async DB layer

The dedicated async DB foundation lives in `include/db/*` and `qornix_orm/database/async_*`. It provides:

- `qornix::db::AsyncDatabase`, `AsyncDbConnection` and `AsyncTransaction`;
- bounded `AsyncConnectionPool` with acquire timeout and waiter limits;
- `CancellationToken` and `QueryOptions` for query deadline control;
- `IAsyncDatabaseDriver` for real async PostgreSQL/MySQL driver implementations;
- `MockAsyncDriver` for tests and examples;
- `SyncOffloadedAsyncDriver` for explicitly marked legacy blocking drivers;
- `AsyncDatabaseInterface` and `AsyncTableManager` for ORM-facing coroutine code.

See `doc/async_db.md` for usage examples and `doc/project_doc/roadmap_async_db_changelog.md` for implementation status. For standalone `qornix_orm` async DB usage, also read `qornix_orm/Readme.md`, `qornix_orm/doc/standalone_usage.md`, `qornix_orm/doc/async_db_api.md`, `qornix_orm/doc/configuration.md` and `qornix_orm/doc/testing.md`. Real async PostgreSQL and MySQL driver paths are available behind `QORNIX_ENABLE_ASYNC_POSTGRES=ON` and `QORNIX_ENABLE_ASYNC_MYSQL=ON`; live benchmark runs are produced by `scripts/db_benchmark.py` and stored in `doc/benchmark_async_db.md`.

Graceful shutdown stops accepting new connections, waits for active requests until a deadline, then stops the `io_context`:

```cpp
server.graceful_shutdown(std::chrono::seconds{10});
```

## Performance benchmark

The benchmark runner starts `baseline_benchmark_server`, runs reproducible HTTP load scenarios, prints a Markdown report, and writes the latest result to `doc/benchmark.md` by default.

Build the benchmark server:

```bash
cmake -S . -B build/perf -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF
cmake --build build/perf --target baseline_benchmark_server --parallel
```

Run the standard benchmark:

```bash
scripts/baseline_benchmark.py \
  --server build/perf/baseline_benchmark_server \
  --duration 5 \
  --concurrency 64
```

Run the full performance benchmark used for the checked-in report:

```bash
scripts/baseline_benchmark.py \
  --server build/perf/baseline_benchmark_server \
  --port 18080 \
  --server-threads 32 \
  --duration 10 \
  --concurrency 256 \
  --extended-load \
  --load-concurrency 1000 5000 10000 \
  --db-normal-concurrency 128 \
  --db-concurrency 10000 \
  --idle-connections 10000
```

The report is saved to `doc/benchmark.md`. Use `--output path/to/report.md` to write it elsewhere. The report separates successful responses, managed HTTP overload responses and client-side errors. Metrics columns marked as `delta` are per-scenario increments, not cumulative process totals.

The current reference run in `doc/benchmark.md` demonstrates 10k idle keep-alive connections opened successfully, 10k active HTTP load without client errors, async timer routes without blocking worker threads, a clean normal DB-pool scenario at 128 concurrency, and controlled overload/timeout accounting for stress scenarios.

Async DB benchmark runs use the dedicated async DB benchmark server:

```bash
cmake -S . -B build/async-db-perf \
  -DQORNIX_BUILD_TESTS=ON \
  -DQORNIX_ENABLE_ASYNC_DB=ON \
  -DQORNIX_ENABLE_ASYNC_POSTGRES=ON
cmake --build build/async-db-perf --target async_db_benchmark_server --parallel

export QORNIX_ASYNC_POSTGRES_URL='postgresql://user:password@127.0.0.1:5432/dbname'
scripts/db_benchmark.py \
  --server build/async-db-perf/async_db_benchmark_server \
  --driver postgres \
  --extended
```

Use `--driver mysql` with `QORNIX_ENABLE_ASYNC_MYSQL=ON` and MySQL connection environment variables. The report is saved to `doc/benchmark_async_db.md`.
The benchmark runner defaults to a `1000ms` DB acquire timeout so the extended `*_normal_select_1000` scenario measures queued async DB work instead of immediately turning into pool backpressure; use `--acquire-timeout-ms 200` when you explicitly want a more aggressive saturation profile.
