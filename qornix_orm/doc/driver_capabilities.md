# DriverCapabilities

`DriverCapabilities` describes which schema operations are supported by a database driver.

This module belongs to the ORM schema core pipeline:

```text
schema_app.xsd
  -> XML validation
  -> SchemaDocument desired
  -> DatabaseSnapshot
  -> SchemaDocument current
  -> semantic diff
  -> risk classification
  -> driver capabilities
  -> schema plan
  -> SQL preview
  -> apply
```

Sprint 31 does **not** generate SQL and does **not** modify a database. It only answers the question:

```text
Can this driver support the operation required by this diff item?
```

## Why this layer is needed

The same semantic diff can require different technical operations depending on the database engine.

Examples:

- PostgreSQL can alter many column attributes directly.
- SQLite often requires a table rebuild for column/constraint changes.
- MySQL supports many `ALTER TABLE` operations but they may lock or rebuild tables depending on engine/version.

The planner must know these differences before creating a safe schema plan.

## Main API

```cpp
#include "core/driver_capabilities.h"

const auto capabilities = DriverCapabilities::sqlite();

if (capabilities.supports(SchemaCapabilityOperation::AddColumn)) {
    // Planner can consider ALTER TABLE ... ADD COLUMN ...
}

const auto capability = capabilities.capabilityFor(
    SchemaCapabilityOperation::AlterColumnType
);

if (!capability.supported && capability.requiresTableRebuild) {
    // Planner should produce a manual-review or table-rebuild operation.
}
```

## Driver profiles

Available profiles:

```cpp
DriverCapabilities::generic();
DriverCapabilities::sqlite();
DriverCapabilities::postgresql();
DriverCapabilities::mysql();
DriverCapabilities::fromDriverName("sqlite");
```

`generic` is conservative and does not allow automatic planning.

## Capability fields

Each operation has:

```text
supported
requiresTableRebuild
destructive
transactional
requiresLock
requiresManualReview
sqlPreviewHint
notes
```

These fields are inputs for the future `SchemaPlanner`.

## Operation mapping from diff

`DriverCapabilities::operationForDiffOperation(...)` maps a semantic diff operation to a technical capability operation.

Examples:

```text
TableAdded              -> create_table
TableOnlyInCurrent      -> drop_table
ColumnAdded             -> add_column
ColumnOnlyInCurrent     -> drop_column
ColumnChanged(type)     -> alter_column_type
ColumnChanged(nullable) -> alter_column_nullable
ColumnChanged(default)  -> alter_column_default
ForeignKeyAdded         -> add_foreign_key
ForeignKeyOnlyInCurrent -> drop_foreign_key
IndexAdded              -> add_index
IndexOnlyInCurrent      -> drop_index
ViewChanged             -> alter_view
TriggerChanged          -> alter_trigger
```

## Baseline driver matrix

| Operation | SQLite | PostgreSQL | MySQL |
|---|---:|---:|---:|
| create_table | supported | supported | supported |
| drop_table | supported, destructive | supported, destructive | supported, destructive |
| rename_table | supported | supported | supported |
| add_column | supported | supported | supported |
| drop_column | supported with version/constraint caveats, may rebuild | supported | supported |
| alter_column_type | table rebuild/manual review | supported/manual review | supported/manual review |
| alter_column_nullable | table rebuild/manual review | supported/manual review | supported/manual review |
| alter_column_default | table rebuild/manual review | supported | supported |
| add_primary_key | table rebuild/manual review | supported/manual review | supported/manual review |
| drop_primary_key | table rebuild/destructive | supported/destructive | supported/destructive |
| add_index | supported | supported | supported |
| drop_index | supported/destructive metadata | supported/destructive metadata | supported/destructive metadata |
| add_foreign_key | table rebuild/manual review | supported/manual review | supported/manual review |
| drop_foreign_key | table rebuild/destructive metadata | supported/destructive metadata | supported/destructive metadata |
| create_view | supported | supported | supported |
| drop_view | supported/destructive metadata | supported/destructive metadata | supported/destructive metadata |
| alter_view | drop/create/manual review | supported/manual review | supported/manual review |
| create_trigger | supported/manual review | supported/manual review | supported/manual review |
| drop_trigger | supported/destructive metadata | supported/destructive metadata | supported/destructive metadata |
| alter_trigger | drop/create/manual review | manual review | manual review |
| create_sequence | not supported | supported | not supported by baseline profile |
| drop_sequence | not supported | supported/destructive metadata | not supported by baseline profile |

## Important rule

Capabilities are not permissions.

A supported operation can still be blocked later by:

- risk policy;
- missing confirmation;
- data checks;
- drift detection;
- driver/version limitations;
- manual-review requirements.

## Relationship with other modules

```text
SchemaDiffEngine
  -> produces semantic diff

SchemaRiskClassifier
  -> explains risk of each diff operation

DriverCapabilities
  -> explains whether a driver can technically support the operation

SchemaPlanner
  -> future module that combines diff + risk + capabilities into an executable plan
```
