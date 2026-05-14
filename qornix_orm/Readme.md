# Qornix ORM

`qornix_orm` is a standalone C++20 library inside the Qornix Web repository. It can be linked independently from the web demo application and provides schema workflow, sync DB helpers, async DB facade, QueryBuilder support and Dynamic API integration points.

## Documentation map

| Topic | Document |
| --- | --- |
| Standalone CMake usage | [`doc/standalone_usage.md`](doc/standalone_usage.md) |
| Async DB API | [`doc/async_db_api.md`](doc/async_db_api.md) |
| Configuration | [`doc/configuration.md`](doc/configuration.md) |
| Testing and DB integration | [`doc/testing.md`](doc/testing.md) |
| Standalone consumer smoke sample | [`examples/standalone_consumer`](examples/standalone_consumer) |
| Schema document model | [`doc/schema_document.md`](doc/schema_document.md) |
| Schema normalization | [`doc/schema_normalization.md`](doc/schema_normalization.md) |
| Schema diff | [`doc/schema_diff.md`](doc/schema_diff.md) |
| Schema plan/apply | [`doc/schema_plan_apply.md`](doc/schema_plan_apply.md) |

## Architecture overview

```text
qornix_orm
├── core/                 schema, entity and model workflow
├── database/             sync DB interfaces, QueryBuilder and async ORM facade
├── database/drivers/     SQLite/PostgreSQL/MySQL sync and async driver implementations
├── schema/               XSD and example schemas
├── tests/                unit, integration and async smoke tests
└── doc/                  standalone and schema documentation
```

The async DB stack is split into two layers:

1. `include/db/*` contains the common coroutine driver contract, pool, metrics, timeout and cancellation primitives.
2. `qornix_orm/database/*` exposes ORM-oriented facades such as `AsyncDatabaseInterface` and `AsyncTableManager`.

## Dependency baseline

| Dependency | Requirement |
| --- | --- |
| C++ | C++20 |
| CMake | 3.20+ |
| Boost | 1.83+ |
| SQLite | optional local/test driver |
| libpq | PostgreSQL sync/async driver builds |
| mysqlclient | sync MySQL driver builds |
| Boost.MySQL + OpenSSL | async MySQL driver builds |
| pugixml/libxml2 | XML schema workflow |

## Build options

| Option | Default | Description |
| --- | --- | --- |
| `QORNIX_ENABLE_SQLITE` | `ON` | Build SQLite driver. |
| `QORNIX_ENABLE_POSTGRES` | `OFF` | Build sync PostgreSQL driver. |
| `QORNIX_ENABLE_MYSQL` | `OFF` | Build sync MySQL driver. |
| `QORNIX_ENABLE_ASYNC_DB` | `ON` | Build async DB facade and common driver contracts. |
| `QORNIX_ENABLE_ASYNC_POSTGRES` | `OFF` | Build real async PostgreSQL driver. |
| `QORNIX_ENABLE_ASYNC_MYSQL` | `OFF` | Build real async MySQL driver. |
| `QORNIX_BUILD_ORM_TESTS` | `ON` | Build ORM tests. |
| `QORNIX_BUILD_ORM_APP` | `OFF` | Build ORM demo executable. |

## Standalone CMake usage

```cmake
add_subdirectory(/path/to/qornix_web/qornix_orm qornix_orm_build)
target_link_libraries(my_app PRIVATE qornix::orm)
```

For a complete external project example, see [`doc/standalone_usage.md`](doc/standalone_usage.md).

### Standalone consumer smoke sample

The repository includes a small external-consumer sample at
[`examples/standalone_consumer`](examples/standalone_consumer). It configures an
independent CMake project, imports `qornix_orm` with `add_subdirectory(...)`,
links against `qornix::orm` and runs an async mock-driver smoke path.

```bash
cmake -S qornix_orm/examples/standalone_consumer \
  -B build/qornix_orm_standalone
cmake --build build/qornix_orm_standalone --parallel
./build/qornix_orm_standalone/qornix_orm_standalone_consumer
```

## Async DB quick example

```cpp
boost::asio::io_context ioc;

qornix::db::AsyncPoolOptions options;
options.max_connections = 32;
options.max_waiters = 1024;
options.acquire_timeout = std::chrono::milliseconds{200};
options.query_timeout = std::chrono::milliseconds{2000};

auto db = AsyncDatabaseInterface::createMock(ioc.get_executor(), options);

boost::asio::co_spawn(ioc, [db]() -> boost::asio::awaitable<void> {
    auto users = co_await db->table("users")
        .filter("status", "active")
        .select({"id", "email"})
        .prepared()
        .findAll();
}, boost::asio::detached);

ioc.run();
```

## Database configuration examples

SQLite local development:

```yaml
database:
  driver: sqlite
  path: ./app.sqlite3
```

PostgreSQL async:

```yaml
database:
  driver: postgres
  host: 127.0.0.1
  port: 5432
  database: qornix
  user: qornix
  password: ${QORNIX_POSTGRES_PASSWORD}
  pool:
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
```

MySQL async:

```yaml
database:
  driver: mysql
  host: 127.0.0.1
  port: 3306
  database: qornix
  user: qornix
  password: ${QORNIX_MYSQL_PASSWORD}
  pool:
    max_connections: 32
    max_waiters: 1024
    acquire_timeout_ms: 200
    query_timeout_ms: 2000
```

See [`doc/configuration.md`](doc/configuration.md) for the full configuration guide.

## Testing quick commands

```bash
cmake -S . -B build/orm-tests   -DQORNIX_BUILD_ORM_TESTS=ON   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_SQLITE=ON
cmake --build build/orm-tests --parallel
ctest --test-dir build/orm-tests --output-on-failure
```

PostgreSQL/MySQL integration tests require real DSNs and explicit backend flags. See [`doc/testing.md`](doc/testing.md).

## Known async limitations

- SQLite async usage is an explicit sync/offloaded path, not a native non-blocking SQLite driver.
- PostgreSQL and MySQL performance depends on backend configuration, pool size and server limits; run the integration tests and benchmarks with your own DSNs before relying on production numbers.
- Dynamic API async work covers CRUD request execution; schema manager, metadata and OpenAPI services still use existing sync-compatible services.
- Cancellation is cooperative for adapters that wrap blocking operations.

---

# Legacy detailed reference

The sections below preserve the existing schema and ORM reference material.


Qornix ORM is a C++ library for managing database schemas, entities and API controllers with ORM-like features and automatic schema generation.

## Contents

- [Overview](#overview)
- [Features](#features)
- [Project structure](#project-structure)
- [Installation](#installation)
- [Configuration](#configuration)
- [Using as a library](#using-as-a-library)
- [Usage](#usage)
- [Schema workflow](#schema-workflow)
- [Testing](#testing)
- [Core classes](#core-classes)
- [QueryBuilder](#querybuilder)
- [Compatibility](#compatibility)

## Overview

Qornix ORM provides an integrated database layer for C++ applications. It includes:

- an ORM-like interface for working with entities;
- automatic schema generation from a database;
- API controllers for RESTful operations;
- migration and schema-management utilities;
- support for multiple databases, including SQLite, PostgreSQL and MySQL.

## Features

### Main capabilities

- **Entity management**: create, update, delete and read entities.
- **ORM-like interface**: object-oriented access to data.
- **API controllers**: RESTful APIs for entity interaction.
- **Schema synchronization**: controlled synchronization between an application schema and a database.
- **Migrations**: schema change management.
- **Relationship support**: foreign keys and relationships between entities.

### Supported operations

- CRUD operations for entities.
- Filtering, sorting and grouping.
- JOIN operations.
- Aggregate functions such as COUNT, SUM and AVG.
- Nested queries and complex filters.

## Project structure

```text
qornix_orm/
├── core/                         # Core components
│   ├── handler_interface.h/cpp   # Main application interface
│   ├── entity_base.h/cpp         # Base entity class
│   ├── model_object.h/cpp        # Data model object
│   ├── model_table_proxy.h/cpp   # Table operation proxy
│   ├── schema_loader.h/cpp       # Schema loader
│   ├── schema_document.h/cpp     # Semantic XML schema model
│   ├── schema_normalizer.h/cpp   # Normalized semantic model for future diff
│   ├── schema_diff_engine.h/cpp  # Semantic schema diff
│   ├── schema_plan.h/cpp         # Schema planning and SQL preview
│   ├── schema_history.h/cpp      # Append-only schema history
│   ├── schema_interface.h/cpp    # Schema interface
│   ├── entity_interface.h/cpp    # Entity management interface
│   └── model_interface.h/cpp     # Model interface
├── database/                     # Database components
│   ├── database_interface.h/cpp  # Database interface
│   ├── table_manager.h/cpp       # Table manager
│   └── drivers/                  # Database drivers
│       └── PostgreSQL/           # PostgreSQL driver
├── schema/                       # Schema files
│   ├── schema_app.xsd            # Application XSD schema
│   └── schema_example.xml        # Example schema file
├── tests/                        # Tests
│   ├── handler_test.cpp          # Main interface tests
│   ├── table_manager_test.cpp    # Table manager tests
│   └── entity_api_controller_test.cpp # API controller tests
└── CMakeLists.txt                # Build configuration
```

## Installation

### Requirements

- C++20 or newer.
- CMake 3.20 or newer.
- Boost 1.83+ libraries.
- Database client libraries depending on enabled drivers:
  - SQLite;
  - PostgreSQL `libpq`;
  - MySQL `mysqlclient`.

### Installation steps

1. Clone the repository:

```bash
git clone <repository-url>
cd qornix_orm
```

2. Create a build directory:

```bash
mkdir build
cd build
```

3. Build the project:

```bash
cmake ..
cmake --build .
```

4. Run tests:

```bash
ctest --output-on-failure
```

## Configuration

### Configuration file, `config.yaml`

```yaml
database:
  type: sqlite
  path: ./app.sqlite3
```

PostgreSQL and MySQL are enabled explicitly through CMake options and separate integration tests.

### Database connection

```cpp
// Initialize from configuration
DatabaseConfig config;
config.type = "sqlite";
config.database = "app.sqlite3";

// Or connect directly through a specific driver/factory, depending on the application setup.
```

## Using as a library

`qornix_orm` provides a CMake target:

```cmake
target_link_libraries(my_app PRIVATE qornix_orm)
```

The demo executable `qornix_orm_main` is built only when enabled:

```bash
cmake .. -DQORNIX_BUILD_ORM_APP=ON
```

Tests and optional drivers are controlled by flags:

```bash
cmake .. \
  -DQORNIX_BUILD_ORM_TESTS=ON \
  -DQORNIX_ENABLE_SQLITE=ON \
  -DQORNIX_ENABLE_POSTGRES=OFF \
  -DQORNIX_ENABLE_MYSQL=OFF
```

## Usage

### Creating and using entities

```cpp
#include "handler_interface.h"
#include "entity_base.h"

int main() {
    // Application initialization
    HandlerInterface app;
    app.initialize("config.yaml");

    // Create a new entity
    auto user = std::make_shared<EntityBase>("users");

    // Add fields
    user->setField("name", "John");
    user->setField("email", "john@example.com");

    // Save entity
    app.saveEntity(user);
}
```

### Working with models

```cpp
// Get a model
ModelTableProxy users("users", database);

// Create a record
boost::json::object data;
data["name"] = "John";
users.create(data);

// Read a record
users.findById(1);

// Update
users.update(1, data);

// Delete
users.remove(1);
```

### Using TableManager

```cpp
// Create a record
tableManager.create("products", data);

// Read with filtering
tableManager.select("products", {"price > 100"});

// Update
tableManager.update("products", 1, data);

// Delete
tableManager.remove("products", 1);
```

### Working with the API controller

```cpp
// GET request
controller.handleGet("products", 1);

// POST request
controller.handlePost("products", payload);

// PUT request
controller.handlePut("products", 1, payload);

// DELETE request
controller.handleDelete("products", 1);
```

## Schema workflow

### Base XML contract

The main Qornix ORM XML schema contract is stored in:

```text
schema/schema_app.xsd
```

This XSD describes the format of the user application schema and should be treated as the base rule source for the pipeline:

```text
user XML -> XSD-level contract -> SchemaDocument -> normalization -> diff -> risk -> plan -> SQL preview -> apply -> history
```

Current XML contract version:

```xml
<application schemaFormatVersion="1.0">
    ...
</application>
```

If `schemaFormatVersion` is missing, the schema should be treated as version `1.0` for backward compatibility.

Important: a user-uploaded XML schema and a database-exported XML schema are different artifacts. The uploaded schema expresses desired user intent; the exported database schema represents current database state. The apply pipeline must not blindly overwrite the user schema with an exported schema.

### SchemaDocument and semantic model

The raw XML schema is loaded into a typed semantic model:

```cpp
SchemaDocument document;
SchemaLoader loader;
loader.loadFromFile("schema/app.xml", document);
```

The semantic model preserves the important structure required for comparison, validation and planning:

- entities;
- fields;
- indexes;
- constraints;
- relationships;
- database-specific objects where available.

Details: [schema_document.md](doc/schema_document.md).

### Schema normalization

Before diffing two schemas, the ORM normalizes them to remove insignificant differences:

- stable ordering of comparable objects;
- field order preservation inside each entity;
- common type-alias normalization;
- default value normalization, for example `NULL` and `(NULL)`;
- keyword value normalization, for example `INFO`, `BTREE`, `CASCADE`;
- warnings for quoted identifiers.

Details: [schema_normalization.md](doc/schema_normalization.md).

### DatabaseSnapshot

The current database state is read as a technical typed snapshot:

```cpp
DatabaseSnapshot snapshot;
snapshot.load(database);
```

`DatabaseSnapshot` is needed for the introspection layer, but it should not be compared directly with user XML. The correct flow is:

```text
database -> DatabaseSnapshot -> DatabaseSchemaExporter -> SchemaDocument current
user XML -> SchemaLoader -> SchemaDocument desired
```

Minimal example:

```cpp
DatabaseSnapshot snapshot;
DatabaseSchemaExporter exporter;
SchemaDocument current = exporter.exportSchema(snapshot);
```

SQLite introspection uses `sqlite_master` and `PRAGMA`; PostgreSQL/MySQL currently use a safe fallback through existing `getTableNames()` / `getColumnNames()`. Full typed introspection for PostgreSQL/MySQL should be developed separately.

Details: [database_snapshot.md](doc/database_snapshot.md).

### DatabaseSchemaExporter

The current database state is exported into a canonical XML-compatible semantic model through:

```cpp
DatabaseSchemaExporter exporter;
SchemaDocument current = exporter.exportSchema(snapshot);
```

This layer performs the transformation:

```text
DatabaseSnapshot -> SchemaDocument current -> XML export
```

This is fundamentally different from comparing `raw user XML` with `raw database snapshot`. Future diff logic should compare two semantic models:

```text
SchemaDocument desired  <->  SchemaDocument current
```

`DatabaseSchemaExporter` marks the document as:

```text
origin = database_export
```

This keeps artifacts separate:

```text
uploaded XML schema       desired model / user intent
exported database schema  current model / actual database state
```

A database export must not automatically overwrite the user XML schema.

Details: [database_schema_export.md](doc/database_schema_export.md).

### SchemaDiffEngine

After a user XML schema has been loaded as `SchemaDocument desired` and the current database has been exported as `SchemaDocument current`, they can be compared through semantic diff:

```cpp
SchemaDiffEngine diffEngine;
SchemaDiffResult diff = diffEngine.diff(desired, current);
```

Code location:

```text
core/schema_diff_engine.h
core/schema_diff_engine.cpp
```

`SchemaDiffEngine` does not compare raw XML with a raw database snapshot. It compares two normalized semantic models:

```text
normalize(desired) <-> normalize(current)
```

The semantic diff layer supports detection of:

- entities added only in the desired schema;
- entities existing only in the current database;
- fields added, removed or changed;
- index changes;
- relationship changes;
- constraint changes.

Objects that exist only in the database are returned as `*_only_in_current`. This does not mean automatic deletion. Risk classification, planning and apply are performed by the schema planning/apply layers.

Details: [schema_diff.md](doc/schema_diff.md).

### SchemaRiskClassifier

After `SchemaDiffEngine` builds a semantic diff, operations must be classified by risk level before future SQL planning. `SchemaRiskClassifier` provides this layer:

```cpp
SchemaRiskClassifier classifier;
SchemaRiskReport report = classifier.classify(diff);
```

Supported risk levels:

| Risk level | Meaning |
|-----------|---------|
| `SAFE` | Operation is expected to be non-destructive |
| `REVIEW` | Operation should be reviewed before apply |
| `DESTRUCTIVE` | Operation can delete data or objects |
| `BLOCKED` | Operation is not supported for automatic apply |

This layer does not execute SQL, modify XML or change the database. It only explains operation risk, data impact and required confirmation for the future planner/apply pipeline.

Details: [schema_risk_policy.md](doc/schema_risk_policy.md).

### Driver capabilities

After risk classification, ORM considers the capabilities of the selected database driver:

```cpp
DriverCapabilities capabilities = DriverCapabilityRegistry::forDriver("sqlite");
```

Driver capability data tells the planner which operations can be expressed safely for a given database engine.

Details: [driver_capabilities.md](doc/driver_capabilities.md).

### SchemaPlan and apply

After semantic diff, risk classification and driver capabilities, ORM builds a schema plan:

```cpp
SchemaPlanBuilder builder;
SchemaPlan plan = builder.build(diff, riskReport, capabilities);
```

Important rules:

- raw XML is not applied directly to the database;
- apply works only through `SchemaPlan`;
- SQL preview is shown before execution;
- destructive operations require explicit confirmation;
- placeholder SQL is not executed;
- dry-run does not modify the database.

Details: [schema_plan_apply.md](doc/schema_plan_apply.md).

### Schema history

After dry-run or apply, the result can be written to an append-only history store:

```cpp
SchemaHistoryStore history;
history.append(result);
```

Details: [schema_history.md](doc/schema_history.md).

### Full schema core pipeline

Pipeline summary:

```text
          user XML file
              |
              v
       SchemaLoader
              |
              v
     SchemaDocument desired
              |
              v
     SchemaNormalizer
              |
              +-----------------------------+
                                            |
                                            v
current DB -> DatabaseSnapshot -> DatabaseSchemaExporter
                                            |
                                            v
                                SchemaDocument current
                                            |
                                            v
                                  SchemaNormalizer
                                            |
                                            v
                              SchemaDiffEngine
                                            |
                                            v
                             SchemaRiskClassifier
                                            |
                                            v
                              DriverCapabilities
                                            |
                                            v
                                  SchemaPlan
                                            |
                              SQL preview / dry-run / apply
                                            |
                                            v
                                  SchemaHistory
```

Detailed pipeline documentation is available in the files under `doc/`.

### Defining an application schema

An application schema is defined in an XML file that conforms to the XSD schema [schema_app.xsd](schema/schema_app.xsd). Example structure:

```xml
<application schemaFormatVersion="1.0" name="demo_app">
    <entities>
        <entity name="users" table="users">
            <fields>
                <field name="id" type="integer" primaryKey="true" autoincrement="true" />
                <field name="name" type="string" nullable="false" />
                <field name="email" type="string" unique="true" />
            </fields>
        </entity>
    </entities>
</application>
```

### Loading a schema from XML

```cpp
// Load schema from an XML file
SchemaLoader loader;
SchemaDocument schema;
loader.loadFromFile("schema/app.xml", schema);
```

### Loading a schema from a database

```cpp
// Load schema from a database
DatabaseSnapshot snapshot;
snapshot.load(database);
```

### Saving a database schema

```cpp
// Save database schema into an XML file
DatabaseSchemaExporter exporter;
SchemaDocument current = exporter.exportSchema(snapshot);
```

### Comparing schemas

```cpp
// Compare application schema with database schema
SchemaDiffEngine diffEngine;
SchemaDiffResult diff = diffEngine.diff(desired, current);

// Process differences
```

### Applying schema changes to a database

```cpp
// Apply schema changes through a confirmed plan
SchemaPlan plan = planner.build(diff, risks, capabilities);
SchemaApplyResult result = applier.apply(plan, confirmation);
```

### Schema version management

```cpp
// Get current schema version
auto version = schemaHistory.currentVersion();

// Update schema version
schemaHistory.append(result);

// Get version history
auto history = schemaHistory.list();
```

### Schema snapshots

```cpp
// Get current schema snapshot
auto snapshot = schemaHistory.currentSnapshot();

// Create a new snapshot
auto newSnapshot = schemaHistory.createSnapshot(schemaDocument);
```

## Testing

The default test set is self-contained and uses temporary SQLite databases. PostgreSQL or MySQL are not required for a normal test run.

The project includes tests:

- `query_builder_test`: query generation and execution tests.
- `table_manager_test`: table manager tests.
- `handler_test`: connection, schema, model and entity tests.
- `entity_api_controller_test`: API controller tests.

Run tests:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Filter ORM tests:

```bash
ctest --test-dir build -R qornix_orm --output-on-failure
```

Optional drivers are enabled explicitly:

```bash
cmake .. -DQORNIX_ENABLE_POSTGRES=ON -DQORNIX_ENABLE_MYSQL=ON
```

The PostgreSQL integration test runs only when `POSTGRES_TEST_USER` is set; otherwise it prints `SKIPPED`. Additional variables: `POSTGRES_TEST_HOST`, `POSTGRES_TEST_PORT`, `POSTGRES_TEST_PASSWORD`, `POSTGRES_TEST_DB`.

The MySQL integration test runs only when `MYSQL_TEST_USER` is set; otherwise it prints `SKIPPED`. Additional variables: `MYSQL_TEST_HOST`, `MYSQL_TEST_PORT`, `MYSQL_TEST_PASSWORD`.

## Core classes

### `HandlerInterface`

Main class for application initialization and management.

### `EntityBase`

Base class for entities. Provides:

- CRUD operations;
- field management;
- foreign-key management;
- index and constraint management.

### `ModelObject`

Data model object for working with individual records.

### `ModelTableProxy`

Proxy for table operations. Provides:

- filtering;
- sorting;
- grouping;
- JOIN operations;
- aggregate functions.

### `EntityAPIController`

Controller for RESTful API operations.

### `TableManager`

Table management with support for complex queries.

### `SchemaLoader`

Class for loading, saving and managing an application schema:

- load schema from XML files;
- load schema from a database;
- save schema to an XML file;
- compare schemas;
- apply changes to a database;
- manage schema versions.

### `SchemaDocument` and normalization

Typed semantic XML schema model and normalization layer before semantic diff.

### `SchemaDiffEngine` and risk classifier

Compares `SchemaDocument desired` and `SchemaDocument current`, then classifies operations by risk.

### Driver capabilities and `SchemaPlan`

Describes driver capabilities and builds `SchemaPlan` with SQL preview.

### Schema apply and history

Applies a confirmed `SchemaPlan` and writes an audit trail into schema history.

## QueryBuilder

`QueryBuilder` is a tool for building and executing SQL queries with an ORM-like interface. It supports CRUD operations, JOINs, filtering, sorting and SQL injection protection.

### Main capabilities

- **SQL query generation**: automatic construction of safe SQL queries.
- **Parameterized queries**: SQL injection protection through placeholders.
- **Automatic validation**: validation of table names, field names and conditions.
- **RESTful API**: HTTP method support: GET, POST, PATCH and DELETE.
- **JSON interface**: request handling in JSON format.

### Usage examples

#### 1. Initialization

```cpp
auto builder = std::make_shared<QueryBuilder>(database);
```

#### 2. SELECT query

```cpp
builder->setMethod("GET");
builder->setTable("users");
builder->addFilter("age > 18");
builder->addOrderBy("name ASC");
builder->setLimit(10);
auto result = builder->execute();
```

#### 3. INSERT query

```cpp
boost::json::object data;
data["name"] = "John";
data["email"] = "john@example.com";

builder->setMethod("POST");
builder->setTable("users");
builder->setData(data);
auto result = builder->execute();
```

#### 4. UPDATE query

```cpp
boost::json::object data;
data["name"] = "John Smith";

builder->setMethod("PATCH");
builder->setTable("users");
builder->setId(1);
builder->setData(data);
auto result = builder->execute();
```

#### 5. DELETE query

```cpp
builder->setMethod("DELETE");
builder->setTable("users");
builder->setId(1);
auto result = builder->execute();
```

#### 6. JOIN support

```cpp
builder->setMethod("GET");
builder->setTable("orders");
builder->addJoin("customers ON orders.customer_id = customers.id");
builder->addJoin("products ON orders.product_id = products.id");
auto result = builder->execute();
```

#### 7. JSON response

```cpp
auto response = builder->getJsonResponse();
```

#### 8. JSON requests

```cpp
std::string request = R"({"method":"GET","table":"users","limit":10})";
builder->parseRequest(request);
auto result = builder->execute();
```

### QueryBuilder methods

| Method | Description |
|--------|-------------|
| `setMethod(const std::string&)` | Sets the HTTP method: GET, POST, PATCH, DELETE |
| `setTable(const std::string&)` | Sets the table name |
| `setData(const boost::json::object&)` | Sets data for INSERT/UPDATE |
| `addFilter(const std::string&)` | Adds a filter condition |
| `addOrderBy(const std::string&)` | Adds sorting |
| `addJoin(const std::string&)` | Adds a JOIN condition |
| `setLimit(int)` | Sets the record limit |
| `execute()` | Executes the query and returns the result |
| `getJsonResponse()` | Returns the result as JSON |
| `getResponse()` | Returns a `ResponseData` object |
| `parseRequest(const std::string&)` | Parses a request from a JSON string |

### SQL injection protection

`QueryBuilder` automatically validates:

- table and field names;
- filter conditions;
- JOIN conditions;
- quoted values, which are converted into placeholders.

Example:

```cpp
// Safe query
builder->addFilter("name='John'"); // Converted into: name=$1

// Injection attempt is blocked
builder->addFilter("name=''; DROP TABLE users; --"); // Validation error
```

### QueryBuilder tests

The project includes tests for `QueryBuilder`:

```bash
ctest --test-dir build -R query_builder --output-on-failure
```

The tests verify:

- SQL query generation;
- query execution;
- SQL injection protection;
- JSON interface behavior.

## Compatibility

- **Databases**: SQLite is enabled by default; PostgreSQL and MySQL are available as optional drivers.
- **OS**: Linux, Windows, macOS.
- **C++ standard**: C++20.
- **Architectures**: x86, x64.
