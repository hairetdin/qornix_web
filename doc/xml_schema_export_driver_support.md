# XML schema export support by driver

This document records the current state of XML schema export support by database driver.

## SQLite

Status: verified by an automated round-trip test.

Supported:

- tables;
- columns;
- primary keys;
- foreign keys;
- indexes;
- CHECK constraints;
- views;
- triggers;
- sequences where supported by the database layer.

Test:

```bash
cmake --build build --target dynamic_web_query_builder_schema_roundtrip_test
./build/example/dynamic_web_query_builder_server/dynamic_web_query_builder_schema_roundtrip_test
```

The test creates a demo SQLite database, temporarily adds fixture objects for CHECK/view/trigger, generates XML, loads the XML back and verifies the key schema elements.

## PostgreSQL

Status: code-level verification completed; a live PostgreSQL instance was not used in the current environment.

Expected support:

- tables;
- columns;
- primary keys;
- foreign keys;
- indexes;
- sequences;
- views;
- materialized views;
- triggers;
- database functions;
- database procedures;
- CHECK constraints.

Completed fixes:

- XML export now serializes PostgreSQL-specific objects from the `db*` collections.
- Multiline definitions for views, materialized views, functions and triggers are normalized before tab-delimited parsing.
- Database function name reading was fixed: the code uses the `name` alias actually returned by the SQL query.

Limitations:

- A real PostgreSQL integration test is still needed.
- Function/procedure parameters are not serialized as structured elements yet because the corresponding database routine structures in `app_struct.h` do not yet contain a parameter list.
- Driver-specific types may lose part of their original detail when mapped to application types.

## MySQL

Status: code-level verification completed; a live MySQL instance was not used in the current environment.

Expected support:

- tables;
- columns;
- primary keys;
- foreign keys;
- indexes;
- views;
- triggers;
- database functions;
- database procedures;
- CHECK constraints for MySQL 8.0.16+.

Completed fixes:

- Triggers are now stored in `dbTriggers_` so they are included in XML export.
- Database functions are now stored in `dbDatabaseFunctions_` so they are included in XML export.
- Multiline definitions for views, triggers, procedures and functions are normalized before tab-delimited parsing.

Limitations:

- A real MySQL integration test is still needed.
- CHECK constraints depend on the MySQL version.
- Stored procedure/function parameters are not serialized as structured elements yet.
- MySQL sequences are not declared as a supported object by the current implementation.

## General limitations

- XML export preserves the database structure, but not table data.
- The XSD describes the core XML format, but full runtime XSD validation is not integrated yet.
- Comparing two XML schemas is outside the current scope.
- PostgreSQL/MySQL require separate integration-test infrastructure.
