# Schema Manager and XML Schema Guide

This document explains how to work with the Qornix schema XML used by the generated `--with-dynamic-api` application.

The goal is practical onboarding: a new user should be able to read this page, edit `schema/app.schema.xml`, validate it, compare it with the current database, build a plan, and understand what the Schema Manager is showing without opening C++ source files or the XSD first.

## 1. What the schema file is

The schema XML is the desired database model for a Qornix application. It describes the application metadata, database configuration, entities, fields, foreign keys, indexes and optional advanced database objects.

In a generated Dynamic API app the important files are:

```text
schema/app.schema.xml                 editable desired schema used by the app
static/demo_schema.xml                 browser-loadable copy used by Load demo schema
static/schema_app.xsd                  raw XML Schema definition for validation
static/schema_manager_guide.md         raw Markdown version of this document
doc/schema_manager_guide.md            project-local Markdown documentation
```

The Schema Manager page is available at:

```text
GET /schema-manager
```

This documentation page is available at:

```text
GET /docs/schema
```

The bundled demo schema models four tables: `categories`, `customers`, `products`, and `orders`.

## 2. Recommended workflow

For a new generated app, use this flow:

1. Open `/schema-manager`.
2. Click **Load current DB schema** to see what is already in the database.
3. Click **Load demo schema** or paste your own `<Application>...</Application>` XML.
4. Click **Validate** to check XML syntax and the `schema_app.xsd` contract.
5. Click **Build diff** to compare desired XML with the current database schema.
6. Click **Build plan** to classify operations, risk level, confirmation requirements, and SQL preview.
7. Use **Dry-run only** when you want to test the pipeline without changing the database.
8. Apply only after reviewing destructive and manual-review operations.
9. Open `/api/dynamic/schema/history` or click **Refresh history** to inspect previous apply attempts.

For the quickest runnable demo, open `/` and click **Initialize demo database**. That endpoint resets the generated app SQLite database, writes the bundled schema copy, creates the demo tables, and inserts sample rows for Query Builder, Table Browser and API Playground.

## 3. Minimal valid schema

A schema document always starts with `Application` and contains `Name`, `Version`, `Configuration`, and `DataStructure`.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="1.0">
  <Name>My App</Name>
  <Version>1.0.0</Version>
  <Description>Example application schema.</Description>

  <Configuration>
    <DbConnectionString>app.sqlite3</DbConnectionString>
    <LogLevel>INFO</LogLevel>
    <Timeout>30</Timeout>
    <DatabaseEngine>sqlite</DatabaseEngine>
  </Configuration>

  <DataStructure>
    <Entity name="Products" tableName="products">
      <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true" />
      <Field name="name" type="TEXT" nullable="false" />
      <Field name="price" type="REAL" nullable="false" />
    </Entity>
  </DataStructure>
</Application>
```

## 4. Top-level XML structure

The top-level structure is intentionally small:

```text
Application
  Name
  Version
  Description?          optional
  Configuration
  DataStructure
  DatabaseMapping?      optional
```

`Application` also accepts the optional attribute:

| Attribute | Required | Default | Meaning |
|---|---:|---:|---|
| `schemaFormatVersion` | no | `1.0` | XML contract version. Current supported value is `1.0`. |

## 5. Configuration

`Configuration` tells the framework which database style the schema targets.

```xml
<Configuration>
  <DbConnectionString>app.sqlite3</DbConnectionString>
  <LogLevel>INFO</LogLevel>
  <Timeout>30</Timeout>
  <DatabaseEngine>sqlite</DatabaseEngine>
</Configuration>
```

| Element | Required | Allowed values | Meaning |
|---|---:|---|---|
| `DbConnectionString` | yes | string | Database connection string or file name. In the generated demo this is `app.sqlite3`. |
| `LogLevel` | yes | `DEBUG`, `INFO`, `WARN`, `ERROR` | Logging verbosity. |
| `Timeout` | yes | integer | Operation timeout value used by the application configuration. |
| `DatabaseEngine` | no | `sqlite`, `postgresql`, `mysql` | Engine hint used by schema tooling and type mapping. |

## 6. DataStructure

`DataStructure` is where you define tables and optional advanced database objects.

```text
DataStructure
  Entity*               normal tables
  Views?                optional SQL views
  MaterializedViews?    optional materialized views
  DatabaseFunctions?    optional database functions
  StoredProcedures?     optional stored procedures
  Triggers?             optional triggers
  Sequences?            optional standalone sequences
```

For the first application version, start with `Entity`, `Field`, `ForeignKeyField`, and optionally `Index`. Add advanced objects later, after the core tables are stable.

## 7. Entity: table definition

An `Entity` maps to one database table.

```xml
<Entity name="Products" tableName="products" verboseName="Product" verboseNamePlural="Products">
  <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true" />
  <Field name="name" type="TEXT" nullable="false" />
  <Field name="price" type="REAL" nullable="false" />
</Entity>
```

| Attribute | Required | Default | Meaning |
|---|---:|---:|---|
| `name` | yes | none | Logical entity name used by schema tooling. Keep it stable. |
| `tableName` | yes | none | Physical database table name and Dynamic API table name. |
| `verboseName` | no | empty | Human-readable singular name. |
| `verboseNamePlural` | no | empty | Human-readable plural name. |
| `description` | no | empty | Documentation string. |
| `tablespace` | no | empty | Engine-specific tablespace hint. |
| `isPartitioned` | no | `false` | Marks entity as partitioned when `Partitions` is present. |

Validation constraints that matter immediately:

- Every `Entity @name` must be unique.
- Every `Entity @tableName` must be unique.
- `ForeignKeyField @references` must point to another entity's `tableName`, not its logical `name`.
- Field, index and constraint names should be unique inside one entity.

## 8. Field: column definition

A `Field` maps to a normal database column.

```xml
<Field name="email" type="TEXT" nullable="false" unique="true" maxLength="255" />
```

| Attribute | Required | Default | Meaning |
|---|---:|---:|---|
| `name` | yes | none | Column name. |
| `type` | yes | none | Database type enum, such as `INTEGER`, `TEXT`, `REAL`, `DECIMAL`, `BOOLEAN`, `TIMESTAMP`, `UUID`, `JSON`. |
| `nullable` | no | `false` | Whether the column allows `NULL`. |
| `primaryKey` | no | `false` | Whether this column is part of the primary key. |
| `unique` | no | `false` | Whether this column should be unique. |
| `maxLength` | no | empty | Length hint for character fields. |
| `defaultValue` | no | empty | Default value expression or literal. |
| `verboseName` | no | empty | Human-readable field label. |
| `description` | no | empty | Field-level documentation. |
| `autoIncrement` | no | `false` | Auto-increment identity hint, commonly used on integer primary keys. |
| `collation` | no | empty | Engine-specific collation. |
| `isComputed` | no | `false` | Whether the column is computed/generated. |
| `computedExpression` | no | empty | Expression for computed/generated values. |
| `precision` | no | empty | Numeric precision. |
| `scale` | no | empty | Numeric scale. |

### Enum values

For `ENUM`-style columns, add values inside the field:

```xml
<Field name="status" type="ENUM" nullable="false" defaultValue="draft">
  <EnumValues>
    <Value>draft</Value>
    <Value>published</Value>
    <Value>archived</Value>
  </EnumValues>
</Field>
```

## 9. Practical type guide

The XSD allows a broad database type set because the framework supports multiple engines. For a generated SQLite demo app, these types are the most practical:

| Use case | Recommended type |
|---|---|
| Integer primary key | `INTEGER` with `primaryKey="true" autoIncrement="true"` |
| Short or long text | `TEXT` |
| Decimal-like demo values | `REAL` or `DECIMAL` |
| Boolean flags | `BOOLEAN` |
| Date or timestamp | `DATE`, `DATETIME`, or `TIMESTAMP` |
| JSON payload | `JSON` |
| External ids | `UUID` or `TEXT` |

The full `DatabaseTypeEnum` currently includes:

```text
INTEGER, BIGINT, VARCHAR, TEXT, DATE, TIMESTAMP, BOOLEAN, DECIMAL,
FLOAT, DOUBLE, TIME, DATETIME, JSON, JSONB, UUID, BLOB, TINYINT,
SMALLINT, MEDIUMINT, INT, REAL, CHAR, TINYTEXT, MEDIUMTEXT,
LONGTEXT, YEAR, ENUM, SET, TINYBLOB, MEDIUMBLOB, LONGBLOB,
SERIAL, BIGSERIAL, BYTEA, INET, CIDR, MACADDR, FOREIGN_KEY
```

## 10. ForeignKeyField: relationship definition

A `ForeignKeyField` describes a relationship column. In the current schema format, the demo schemas also include a matching normal `Field` for the same column so table metadata and relationship metadata are both explicit.

```xml
<Field name="category_id" type="INTEGER" nullable="true" />
<ForeignKeyField
  name="category_id"
  type="FOREIGN_KEY"
  nullable="true"
  references="categories"
  toField="id"
  onDelete="SET NULL"
  onUpdate="NO_ACTION" />
```

| Attribute | Required | Default | Meaning |
|---|---:|---:|---|
| `name` | yes | none | Local relationship column name. |
| `type` | yes | none | Usually `FOREIGN_KEY`. |
| `references` | yes | none | Referenced table name, matching `Entity @tableName`. |
| `toField` | no | `id` | Referenced column name. |
| `onDelete` | no | `NO_ACTION` | Delete action. |
| `onUpdate` | no | `NO_ACTION` | Update action. |
| `relatedName` | no | empty | Optional reverse relation name for higher-level tooling. |

Allowed foreign-key actions:

```text
NO_ACTION, CASCADE, SET_NULL, SET NULL, SET_DEFAULT, SET DEFAULT, RESTRICT
```

For portability, prefer `NO_ACTION`, `CASCADE`, `SET NULL`, and `RESTRICT`.

## 11. Indexes

An `Index` lists one or more field names.

```xml
<Index name="idx_products_name" unique="false" type="BTREE">
  <FieldName>name</FieldName>
</Index>
```

| Attribute | Required | Default | Meaning |
|---|---:|---:|---|
| `name` | yes | none | Index name. Must be unique inside the entity. |
| `unique` | no | `false` | Whether the index is unique. |
| `type` | no | `BTREE` | Index method. |
| `concurrently` | no | `false` | PostgreSQL-style concurrent index creation hint. |
| `tablespace` | no | empty | Engine-specific tablespace hint. |

Allowed index types:

```text
BTREE, HASH, GIST, GIN, BRIN, SPGIST
```

## 12. Constraints

Use `Constraint` for checks, explicit unique constraints, and exclusion constraints.

```xml
<Constraint constraintName="chk_products_price" type="CHECK">
  <Name>Positive product price</Name>
  <Description>Products must not have a negative price.</Description>
  <Expression>price &gt;= 0</Expression>
</Constraint>
```

Allowed constraint types:

```text
CHECK, EXCLUDE, UNIQUE
```

Keep expressions database-specific. The Schema Manager can validate the XML shape, but it cannot prove every SQL expression is portable or safe.

## 13. Options and partitioning

`Options` stores key-value hints for custom tooling or driver-specific behavior.

```xml
<Options>
  <Option key="owner" value="catalog-team" />
</Options>
```

`Partitions` describes range, list, or hash partitioning metadata.

```xml
<Partitions>
  <Strategy>RANGE</Strategy>
  <Columns>
    <Column>created_at</Column>
  </Columns>
  <Bounds>
    <Bound>
      <PartitionName>orders_2026</PartitionName>
      <LowerBound>2026-01-01</LowerBound>
      <UpperBound>2027-01-01</UpperBound>
    </Bound>
  </Bounds>
</Partitions>
```

Allowed partition strategies:

```text
RANGE, LIST, HASH
```

Partitioning is advanced and database-specific. Treat generated SQL and apply plans as manual-review material.

## 14. Views, functions, procedures, triggers and sequences

The XSD can describe advanced database objects:

```text
Views/View
MaterializedViews/MaterializedView
DatabaseFunctions/DatabaseFunction
StoredProcedures/StoredProcedure
Triggers/Trigger
Sequences/Sequence
```

Example view:

```xml
<Views>
  <View viewName="paid_orders" isUpdatable="false">
    <Name>Paid Orders</Name>
    <Description>Orders with paid status.</Description>
    <Query>SELECT * FROM orders WHERE status = 'paid'</Query>
  </View>
</Views>
```

Example sequence:

```xml
<Sequences>
  <Sequence name="order_number_seq" startValue="1000" increment="1" />
</Sequences>
```

Advanced objects are useful for documentation and future migration workflows, but they are more database-specific than basic entities. Expect plan output to require manual review, especially in SQLite.

## 15. DatabaseMapping

`DatabaseMapping` lets a schema specify physical column names and per-engine type choices separately from the logical entity and field model.

```xml
<DatabaseMapping>
  <EntityMapping entityName="Products" tableName="products">
    <FieldMapping fieldName="name" columnName="name" sqliteType="TEXT" postgresqlType="TEXT" />
    <FieldMapping fieldName="price" columnName="price" sqliteType="REAL" postgresqlType="DECIMAL" />
  </EntityMapping>
  <TypeMapping>
    <TypeMap appType="UUID" sqliteType="TEXT" postgresqlType="UUID" mysqlType="CHAR" />
  </TypeMapping>
</DatabaseMapping>
```

For first-run applications, you can usually omit `DatabaseMapping` and keep `Field @name` equal to the physical column name.

## 16. Schema Manager API endpoints

The Schema Manager UI is a frontend over these endpoints:

```text
GET  /api/dynamic/schema/export
POST /api/dynamic/schema/validate
POST /api/dynamic/schema/diff
POST /api/dynamic/schema/plan
POST /api/dynamic/schema/apply
GET  /api/dynamic/schema/history
GET  /api/dynamic/schema.xml
```

`GET /api/dynamic/schema.xml` is a compatibility alias for schema export.

## 17. Request formats

All POST schema endpoints accept either raw XML or a JSON object with an `xml` string.

Raw XML:

```bash
curl -X POST http://127.0.0.1:8008/api/dynamic/schema/validate \
  -H 'Content-Type: application/xml' \
  --data-binary @schema/app.schema.xml
```

JSON wrapper:

```json
{
  "xml": "<Application schemaFormatVersion=\"1.0\">...</Application>"
}
```

`apply` accepts additional confirmation fields:

```json
{
  "xml": "<Application schemaFormatVersion=\"1.0\">...</Application>",
  "plan_id": "schema-plan-id-from-plan-response",
  "destructive_confirmed": false,
  "manual_review_confirmed": false,
  "dry_run": true,
  "applied_by": "schema-manager"
}
```

Use the `plan_id` returned by `POST /api/dynamic/schema/plan`. If the schema changes between plan and apply, apply can reject the request with a plan-id mismatch.

## 18. Response shapes

### Validate

```json
{
  "ok": true,
  "valid": true,
  "message": "XML schema is valid",
  "schema_path": ".../qornix_orm/schema/schema_app.xsd",
  "errors": []
}
```

When invalid, `errors` contains line, column, path, code, and message.

### Diff

```json
{
  "ok": true,
  "has_changes": true,
  "operation_count": 2,
  "diff": {
    "hasChanges": true,
    "operations": []
  },
  "text": "Schema differences: ..."
}
```

### Plan

```json
{
  "ok": true,
  "plan_id": "...",
  "operation_count": 2,
  "requires_confirmation": false,
  "contains_destructive_operations": false,
  "contains_unsupported_operations": false,
  "contains_manual_review_operations": false,
  "diff": {},
  "plan": {},
  "sql_preview": "..."
}
```

### Apply

```json
{
  "ok": true,
  "success": true,
  "dry_run": true,
  "plan_id": "...",
  "result": {
    "success": true,
    "dryRun": true,
    "operations": []
  },
  "plan": {},
  "sql_preview": "..."
}
```

## 19. Diff operation names

The current diff engine can report these operation kinds:

```text
table_added, table_only_in_current, table_changed,
column_added, column_only_in_current, column_changed,
primary_key_changed,
foreign_key_added, foreign_key_only_in_current, foreign_key_changed,
index_added, index_only_in_current, index_changed,
view_added, view_only_in_current, view_changed,
trigger_added, trigger_only_in_current, trigger_changed,
sequence_added, sequence_only_in_current, sequence_changed
```

Operations named `*_only_in_current` usually mean the current database has an object that is not present in the desired XML. Applying such a plan can be destructive, so review carefully.

## 20. Risk and confirmation rules

Schema Manager separates three concepts:

| Concept | Meaning |
|---|---|
| Destructive operation | Can remove data or database objects, such as dropping a table or column. |
| Manual-review operation | The driver or policy says a human must inspect the generated SQL and impact. |
| Unsupported operation | The selected driver does not support automatic apply for this operation. |

Recommended policy:

- Additive table and nullable-column changes are the safest.
- Dropping tables, columns, indexes, triggers, views, or sequences requires review.
- Changing primary keys, nullability, foreign keys, and column types should be treated as migration work, not a casual UI action.
- Always run a dry-run before a destructive or manual-review apply.
- Keep backups for real databases.

## 21. Current implementation notes

The current generated template is best at onboarding users into the schema workflow and Dynamic API flow.

Important current behavior:

- `Validate` checks XML shape using `schema_app.xsd`.
- `Diff` compares the desired XML model with an exported model of the current database.
- `Plan` classifies operations and shows SQL preview text.
- `Apply` records schema history and executes only operations that are marked executable and have concrete SQL.
- SQL preview lines containing placeholders such as `<table_name>`, `<type>`, or `...` are treated as preview/manual material and are skipped by the applier.
- The generated demo app includes `POST /api/demo/setup` so a beginner can initialize a runnable database even while full automatic SQL generation is evolving.

This means the Schema Manager documentation should be read as the schema contract and workflow reference, while the generated demo setup endpoint remains the fastest way to get working sample data.

## 22. Common edits

### Add a nullable column

```xml
<Entity name="Products" tableName="products">
  <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true" />
  <Field name="name" type="TEXT" nullable="false" />
  <Field name="price" type="REAL" nullable="false" />
  <Field name="notes" type="TEXT" nullable="true" />
</Entity>
```

### Add a required column safely

Prefer adding a default value when rows may already exist:

```xml
<Field name="status" type="TEXT" nullable="false" defaultValue="active" />
```

### Add a relationship

```xml
<Entity name="Products" tableName="products">
  <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true" />
  <Field name="category_id" type="INTEGER" nullable="true" />
  <ForeignKeyField name="category_id" type="FOREIGN_KEY" nullable="true" references="categories" toField="id" onDelete="SET NULL" />
</Entity>
```

Remember: `references="categories"` uses the target table name.

## 23. Naming recommendations

Use stable, boring names:

- Entity `name`: PascalCase or singular/plural logical name, for example `Products` or `Product`.
- Entity `tableName`: lowercase snake_case plural table name, for example `products`.
- Field `name`: lowercase snake_case column name, for example `created_at`.
- Index `name`: include table and fields, for example `idx_products_category_id`.
- Constraint `constraintName`: include table and purpose, for example `chk_products_price_non_negative`.

Changing names later looks like a drop/create or rename operation to migration tooling, so choose names deliberately.

## 24. Troubleshooting

### Validation says the schema is invalid

Check the `errors` array. It includes line, column, path, code and message. Common causes are missing required attributes, invalid enum values, or a foreign key pointing to an entity `name` instead of a `tableName`.

### Diff has more operations than expected

Load current DB schema and save it locally. Compare it with your desired XML. Differences in table names, field names, defaults and nullability are interpreted as schema changes.

### Apply does not change the database

Check the apply result. Operations may be skipped when they require manual review, are unsupported for the selected driver, or only have placeholder SQL preview. Use `/` -> **Initialize demo database** for the bundled first-run sample.

### The Dynamic API cannot see a table

Open `/api/dynamic/meta/tables`. If the table is not listed, the database does not currently contain that table, even if it exists in the desired XML. Apply or initialize the database first.
