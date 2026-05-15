# schema_app.xsd coverage matrix

## Purpose

`qornix_orm/schema/schema_app.xsd` is the baseline XML contract for Qornix ORM application schemas.

The contract is used to align:

```text
user XML schema
  -> XSD validation
  -> SchemaDocument semantic model
  -> database export format
  -> semantic diff
  -> schema plan/apply pipeline
```

## Contract version

Current XML schema contract version:

```text
1.0
```

The root element supports an optional attribute:

```xml
<Application schemaFormatVersion="1.0">
```

If the attribute is omitted, the schema should be treated as `1.0` for backward compatibility.

## Reference XSD

```text
qornix_orm/schema/schema_app.xsd
```

## Baseline fixtures

Fixtures are stored in:

```text
qornix_orm/schema/fixtures/
```

| Fixture | Expected result | Purpose |
| --- | --- | --- |
| `valid_minimal_schema.xml` | Valid | Minimal empty `DataStructure` schema |
| `valid_schema_with_relations.xml` | Valid | Entities, fields, index and FK reference |
| `valid_fk_metadata_reuses_column.xml` | Valid | Physical `Field` and `ForeignKeyField` relation metadata reuse the same column name |
| `invalid_missing_required_field.xml` | Invalid | Missing required `Field/@type` |
| `invalid_fk_reference.xml` | Invalid | FK references an unknown `Entity/@tableName` |
| `invalid_duplicate_names.xml` | Invalid | Duplicate `Entity/@name` |

## XSD support matrix

| XSD element / construct | ORM parser support | DB introspection support | XML export support | Diff/plan/apply support | Notes |
| --- | --- | --- | --- | --- | --- |
| `Application` | Supported | N/A | Supported | Planned | Root XML contract. `schemaFormatVersion="1.0"` is the current contract marker. |
| `Name` | Supported | Generated during export | Supported | Planned | Stored in `SchemaMetadata::name`. |
| `Version` | Supported | Generated during export | Supported | Planned | Stored in `SchemaMetadata::version`. |
| `Description` | Supported | Generated during export | Supported | Planned | Optional. |
| `Configuration` | Supported | Partially inferred from DB config | Supported | N/A | Contains connection/logging/timeout/engine metadata. |
| `DataStructure` | Supported | Supported through introspection | Supported | Planned | Empty `DataStructure` is valid. |
| `Entity` | Supported | Supported as table metadata | Supported | Partially supported by current compare/apply | `tableName` is required in the XSD contract. |
| `Field` | Supported | Supported as column metadata | Supported | Partially supported by current compare/apply | Required attributes: `name`, `type`. |
| `ForeignKeyField` | Supported | Supported for SQLite/PostgreSQL/MySQL with driver limitations | Supported | Partially supported | `references` targets `Entity/@tableName`. |
| `Index` | Supported | Supported with driver limitations | Supported | Diff/apply planned | XSD requires unique index names per entity. |
| `Constraint` | Supported | Partially supported | Supported | Diff/apply planned | CHECK/UNIQUE/EXCLUDE are represented in XSD; driver support differs. |
| `EntityFunctions` | Parsed | N/A | Export support is limited | Not planned for DB apply | XSD allows `python`, `javascript`, `cpp`, `sql`, `plpgsql` as function languages. |
| `Views` / `View` | Supported | Supported for available drivers | Supported | Diff/apply planned | View field metadata is currently not deeply modeled by parser. |
| `MaterializedViews` | Supported | PostgreSQL-oriented support | Supported | Planned | Driver-specific. |
| `StoredProcedures` | Partially supported | Driver-specific / partial | Supported | Planned | Parameter parsing/export needs deeper coverage. |
| `DatabaseFunctions` | Partially supported | PostgreSQL-oriented support | Supported | Planned | Parameter parsing/export needs deeper coverage. |
| `Triggers` | Supported | Supported where introspection exists | Supported | Planned | Driver differences are expected. |
| `Sequences` | Supported | PostgreSQL-oriented support | Supported | Planned | Driver-specific. |
| `DatabaseMapping` | Supported | Generated from entities when explicit mapping is missing | Supported | N/A | Maps application fields to database columns/types. |
| `TypeMapping` | Supported | N/A | Supported when configured | N/A | Maps application types to database-specific types. |

## Known gaps

1. XSD validation is not wired into `SchemaLoader::loadSchemaFromFile(...)` yet.
2. The current compare/apply API works with `SchemaComparisonResult`, not with normalized `SchemaDocument` objects.
3. `DatabaseSnapshot` should remain a technical introspection layer. The semantic diff should compare normalized `SchemaDocument desired` and `SchemaDocument current`.
4. Generated schemas may represent a foreign-key column both as a physical `Field` and as `ForeignKeyField` relation metadata with the same `@name`. This is valid and must not fail XSD validation.
5. Stored procedure and database function parameters need deeper parser/export coverage.
6. Driver support for materialized views, triggers, sequences and advanced constraints is not uniform.

## Baseline contract decisions

| Decision | Rule |
| --- | --- |
| XML contract source | `qornix_orm/schema/schema_app.xsd` |
| Current contract version | `1.0` |
| Root version marker | `Application/@schemaFormatVersion` |
| Entity identity | `Entity/@name` must be unique |
| Database table identity | `Entity/@tableName` must be unique and required |
| FK reference target | `ForeignKeyField/@references` targets `Entity/@tableName` |
| Field identity | `Field/@name` values must be unique inside an entity |
| Foreign key metadata identity | `ForeignKeyField/@name` values must be unique among `ForeignKeyField` entries inside an entity, but may reuse the same name as a physical `Field` |
| Index identity | `Index/@name` must be unique inside an entity |
| Constraint identity | `Constraint/@constraintName` must be unique inside an entity |
| Empty schema | `DataStructure` may contain zero entities |

## Runtime validation follow-up

The runtime validation layer should include:

```cpp
class XmlSchemaValidator;
```

The validator must use `qornix_orm/schema/schema_app.xsd` and the fixtures in `qornix_orm/schema/fixtures/`.
