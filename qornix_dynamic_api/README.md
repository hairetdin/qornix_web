# qornix_dynamic_api

Reusable module for the Qornix Web **Schema-driven Dynamic API**.

## Provides

- Schema Manager API: export, validate, diff, plan, apply and history.
- Dynamic CRUD/query API with schema-aware validation.
- Schema Manager UI v2 assets.
- OpenAPI generation for dynamic endpoints.
- Permissions/policy foundation.

## Register routes

```cpp
qornix_dynamic_api::DynamicApiConfig config;
config.databaseConfig.driver = "sqlite";
config.databaseConfig.database = "app.sqlite3";
qornix_dynamic_api::addSchemaDrivenDynamicApiRoutes(server, config);
```

## Main endpoints

```text
GET  /api/dynamic/schema/export
POST /api/dynamic/schema/validate
POST /api/dynamic/schema/diff
POST /api/dynamic/schema/plan
POST /api/dynamic/schema/apply
GET  /api/dynamic/schema/history
GET  /api/dynamic/meta/tables
POST /api/dynamic/query
GET  /api/dynamic/openapi.json
```

## Async CRUD registration

When `QORNIX_ENABLE_ASYNC_DB=ON`, CRUD routes can be registered on the async DB path while schema manager, metadata and OpenAPI routes remain compatible with the existing sync services:

```cpp
qornix_dynamic_api::DynamicApiConfig config;
config.dynamicQueryTimeout = std::chrono::milliseconds{2000};
config.dynamicRouteTimeout = std::chrono::milliseconds{30000};
config.maxDynamicBodySize = 1024 * 1024;
config.maxConcurrentDbOperations = 128;
config.preparedDynamicQueries = true;

auto asyncDatabase = qornix_dynamic_api::makeDynamicAsyncDatabaseInterface(
    server.executor(),
    config.databaseConfig
);
auto allowlist = qornix_dynamic_api::loadDynamicAsyncAllowlist(config);

qornix_dynamic_api::addSchemaDrivenDynamicApiAsyncRoutes(
    server,
    config,
    asyncDatabase,
    std::move(allowlist)
);
```

The async CRUD path uses `AsyncQueryBuilder` and `AsyncDatabaseInterface`, expects the allowlist to be preloaded outside the request path, and maps DB timeout/unavailable/conflict/query errors to stable HTTP responses.
Include `qornix_dynamic_api/dynamic_api_async_db.h` for the helper functions above. SQLite is wired through the explicit `sync_offloaded` adapter; PostgreSQL/MySQL use real async drivers only when the corresponding CMake backend flag is enabled.
