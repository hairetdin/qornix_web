# Qornix ORM schema fixtures

These fixtures define the baseline XML contract examples for `qornix_orm/schema/schema_app.xsd`.

## Valid fixtures

- `valid_minimal_schema.xml` — minimal valid application schema with an empty `DataStructure`.
- `valid_schema_with_relations.xml` — valid schema with entities, fields, index and foreign key.
- `valid_fk_metadata_reuses_column.xml` — valid schema where a physical `Field` and a `ForeignKeyField` metadata entry reuse the same column name.

## Invalid fixtures

- `invalid_missing_required_field.xml` — `Field` is missing the required `type` attribute.
- `invalid_fk_reference.xml` — `ForeignKeyField/@references` points to a missing `Entity/@tableName`.
- `invalid_duplicate_names.xml` — duplicate `Entity/@name` value.

## Validation rule

The baseline contract is:

```text
qornix_orm/schema/schema_app.xsd
```

`ForeignKeyField` is relation metadata. It may reuse the same `@name` as a physical `Field` when the exported schema contains both the database column and the foreign-key relationship description.

All fixtures are designed for the XML validation layer.
