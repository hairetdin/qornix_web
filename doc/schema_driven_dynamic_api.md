# Schema-driven Dynamic API

Schema-driven Dynamic API is the central Qornix Web feature for database-backed applications.

```text
XML schema
  -> validate against qornix_orm/schema/schema_app.xsd
  -> compare with the current database
  -> build a schema plan
  -> review SQL preview
  -> apply a confirmed plan
  -> use dynamic CRUD/query endpoints
```

The XML schema is the desired model. The database is the current state. Qornix ORM produces a diff, plan, SQL preview and history entry.

## Main modules

```text
qornix_orm            schema core
qornix_dynamic_api    reusable web/API module
Schema Manager        validate/diff/plan/apply UI
Dynamic Query API     schema-aware query endpoint
OpenAPI service       machine-readable API contract
```

## Quick flow

1. Create or edit `schema/app.schema.xml`.
2. Open `/schema-manager`.
3. Validate XML.
4. Build diff.
5. Build plan.
6. Review SQL preview.
7. Confirm apply.
8. Query `/api/dynamic/{entity}` or `/api/dynamic/query`.


## Generated app build and deployment

The recommended way to try the schema-driven flow from scratch is:

```bash
./create_new_project.sh ../my_app --with-dynamic-api
cd ../my_app
mkdir -p build
cd build
cmake ..
cmake --build .
```

The generated Dynamic API application produces a portable deploy bundle:

```text
build/deploy/my_app/
├── my_app
├── config.yaml
├── templates/
├── static/
├── schema/
│   ├── app.schema.xml
│   └── schema_app.xsd
├── doc/
└── logs/
```

Run the generated app from that bundle:

```bash
cd build/deploy/my_app
./my_app
```

The bundle is the artifact to copy to another directory or machine. Copying only the raw `build/my_app` executable is not enough because the generated app needs templates, static assets, schema files, docs and config at runtime.

The app root can also be set explicitly:

```bash
QORNIX_APP_ROOT=/opt/my_app /opt/my_app/my_app
```

See [`dynamic_api_app_deployment.md`](dynamic_api_app_deployment.md) for deployment layouts, `cmake --install`, runtime root discovery, runtime Docker images, shared libraries and troubleshooting.

## Runtime Docker image

Generated Dynamic API applications include `runtime-Dockerfile`. From the generated project root, it builds a runtime image around `build/deploy/my_app/`:

```bash
cmake --build build --target my_app_deploy
docker build -f runtime-Dockerfile -t my_app:runtime .
```

For a simple run:

```bash
docker run --rm -p 8008:8008 my_app:runtime
```

For persistent SQLite/schema state and logs, mount external paths and use container paths in `config.yaml`:

```bash
mkdir -p docker/config data logs
cp build/deploy/my_app/config.yaml docker/config/config.yaml
```

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

```bash
docker run -d --name my_app \
  -p 8008:8008 \
  -v "$PWD/docker/config/config.yaml:/app/config.yaml:ro" \
  -v "$PWD/data:/data" \
  -v "$PWD/logs:/logs" \
  my_app:runtime
```

To transfer without a registry, use `docker save my_app:runtime -o my_app-runtime.tar`, then copy the tar file plus external config and `data/` to the target machine.

## Async DB path status

When `QORNIX_ENABLE_ASYNC_DB=ON`, the Dynamic API CRUD request path can be registered through the async DB layer. Route-level DB timeouts, body limits and concurrency limits are configured under `dynamic_api.async` in generated template config.

Current boundary:

- async: CRUD request execution and async query service path;
- sync-compatible: schema manager, metadata, OpenAPI and schema management services.

Do not describe the Dynamic API as fully async end-to-end until metadata/schema management paths are migrated and validated as async too.
