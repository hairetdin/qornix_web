# Semantic Schema Diff

`SchemaDiffEngine` compares two normalized `SchemaDocument` instances and returns a structured semantic diff.

This is the semantic diff layer of the ORM schema pipeline:

```text
user XML
  -> XSD validation
  -> SchemaDocument
  -> normalization
  -> desired SchemaDocument

database
  -> DatabaseSnapshot
  -> DatabaseSchemaExporter
  -> SchemaDocument
  -> normalization
  -> current SchemaDocument

desired SchemaDocument
  -> SchemaDiffEngine
  -> current SchemaDocument
```

The engine does **not** compare raw XML to raw database introspection rows. `DatabaseSnapshot` remains a technical introspection artifact. The comparison boundary is semantic:

```text
SchemaDocument desired
SchemaDocument current
```

## What it detects

The diff engine supports read-only detection of these changes:

```text
table added
table exists only in current database schema
column added
column exists only in current database schema
column changed
primary key changed
foreign key added
foreign key exists only in current database schema
foreign key changed
index added
index exists only in current database schema
index changed
view added
view exists only in current database schema
view changed
trigger added
trigger exists only in current database schema
trigger changed
sequence added
sequence exists only in current database schema
sequence changed
```

## What it intentionally does not do

`SchemaDiffEngine` is side-effect free.

It does not:

- modify uploaded XML;
- overwrite exported DB XML;
- modify `SchemaDocument` inputs;
- execute SQL;
- change the database;
- decide whether a change is safe or destructive;
- build a migration/apply plan.

Risk classification, planning and apply are handled by the risk policy, planner and applier layers.

## Minimal usage

```cpp
#include "core/schema_diff_engine.h"
#include "core/schema_document.h"
#include "core/schema_normalizer.h"

SchemaDocument desired = ...; // loaded from user XML
SchemaDocument current = ...; // exported from DatabaseSnapshot

SchemaDiffOptions options;
options.normalizeBeforeCompare = true;

SchemaDocumentDiff diff = SchemaDiffEngine::compare(desired, current, options);

if (diff.hasChanges()) {
    std::cout << diff.toText() << std::endl;
    std::cout << diff.toJsonString() << std::endl;
}
```

## Output formats

The diff can be rendered as text:

```cpp
std::string text = diff.toText();
```

or as machine-readable JSON:

```cpp
std::string json = diff.toJsonString();
```

The JSON output is intended for the later planner/API/UI layers.

## Current-only objects

Objects that exist in the database but not in the desired XML schema are reported explicitly as `*_only_in_current` operations.

Example:

```text
table_only_in_current
column_only_in_current
index_only_in_current
foreign_key_only_in_current
```

This is important: an object that exists only in the current database is **not** automatically considered something to drop. The risk policy classifies risk, and the planner decides whether such an operation can become part of a plan.

## Normalization

By default, `SchemaDiffEngine` normalizes both documents before comparing them:

```cpp
SchemaDiffOptions options;
options.normalizeBeforeCompare = true;
```

This prevents false differences caused by:

- element order;
- trivial type aliases;
- default value formatting;
- keyword casing;
- whitespace around identifiers.

Original user XML remains a separate artifact and is not overwritten.

## Test coverage

The diff engine adds:

```text
qornix_orm/tests/schema_diff_engine_test.cpp
```

The test covers:

- empty diff;
- table added;
- table only in current;
- column changed;
- column added;
- column only in current;
- index added;
- foreign key changed;
- text output;
- JSON output.
