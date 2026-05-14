# Schema Risk Policy

`SchemaRiskClassifier` sits between semantic diff and schema planning.

It does not execute SQL and does not modify XML or the database. Its only responsibility is to explain how risky each `SchemaDiffOperation` is and what policy decision should be made before a future planner/applier handles it.

## Position in the schema pipeline

```text
schema_app.xsd
  -> XML validation
  -> SchemaDocument desired
  -> DatabaseSnapshot
  -> SchemaDocument current
  -> SchemaDiffEngine
  -> SchemaRiskClassifier
  -> SchemaPlanner
  -> SchemaApplier
```

The risk classifier implements only this part:

```text
SchemaDocumentDiff
  -> SchemaRiskClassifier
  -> SchemaRiskReport
```

## Main classes

```text
qornix_orm/core/schema_risk_policy.h
qornix_orm/core/schema_risk_policy.cpp
```

Public API:

```cpp
SchemaRiskAssessment SchemaRiskClassifier::classifyOperation(
    const SchemaDiffOperation& operation,
    const SchemaPolicy& policy = SchemaPolicy::defaultPolicy()
);

SchemaRiskReport SchemaRiskClassifier::classifyDiff(
    const SchemaDocumentDiff& diff,
    const SchemaPolicy& policy = SchemaPolicy::defaultPolicy()
);
```

## Risk levels

```text
safe
warning
destructive
unsupported
manual_review
```

### `safe`

Operation is usually safe and does not modify existing rows.

Examples:

```text
table_added
index_added
view_added
```

### `warning`

Operation is usually possible but should be inspected by the planner or user.

Examples:

```text
column_added
foreign_key_added
index_changed
view_changed
trigger_added
```

A new column is classified as `warning` at this layer because the diff operation itself may not contain enough context to know whether it is nullable or has a default value. The future planner can refine this after inspecting the full schema model.

### `destructive`

Operation may remove data or database objects, or remove database integrity guarantees.

Examples:

```text
table_only_in_current
column_only_in_current
foreign_key_only_in_current
view_only_in_current
trigger_only_in_current
```

Objects that exist only in the current database schema are not automatically deleted. They are only marked as destructive candidates for future planning.

### `manual_review`

Operation is too sensitive or driver-specific for automatic handling at this layer.

Examples:

```text
table_changed
primary_key_changed
column_changed type/maxLength/precision/scale
trigger_changed
foreign_key_changed
```

### `unsupported`

Operation is intentionally not executable until future driver capabilities and planner support exist.

Examples:

```text
sequence_added
sequence_only_in_current
sequence_changed
```

`DriverCapabilities` can refine unsupported/manual-review decisions by database driver.

## Policy decisions

```text
allowed
requires_confirmation
blocked
manual_review_required
unsupported
```

Default policy:

```cpp
SchemaPolicy policy = SchemaPolicy::defaultPolicy();
```

Default behavior:

```text
safe         -> allowed
warning      -> allowed
destructive -> requires_confirmation
manual_review -> manual_review_required
unsupported -> unsupported
```

Review policy:

```cpp
SchemaPolicy policy = SchemaPolicy::reviewPolicy();
```

Review behavior requires confirmation even for warning operations.

Permissive policy:

```cpp
SchemaPolicy policy = SchemaPolicy::permissivePolicy();
```

Permissive policy allows manual-review operations and destructive operations to proceed to later stages, but destructive operations still require explicit confirmation by default.

## Example

```cpp
#include "core/schema_diff_engine.h"
#include "core/schema_risk_policy.h"

SchemaDocument desired = ...;
SchemaDocument current = ...;

SchemaDocumentDiff diff = SchemaDiffEngine::compare(desired, current);
SchemaRiskReport risk = SchemaRiskClassifier::classifyDiff(diff);

if (risk.hasDestructiveOperations()) {
    std::cout << "Schema contains destructive changes" << std::endl;
}

std::cout << risk.toText() << std::endl;
std::cout << risk.toJsonString() << std::endl;
```

## Example JSON output

```json
{
  "hasAssessments": true,
  "hasBlockedOperations": true,
  "assessments": [
    {
      "kind": "column_only_in_current",
      "objectType": "column",
      "objectPath": "/entities/products/fields/legacy_code",
      "objectName": "legacy_code",
      "property": "",
      "riskLevel": "destructive",
      "decision": "requires_confirmation",
      "executable": false,
      "requiresConfirmation": true,
      "reason": "Column exists in the current database but not in desired schema; aligning strictly would drop the column.",
      "dataImpact": "Dropping the column may permanently remove data from every row.",
      "requiredConfirmation": "Explicit destructive confirmation is required before a future plan can drop this column.",
      "recommendedAction": "Review whether the column should be kept, ignored, renamed or explicitly dropped."
    }
  ]
}
```

## Important limitation

`SchemaRiskClassifier` works on diff operations. Some decisions need more context than the diff operation currently contains.

For example:

```text
column_added
```

can be safe if the column is nullable, but can require review if it is `NOT NULL` without a default value.

The current policy classifies this as `warning`. `SchemaPlanner` can refine it by looking at the full `SchemaDocument` and driver capabilities.

## Relation to planning and apply layers

- `DriverCapabilities` can refine risk by database engine.
- `SchemaPlanner` converts risk-assessed diff operations into ordered plan operations.
- `SchemaApplier` executes only confirmed plan operations.

