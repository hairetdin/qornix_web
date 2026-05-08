# XML schema core pipeline

`qornix_orm` schema core is built around a single pipeline:

```text
qornix_orm/schema/schema_app.xsd
  -> XML validation
  -> SchemaDocument
  -> SchemaNormalizer
  -> DatabaseSnapshot
  -> DatabaseSchemaExporter
  -> SchemaDiffEngine
  -> SchemaRiskClassifier
  -> DriverCapabilities
  -> SchemaPlanner
  -> SQL preview
  -> SchemaApplier
  -> SchemaHistoryStore
```

## Artifacts

| Artifact | Meaning |
|---|---|
| uploaded XML | User intent. This file is not overwritten by database export. |
| validated SchemaDocument | Typed semantic representation of uploaded XML. |
| normalized desired SchemaDocument | Stable desired model for diff. |
| DatabaseSnapshot | Technical database introspection output. |
| current SchemaDocument | Canonical semantic model exported from current database state. |
| SchemaDocumentDiff | Semantic difference between desired/current models. |
| SchemaPlan | Ordered driver-aware operations with risk labels and SQL preview. |
| SchemaApplyResult | Result of dry-run or apply. |
| SchemaHistoryRecord | Audit trail entry. |

## Important comparison rule

Do not compare raw XML with raw database snapshot.

Correct comparison:

```text
normalized SchemaDocument desired
  vs
normalized SchemaDocument current
```

`DatabaseSnapshot` is a technical introspection layer only. It is converted into `SchemaDocument current` before diff.

