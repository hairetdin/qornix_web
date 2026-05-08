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
