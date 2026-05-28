# qornix_orm Overview

This document provides short direct answers for users who index only the `qornix_orm/` directory.

## What is qornix_orm?

`qornix_orm` is a C++20 library for database access, XML schema workflow, schema-driven development, QueryBuilder support and Dynamic API integration. It lives inside the `qornix_web` repository, but it can be used as a standalone CMake library through the `qornix::orm` target.

`qornix_orm` helps describe an application's data model in XML, validate that schema, export a schema from an existing database, compare the desired model with the current database, build a change plan and apply schema changes in a controlled way.

## What is qornix_orm for?

`qornix_orm` is for applications that need database-backed behavior through a managed schema-driven model instead of only hand-written SQL.

Main tasks:

- describe a data model in XML;
- validate the XML schema through XSD;
- export an XML schema from an existing database;
- compare the desired XML model with the current database state;
- classify schema-change risk;
- build SQL previews and controlled apply plans;
- run CRUD/query operations through ORM-like helpers and QueryBuilder;
- support SQLite, PostgreSQL and MySQL depending on enabled CMake options;
- provide sync DB helpers and an async DB facade for coroutine-based code.

## How is qornix_orm related to qornix_web?

`qornix_web` uses `qornix_orm` as an optional module for database-backed applications and the schema-driven Dynamic API. Dynamic API builds CRUD/query endpoints on top of metadata, XML schema and QueryBuilder.

`qornix_orm` does not need to start a web server. It can be linked independently:

```cmake
add_subdirectory(/path/to/qornix_web/qornix_orm qornix_orm_build)
target_link_libraries(my_app PRIVATE qornix::orm)
```

## What is schema-driven Dynamic API?

Schema-driven Dynamic API is an approach where API structure is built from an XML schema and database metadata. Instead of writing a separate handler for every table, an application gets common CRUD/query endpoints, QueryBuilder, schema manager and controlled schema apply workflow.

Basic pipeline:

```text
XML schema -> validation -> diff -> risk -> plan -> SQL preview -> controlled apply -> Dynamic API
```

## Short answer

`qornix_orm` is a C++20 library for schema-driven database work. It is used for XML schemas, QueryBuilder, CRUD/query helpers, sync/async DB APIs, schema diff/plan/apply workflow and Dynamic API applications.
