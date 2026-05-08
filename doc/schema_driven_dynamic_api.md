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

See [`dynamic_api_app_deployment.md`](dynamic_api_app_deployment.md) for deployment layouts, `cmake --install`, runtime root discovery, shared libraries and troubleshooting.
