# Dynamic Web Query Builder Server

`dynamic_web_query_builder_server` is a Qornix Web Server example application that shows how to expose a universal web UI for dynamic database queries.

The example provides:

- a home page with interactive API examples;
- a `/query-builder` page for building queries from a form;
- a `/table` page for browsing tables with pagination;
- a `/table/{table}/{id}` page for viewing and editing individual records with inline field editing;
- a `/schema-manager` page for exporting an XML schema from the database and applying an XML schema to the database;
- a universal `/api/dynamic` endpoint for SELECT/INSERT/UPDATE/DELETE through `QueryBuilder`;
- metadata endpoints for tables, fields and universal autocomplete;
- `/api/dynamic/schema.xml` for exporting the current demo database XML schema;
- `/api/dynamic/schema/apply` for applying an XML schema to the demo database;
- an automatically created SQLite demo database with `categories`, `products`, `customers` and `orders` tables;
- a Python performance test suite for API performance checks.

## What The Server Does

On startup, the server:

1. Creates or opens the `dynamic_web_query_builder_demo.sqlite3` SQLite database.
2. Creates the demo schema if it does not exist yet.
3. Seeds demo data through `INSERT OR IGNORE`.
4. Registers HTML pages (`home`, `query-builder`, `table`, `row_view`, `schema-manager`), static files and API routes.
5. Allows generating the current demo database XML schema through `/api/dynamic/schema.xml`.
6. Allows applying an XML schema to the demo database through `/api/dynamic/schema/apply`.
7. When `QORNIX_ENABLE_ASYNC_DB=ON`, registers CRUD routes through the async Dynamic API path. The SQLite demo uses the explicit `sqlite_sync_offloaded` adapter, while PostgreSQL/MySQL can use real async drivers when their backend flags are enabled.
8. Starts the HTTP server with the framework settings, by default on `127.0.0.1:8008`.

The demo database is created in the example directory:

```text
example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.sqlite3
```

The database file is not committed. It is ignored by the local `.gitignore`.

The demo database XML schema is generated on demand and saved next to the database:

```text
example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.schema.xml
```

The XML schema file is also not committed.

## Main URLs

- `/` - home page with API examples.
- `/query-builder` - interactive Query Builder.
- `/table` - table browser with pagination and autocomplete.
- `/table/{table}/{id}` - view and edit an individual record.
- `/schema-manager` - XML schema management.
- `/static/{filename}` - static files from `templates`.
- `/api/dynamic` - dynamic requests where table and method are passed in JSON.
- `/api/dynamic/{table}` - dynamic requests with the table in the path.
- `/api/dynamic/{table}/{id}` - dynamic requests with table and id in the path.
- `/api/dynamic/meta/tables` - table list.
- `/api/dynamic/meta/{table}/fields` - field list for a table.
- `/api/dynamic/meta/autocomplete` - universal autocomplete.
- `/api/dynamic/schema.xml` - export the current demo SQLite database XML schema.
- `/api/dynamic/schema/apply` - apply an XML schema to the current demo SQLite database.

## Demo Schema

The server creates these tables automatically:

- `categories` - product categories.
- `products` - products linked to categories.
- `customers` - customers.
- `orders` - orders linked to customers and products.

Relationships:

```text
products.category_id -> categories.id
orders.customer_id   -> customers.id
orders.product_id    -> products.id
```

## Request Format

The UI primarily sends a JSON body:

```json
{
  "method": "GET",
  "table": "orders",
  "query": {
    "fields": [
      "orders.id",
      "orders.order_date",
      "products.name",
      "customers.city",
      "orders.quantity",
      "orders.status",
      "orders.total_amount"
    ],
    "join": [
      "JOIN products ON orders.product_id=products.id",
      "JOIN customers ON orders.customer_id=customers.id"
    ],
    "filter": ["orders.status='paid'"],
    "order_by": ["orders.order_date DESC"],
    "limit": 10
  }
}
```

The home page HTTP transport uses `POST /api/dynamic/`, while the actual operation is selected by the `"method"` field. A JSON request with `"method": "GET"` is therefore executed as a SELECT through `QueryBuilder`.

The backend also supports the URL DSL form `?query=(method:GET;table:...)`, but the UI uses JSON for complex JOIN/filter requests so it does not depend on URL encoding.

## Running

From the repository root:

```bash
cmake --build build --target dynamic_web_query_builder_server
./build/example/dynamic_web_query_builder_server/dynamic_web_query_builder_server
```

After startup, open:

```text
http://localhost:8008/
http://localhost:8008/query-builder
http://localhost:8008/table
http://localhost:8008/schema-manager
```

If the build directory does not exist yet:

```bash
cmake -S . -B build
cmake --build build --target dynamic_web_query_builder_server
```

## Important Files

- `main.cpp` - example entry point.
- `routes.h` - route registration.
- `handlers.h` - HTML/API handlers.
- `demo_database.h` - SQLite demo database, schema and seed data creation.
- `example_paths.h` - template and demo database path resolution.
- `config.yaml` - YAML configuration for the demo SQLite connection.
- `schema_roundtrip_test.cpp` - C++ round-trip XML schema generation test.
- `templates/home.html` - home page with API examples.
- `templates/query_builder.html` - interactive Query Builder.
- `templates/table.html` - table browser with pagination.
- `templates/row_view.html` - view and edit an individual record.
- `templates/schema_manager.html` - XML schema management.
- `templates/app.js` - JavaScript for the home page.
- `templates/schema_manager.js` - JavaScript for the schema management page.
- `templates/styles.css` - shared styles.
- `perf_test/` - Python performance test suite.
- `CMakeLists.txt` - standalone example target build.

## How This Example Was Built

1. The example directory was created:

```text
example/dynamic_web_query_builder_server
```

2. The `main.cpp` entry point was added.

It creates `ServerManager` and then registers a custom route function:

```cpp
auto server_manager = create_server_manager(argc, argv);
server_manager->addRouteFunction(setupDynamicRoutes);
server_manager->run();
```

3. `routes.h` was added.

`setupDynamicRoutes(HttpServer& server)` registers:

- `/` for the home page;
- `/query-builder` for the Query Builder UI;
- `/table` for table browsing;
- `/table/{table}/{id}` for record viewing;
- `/schema-manager` for XML schema management;
- `/static/{filename}` for CSS/JS;
- `/api/dynamic...` for dynamic requests;
- `/api/dynamic/meta...` for metadata and autocomplete;
- `/api/dynamic/schema.xml` and `/api/dynamic/schema/apply` for schema export/import.

4. `handlers.h` was added.

It defines these handlers:

- `ApiDescriptionHandler` - serves `home.html`;
- `QueryBuilderInterfaceHandler` - serves `query_builder.html`;
- `TableInterfaceHandler` - serves `table.html` for table browsing;
- `RowViewInterfaceHandler` - serves `row_view.html` for record editing;
- `SchemaManagerPageHandler` - serves `schema_manager.html`;
- `QueryBuilderHandler` - accepts JSON/query DSL and calls ORM `QueryBuilder`;
- `SchemaExportHandler` - exports an XML schema from the demo database;
- `SchemaApplyHandler` - applies an XML schema to the demo database;
- `QueryMetadataHandler` - returns tables, fields and autocomplete data for the UI.

5. ORM `QueryBuilder` was integrated.

`QueryBuilderHandler` passes the incoming request to `QueryBuilder::parseRequest(...)` and then calls `exec()`. `QueryBuilder` validates table and field names, builds SQL and executes it through `DatabaseInterface`.

6. The demo SQLite database was added.

`demo_database.h` implements:

- `ensureDemoDatabase()` - creates the SQLite file, tables and data;
- `demoDatabaseConfig()` - returns `DatabaseConfig` for SQLite.

This keeps the example runnable without external PostgreSQL/MySQL services and without manual database preparation.

7. Portable path resolution was added.

`example_paths.h` reads the example directory path from the CMake definition:

```cpp
DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR
```

This removed absolute local paths from `routes.h` and `handlers.h`.

8. The build was configured in `CMakeLists.txt`.

It defines two targets:

- `dynamic_web_query_builder_server` - the main application;
- `dynamic_web_query_builder_schema_roundtrip_test` - the XML schema generation round-trip test.

The `dynamic_web_query_builder_server` target builds:

- Qornix Web server files;
- ORM core/database files;
- PostgreSQL/SQLite/MySQL drivers;
- the example itself.

The example also receives a compile definition with the source directory:

```cmake
target_compile_definitions(dynamic_web_query_builder_server PRIVATE
    DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    QORNIX_ENABLE_SQLITE=1
)
```

9. HTML templates were added.

`home.html` contains interactive forms for CRUD examples. `query_builder.html` provides a full query-building UI, table/field autocomplete and result display. `table.html` provides table browsing with pagination and autocomplete. `row_view.html` allows viewing and editing individual records with inline field editing. `schema_manager.html` provides the UI for XML schema export and import.

10. Demo requests were updated.

The initial examples on the home page use real tables from the demo SQLite database:

- GET: JOIN `orders`, `products`, `customers`;
- POST: create a product in `products`;
- PUT: update product `products.id=2`;
- PATCH: update order `orders.id=4`;
- DELETE: delete order `orders.id=6`.

11. A local `.gitignore` was added.

It excludes the generated SQLite database, XML schemas and sidecar files:

```text
dynamic_web_query_builder_demo.sqlite3
dynamic_web_query_builder_demo.sqlite3-*
dynamic_web_query_builder_demo.schema.xml
dynamic_web_query_builder_uploaded.schema.xml
/perf_test/report.json
```

## Verification

Build the target:

```bash
cmake --build build --target dynamic_web_query_builder_server
```

Check metadata after startup:

```bash
curl http://localhost:8008/api/dynamic/meta/tables
curl http://localhost:8008/api/dynamic/meta/products/fields
```

Check a dynamic query:

```bash
curl -X POST http://localhost:8008/api/dynamic/ \
  -H "Content-Type: application/json" \
  -d '{"method":"GET","table":"products","query":{"fields":["id","name","price","stock"],"limit":10}}'
```

Check XML schema export:

```bash
curl http://localhost:8008/api/dynamic/schema.xml
```

The XML schema can also be generated from `/schema-manager`. On that page, you can open an XML file from disk and apply it to the current demo SQLite database.

After the request, this file is created:

```text
example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.schema.xml
```

Run the schema generation round-trip test without starting the HTTP server:

```bash
cmake --build build --target dynamic_web_query_builder_schema_roundtrip_test
./build/example/dynamic_web_query_builder_server/dynamic_web_query_builder_schema_roundtrip_test
```

The test runs the `SQLite DB -> XML -> SchemaDefinition -> DB apply` scenario and checks that the schema contains `categories`, `products`, `customers`, `orders`, key fields, indexes, foreign keys, a CHECK constraint, a view and a trigger.

Check XML schema apply through HTTP:

```bash
curl -X POST http://localhost:8008/api/dynamic/schema/apply \
  -H "Content-Type: application/xml" \
  --data-binary @example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.schema.xml
```

The XML export driver support matrix is documented in:

```text
doc/xml_schema_export_driver_support.md
```

## Performance Tests

The `perf_test/` directory contains a Python performance test suite for checking API performance.

The suite has six phases:

1. **simple_reads** - simple GET requests by table name and by ID, using seven endpoint patterns.
2. **complex_queries** - JOINs, filters, sorting and pagination, using four query patterns.
3. **metadata_endpoints** - tables, fields, autocomplete and `schema.xml`, using nine endpoint patterns.
4. **write_operations** - POST create, PUT update, PATCH partial update and DELETE. The test creates and cleans up test data.
5. **concurrent_load** - concurrency levels 1, 5, 10, 20, 50 and 100.
6. **stress_test** - sustained high load across six endpoints.

Run:

```bash
cd perf_test
./run_perf_test.sh
```

The script builds the target, starts the server, runs the tests and stops the server automatically. Results are written to `perf_test/report.json`.

## License

This project is dual-licensed under:

1. **GNU General Public License v3.0** (Open Source)
  - You may use, modify, and distribute this software freely under the terms of the GPL v3.0, provided that you also distribute your modifications under the same license.
  - For the full text of the GPL, visit: [GPLv3](https://www.gnu.org/licenses/gpl-3.0.txt).

2. **Commercial License**
  - If you want to use this software in a proprietary or closed-source project, you need to acquire a commercial license.
  - For commercial licensing inquiries, please contact us via [GitHub](https://github.com/hairetdin).

See the [LICENSE](../../LICENSE.txt) file for more details.
