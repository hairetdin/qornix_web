# Schema plan and safe apply

`SchemaPlanner` and `SchemaApplier` are the ORM-level bridge between semantic schema diff and database execution.

The intended flow is:

```text
schema_app.xsd
  -> validate XML
  -> normalize XML
  -> desired SchemaDocument
  -> introspect database
  -> current SchemaDocument
  -> semantic diff
  -> risk classification
  -> schema plan
  -> SQL preview
  -> explicit apply
```

## SchemaPlanner

Code:

```text
qornix_orm/core/schema_plan.h
qornix_orm/core/schema_plan.cpp
```

`SchemaPlanner` converts `SchemaDocumentDiff` into `SchemaPlan` using:

- `DriverCapabilities`;
- `SchemaPolicy`;
- `SchemaRiskClassifier`.

Minimal example:

```cpp
#include "core/schema_diff_engine.h"
#include "core/schema_plan.h"

SchemaDocumentDiff diff = SchemaDiffEngine::compare(desired, current);
DriverCapabilities capabilities = DriverCapabilities::sqlite();
SchemaPolicy policy = SchemaPolicy::defaultPolicy();

SchemaPlan plan = SchemaPlanner::buildPlan(diff, capabilities, policy);

std::cout << plan.toText() << std::endl;
std::cout << plan.toSqlPreview() << std::endl;
```

A plan contains:

- ordered operations;
- risk labels;
- policy decisions;
- driver capability metadata;
- SQL preview;
- manual-review operations;
- unsupported operations.

`SchemaPlanner` does not change XML and does not change the database.

## SQL preview

`SchemaPlan::toSqlPreview()` shows what the ORM intends to do.

At this stage, SQL preview is intentionally conservative. For some operations it contains placeholders such as `<table_name>` or `<type>`. Placeholder SQL is meant for review and must not be treated as a final executable migration script.

Future driver-specific SQL generation can gradually replace placeholders with executable SQL.

## SchemaApplier

Code:

```text
qornix_orm/core/schema_applier.h
qornix_orm/core/schema_applier.cpp
```

`SchemaApplier` accepts only a `SchemaPlan`:

```cpp
SchemaApplyOptions options;
options.dryRun = true;

SchemaApplyResult result = SchemaApplier::dryRun(plan);
```

For real execution:

```cpp
SchemaApplyOptions options;
options.destructiveConfirmed = true;
options.manualReviewConfirmed = true;

SchemaApplyResult result = SchemaApplier::applyPlan(plan, *db, options);
```

Important rules:

- raw XML is not applied directly;
- apply works from an already built plan;
- destructive operations require explicit confirmation;
- manual-review operations require explicit confirmation;
- unsupported operations are not executable;
- transaction wrapper is used when enabled and supported by the caller's driver flow;
- placeholder SQL is skipped rather than executed.

## Dry-run

Dry-run is the default safe way to inspect a plan:

```cpp
SchemaApplyResult result = SchemaApplier::dryRun(plan);
```

Dry-run never changes the database.

## Transaction behavior

`SchemaTransaction` wraps simple transaction commands:

```text
BEGIN
COMMIT
ROLLBACK
```

Database drivers differ in transactional DDL behavior. `DriverCapabilities` should be used to decide whether transaction wrapping is appropriate for a selected plan.

