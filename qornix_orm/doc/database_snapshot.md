# DatabaseSnapshot and database introspection

`DatabaseSnapshot` is the technical result of database introspection in `qornix_orm`.

It is not the object that should be compared directly with user XML. The intended schema-driven pipeline is:

```text
user XML
  -> validate against qornix_orm/schema/schema_app.xsd
  -> normalize
  -> desired SchemaDocument

database
  -> DatabaseIntrospector
  -> DatabaseSnapshot
  -> current SchemaDocument

desired SchemaDocument
  -> semantic diff
  -> current SchemaDocument
```

Sprint 27 introduces the introspection layer. Sprint 28 should convert `DatabaseSnapshot` into the canonical `SchemaDocument current` model.

## Main types

```cpp
class DatabaseSnapshot;
class DatabaseIntrospector;
```

Important snapshot entities:

```text
DbTable
DbColumn
DbPrimaryKey
DbForeignKey
DbIndex
DbConstraint
DbView
DbTrigger
```

## Basic usage

```cpp
#include "database_snapshot.h"
#include "database_interface.h"

int main() {
    auto db = DatabaseInterface::init("app.sqlite", "sqlite");

    auto result = DatabaseIntrospector::introspect(*db);
    if (!result.ok()) {
        for (const auto& error : result.errors) {
            std::cerr << error.code << ": " << error.message << std::endl;
        }
        return 1;
    }

    const auto& snapshot = result.snapshot;
    std::cout << snapshot.toDebugString() << std::endl;
}
```

## SQLite support

SQLite introspection uses:

```text
sqlite_master
PRAGMA table_info(...)
PRAGMA foreign_key_list(...)
PRAGMA index_list(...)
PRAGMA index_info(...)
```

The snapshot can detect:

- user tables;
- columns;
- primary keys;
- foreign keys;
- explicit indexes;
- views;
- triggers;
- raw SQL definitions where SQLite exposes them.

SQLite internal autoindexes such as `sqlite_autoindex_*` are skipped because they are implementation details for primary/unique constraints, not stable user-facing indexes.

## PostgreSQL and MySQL status

Sprint 27 adds a generic fallback for non-SQLite drivers through the existing `DatabaseInterface` methods:

```text
getTableNames()
getColumnNames(table)
```

This fallback captures table and column names only and returns a warning.

Full PostgreSQL/MySQL typed introspection should be implemented later with driver-specific queries and capability metadata.

## Why DatabaseSnapshot is not the diff input

User XML may be valid but not identical to XML generated from the database. For example, it may differ by:

- element order;
- optional attributes;
- type aliases;
- default value formatting;
- driver-specific metadata;
- comments or user metadata.

Therefore the diff layer should compare:

```text
SchemaDocument desired
SchemaDocument current
```

not:

```text
raw XML vs raw DatabaseSnapshot
```

`DatabaseSnapshot` is a necessary technical layer, but not the final semantic representation.

## Test coverage

Sprint 27 adds:

```text
qornix_orm/tests/database_snapshot_test.cpp
```

The test creates an in-memory SQLite database with:

- two tables;
- primary keys;
- a foreign key;
- an explicit index;
- a view;
- a trigger.

Then it verifies that `DatabaseIntrospector::introspect(...)` returns the expected typed snapshot.
