# Dynamic API Query Syntax

This document describes the **current** Qornix Schema-driven Dynamic API query contract implemented by `qornix_dynamic_api::DynamicQueryService` and the ORM `QueryBuilder`.

It intentionally documents the API that exists today, not the future `POST /api/dynamic/query` contract sketched in older design notes.

## 1. What this API is for

The Dynamic API lets an application expose CRUD and query endpoints for tables discovered from the current database schema. Every request is checked against a metadata allowlist built from the database schema before SQL is generated.

Typical flow for a generated `--with-dynamic-api` app:

1. Initialize or apply a schema.
2. Inspect available tables and fields through metadata endpoints.
3. Send JSON query payloads to `/api/dynamic` or table-specific endpoints.
4. Read the normalized JSON response.

## 2. Canonical endpoints

Assuming the default prefix:

```text
/api/dynamic
```

### Metadata endpoints

```text
GET /api/dynamic/meta/tables
GET /api/dynamic/meta/{table}/fields
GET /api/dynamic/meta/{table}/fields-info
GET /api/dynamic/meta/allowlist
GET /api/dynamic/meta/autocomplete?type=tables&q=pro
GET /api/dynamic/meta/autocomplete?type=fields&table=products&q=pr
```

### Data endpoints

```text
POST   /api/dynamic
GET    /api/dynamic/{table}
POST   /api/dynamic/{table}
GET    /api/dynamic/{table}/{id}
PUT    /api/dynamic/{table}/{id}
PATCH  /api/dynamic/{table}/{id}
DELETE /api/dynamic/{table}/{id}
```

`POST /api/dynamic` is the most flexible endpoint. It accepts a request envelope that can describe read, create, update, patch, and delete operations.

Table-specific endpoints are convenient shortcuts when the table and/or id are already encoded in the URL.

### Schema endpoints

These are not query endpoints, but they are part of the generated Dynamic API app:

```text
GET  /api/dynamic/schema/export
POST /api/dynamic/schema/validate
POST /api/dynamic/schema/diff
POST /api/dynamic/schema/plan
POST /api/dynamic/schema/apply
GET  /api/dynamic/schema/history
GET  /api/dynamic/schema.xml
GET  /api/dynamic/openapi.json
GET  /openapi.json
```

## 3. Response envelope

Most Dynamic API data responses have this shape:

```json
{
  "ok": true,
  "message": "Query executed successfully",
  "count": 2,
  "data": [
    {
      "id": "1",
      "name": "Wireless Mouse"
    }
  ]
}
```

Fields returned by the database are serialized as strings in the current implementation.

Error responses have this shape:

```json
{
  "ok": false,
  "error": "table_not_allowed",
  "message": "Table is not allowed by schema metadata: unknown_table"
}
```

Common error codes include:

```text
table_required
table_not_allowed
table_read_forbidden
table_write_forbidden
table_delete_forbidden
field_not_allowed
field_not_writable
field_not_filterable
field_not_sortable
invalid_json
invalid_field
invalid_where
invalid_filter
invalid_order
raw_join_disabled
raw_query_disabled
filter_required
dynamic_query_failed
```

## 4. Request envelope

The canonical JSON body is:

```json
{
  "method": "GET",
  "table": "products",
  "query": {
    "fields": ["id", "name", "price"],
    "where": [
      { "field": "price", "op": "gte", "value": 40 }
    ],
    "order_by": ["price DESC"],
    "limit": 25
  },
  "data": {
    "name": "Wireless Mouse"
  }
}
```

Top-level fields:

| Field | Type | Required | Meaning |
|---|---:|---:|---|
| `method` | string | usually | Logical operation: `GET`, `POST`, `PUT`, `PATCH`, `DELETE`. If omitted on a table-specific endpoint, the HTTP method is used. |
| `table` | string | yes, unless URL has `{table}` | Table to read/write. Must exist in the metadata allowlist. |
| `query` | object | no | Read filters, selected fields, sorting, joins, grouping, and limit. |
| `data` | object | for writes | Column values for `POST`, `PUT`, or `PATCH`. |

Recommended rule: **always use the `data` envelope for writes**. Do not rely on flat JSON bodies such as `{ "name": "x" }` for the current Dynamic API service.

## 5. Query object fields

```json
{
  "fields": ["id", "name"],
  "where": [
    { "field": "price", "op": "gte", "value": 40 }
  ],
  "filter": ["stock>0"],
  "join": ["JOIN categories ON products.category_id=categories.id"],
  "group_by": ["status"],
  "having": ["total_amount>20"],
  "order_by": ["price DESC"],
  "limit": 25
}
```

| Field | Type | Meaning |
|---|---:|---|
| `fields` | array of strings | SELECT fields. Defaults to `*` when omitted. |
| `where` | array of objects | Preferred structured filters. Each item is `{field, op, value}`. |
| `filter` | array of strings | Raw filter strings, for example `"price>=40"`. Use sparingly. |
| `join` | array of strings | Raw JOIN clauses. Disabled by default unless `allowRawJoin=true`. |
| `group_by` | array of strings | GROUP BY fields. |
| `having` | array of strings | HAVING conditions using the same simple filter syntax as `filter`. |
| `order_by` | array of strings | ORDER BY clauses, each requiring `FIELD ASC` or `FIELD DESC`. |
| `limit` | integer | Result limit. Clamped to `1..maxLimit`. |

## 6. Metadata first: discover tables and fields

Before building UI queries, ask the metadata endpoints what is currently allowed.

### List tables

```bash
curl 'http://127.0.0.1:8008/api/dynamic/meta/tables?limit=100&offset=0'
```

Example response:

```json
{
  "ok": true,
  "message": "tables loaded",
  "tables": ["categories", "customers", "orders", "products"]
}
```

Query parameters:

| Parameter | Meaning |
|---|---|
| `q` | Optional case-insensitive prefix filter. |
| `limit` | Maximum number of table names. |
| `offset` | Number of matching names to skip. |

### List field names

```bash
curl 'http://127.0.0.1:8008/api/dynamic/meta/products/fields'
```

Example response:

```json
{
  "ok": true,
  "message": "fields loaded",
  "fields": ["id", "category_id", "name", "description", "price", "stock", "created_at"]
}
```

`q` can be used as a prefix filter:

```text
GET /api/dynamic/meta/products/fields?q=pr
```

### Field policy metadata

```bash
curl 'http://127.0.0.1:8008/api/dynamic/meta/products/fields-info'
```

Example response:

```json
{
  "ok": true,
  "message": "field metadata loaded",
  "table": "products",
  "fields": [
    {
      "name": "id",
      "type": "string",
      "readable": true,
      "writable": false,
      "filterable": true,
      "sortable": true,
      "hidden": false
    }
  ]
}
```

### Full allowlist

```bash
curl 'http://127.0.0.1:8008/api/dynamic/meta/allowlist'
```

The allowlist is the server-side contract for what a request may read, write, filter, sort, or delete.

## 7. Reading rows

### Simple table read

```bash
curl 'http://127.0.0.1:8008/api/dynamic/products'
```

Equivalent structured request:

```bash
curl -X POST 'http://127.0.0.1:8008/api/dynamic' \
  -H 'Content-Type: application/json' \
  -d '{
    "method": "GET",
    "table": "products",
    "query": { "limit": 25 }
  }'
```

### Read one row by id

```bash
curl 'http://127.0.0.1:8008/api/dynamic/products/1'
```

This implicitly adds the filter:

```text
id=1
```

### Select specific fields

```bash
curl -X POST 'http://127.0.0.1:8008/api/dynamic' \
  -H 'Content-Type: application/json' \
  -d '{
    "method": "GET",
    "table": "products",
    "query": {
      "fields": ["id", "name", "price", "stock"],
      "limit": 25
    }
  }'
```

`fields` must contain readable fields for the base table. In the current implementation, joined-table field names such as `categories.name` are not validated against the joined table. For JOIN examples, omit `fields` and let the query use `SELECT *`, or select only fields from the base table.

## 8. Structured `where`

Use `where` for normal filters. It is safer than raw filter strings because the value is converted to a SQL literal by the service.

```json
{
  "method": "GET",
  "table": "products",
  "query": {
    "fields": ["id", "name", "price", "stock"],
    "where": [
      { "field": "price", "op": "gte", "value": 40 },
      { "field": "stock", "op": "gt", "value": 0 }
    ],
    "order_by": ["price DESC"],
    "limit": 25
  }
}
```

Supported `op` values:

| `op` | SQL |
|---|---|
| `eq` or `=` | `=` |
| `ne` or `!=` | `!=` |
| `gt` or `>` | `>` |
| `gte` or `>=` | `>=` |
| `lt` or `<` | `<` |
| `lte` or `<=` | `<=` |
| `like` | `LIKE` |

Notes:

- `field` must be filterable in the table allowlist.
- Qualified fields such as `orders.status` are accepted for allowlist checking by stripping the qualifier, but the SQL generated for structured `where` uses the stripped field name. For JOIN filters that need a qualified column, use raw `filter` instead.
- Multiple `where` items are joined with `AND`.

## 9. Raw `filter`

`filter` is an array of simple SQL-like conditions:

```json
{
  "method": "GET",
  "table": "products",
  "query": {
    "filter": ["price>=40", "stock>0"],
    "order_by": ["price DESC"],
    "limit": 25
  }
}
```

Allowed raw filter shape:

```text
field=value
field!=value
field>value
field>=value
field<value
field<=value
```

String values should be single-quoted:

```json
{
  "filter": ["name='Wireless Mouse'"]
}
```

The current raw filter validator rejects obvious injection markers in the value part, including semicolons, SQL comments, and embedded ` OR ` / ` AND `.

Use one array item per condition:

```json
{
  "filter": ["price>=40", "stock>0"]
}
```

Do **not** put this in one string:

```json
{
  "filter": ["price>=40 AND stock>0"]
}
```

For joined queries, raw filter can use qualified fields:

```json
{
  "filter": ["orders.status='paid'"]
}
```

The allowlist check strips the qualifier and checks `status` against the base table.

## 10. Sorting

`order_by` items must contain a field and direction:

```json
{
  "order_by": ["price DESC", "name ASC"]
}
```

Rules:

- Direction must be uppercase `ASC` or `DESC`.
- The field must be sortable in the table allowlist.
- Qualified fields such as `orders.order_date DESC` are accepted if the unqualified field exists in the base table.

Invalid examples:

```json
{ "order_by": ["price"] }
{ "order_by": ["price desc"] }
```

## 11. Limit

`limit` is optional.

When it is omitted from a structured query, the service applies `DynamicApiConfig::defaultLimit`, currently `100` by default.

When it is provided, the value is clamped to:

```text
1..DynamicApiConfig::maxLimit
```

The default `maxLimit` is `1000`.

Example:

```json
{
  "query": { "limit": 50 }
}
```

Offset is not part of the current data query contract. Only the metadata table endpoint supports `offset`.

## 12. JOIN

Raw JOIN clauses are disabled by default:

```cpp
config.allowRawJoin = false;
```

A generated showcase app may explicitly enable it:

```cpp
config.allowRawJoin = true;
```

JOIN syntax accepted by `QueryBuilder`:

```text
JOIN table ON left_field=right_field
INNER JOIN table ON left_field=right_field
LEFT JOIN table ON left_field=right_field
RIGHT JOIN table ON left_field=right_field
FULL JOIN table ON left_field=right_field
LEFT OUTER JOIN table ON left_field=right_field
RIGHT OUTER JOIN table ON left_field=right_field
FULL OUTER JOIN table ON left_field=right_field
```

Example:

```bash
curl -X POST 'http://127.0.0.1:8008/api/dynamic' \
  -H 'Content-Type: application/json' \
  -d '{
    "method": "GET",
    "table": "orders",
    "query": {
      "join": ["JOIN products ON orders.product_id=products.id"],
      "filter": ["orders.status='paid'"],
      "order_by": ["orders.total_amount DESC"],
      "limit": 10
    }
  }'
```

Current JOIN limitations:

- JOIN is raw string DSL and should be treated as a showcase / trusted-admin feature until relation-aware joins are implemented.
- `fields` validation is base-table-centric. If you need a joined query today, the most reliable first-run demo pattern is to omit `fields`, which produces `SELECT *`.
- JOIN table and field names must pass identifier validation: letters, digits, underscores and dots only.
- The JOIN condition must be equality-based and use `ON`.

## 13. GROUP BY and HAVING

`group_by` accepts field identifiers:

```json
{
  "group_by": ["status"]
}
```

`having` accepts the same simple filter syntax as raw `filter`:

```json
{
  "having": ["total_amount>20"]
}
```

Current aggregation limitations:

- `fields` are validated as identifiers, so aggregate expressions such as `COUNT(id)` are not supported as selected fields by the current `QueryBuilder` validation.
- `having` does not currently support aggregate expressions such as `COUNT(id)>0` because the left side must pass field identifier validation.
- Treat `group_by` / `having` as low-level current QueryBuilder features, not a complete analytics DSL yet.

## 14. Creating rows

Preferred generic endpoint:

```bash
curl -X POST 'http://127.0.0.1:8008/api/dynamic' \
  -H 'Content-Type: application/json' \
  -d '{
    "method": "POST",
    "table": "categories",
    "data": {
      "name": "Accessories",
      "description": "Small useful add-ons",
      "created_at": "2026-05-08T10:00:00Z"
    }
  }'
```

Table-specific equivalent:

```bash
curl -X POST 'http://127.0.0.1:8008/api/dynamic/categories' \
  -H 'Content-Type: application/json' \
  -d '{
    "data": {
      "name": "Accessories",
      "description": "Small useful add-ons",
      "created_at": "2026-05-08T10:00:00Z"
    }
  }'
```

Rules:

- The table must be writable.
- Every key in `data` must be writable for the table.
- Primary key / auto-increment / computed fields are considered non-writable by the schema allowlist.
- Current SQL generation uses `INSERT ... RETURNING id`. Ensure the selected database driver supports the generated SQL.

## 15. Updating rows

The safest path is to use the row id in the URL:

```bash
curl -X PATCH 'http://127.0.0.1:8008/api/dynamic/products/1' \
  -H 'Content-Type: application/json' \
  -d '{
    "data": {
      "price": 44.5,
      "stock": 80
    }
  }'
```

Equivalent generic request:

```bash
curl -X POST 'http://127.0.0.1:8008/api/dynamic' \
  -H 'Content-Type: application/json' \
  -d '{
    "method": "PATCH",
    "table": "products",
    "query": {
      "where": [
        { "field": "id", "op": "eq", "value": 1 }
      ]
    },
    "data": {
      "price": 44.5,
      "stock": 80
    }
  }'
```

Important current safety default:

```cpp
config.allowUpdateWithoutFilter = false;
```

So `PUT` and `PATCH` without a URL id are rejected before execution. Use the URL id form for predictable updates.

Current caveat: the pre-execution guard checks for a URL id, not for a filter inside the JSON body. For maximum compatibility with current code, prefer:

```text
PATCH /api/dynamic/{table}/{id}
PUT   /api/dynamic/{table}/{id}
```

## 16. Deleting rows

Use the id-specific endpoint:

```bash
curl -X DELETE 'http://127.0.0.1:8008/api/dynamic/products/1'
```

Current safety default:

```cpp
config.allowDeleteWithoutFilter = false;
```

So `DELETE` without a URL id is rejected unless the application explicitly enables delete-without-filter.

## 17. Table-specific endpoint rules

For table-specific routes, the URL supplies part of the envelope:

| HTTP request | Effective method | Effective table | Effective id |
|---|---|---|---|
| `GET /api/dynamic/products` | `GET` | `products` | empty |
| `GET /api/dynamic/products/1` | `GET` | `products` | `1` |
| `POST /api/dynamic/products` | `POST` | `products` | empty |
| `PATCH /api/dynamic/products/1` | `PATCH` | `products` | `1` |
| `PUT /api/dynamic/products/1` | `PUT` | `products` | `1` |
| `DELETE /api/dynamic/products/1` | `DELETE` | `products` | `1` |

The JSON body may still include `query` and `data`, but the URL table/id are authoritative when present.

## 18. Identifier rules

The current `QueryBuilder` accepts identifiers containing:

```text
A-Z a-z 0-9 _ .
```

Examples that pass identifier validation:

```text
id
products.name
orders.total_amount
```

Examples that do not pass identifier validation:

```text
COUNT(id)
price DESC; DROP TABLE products
products.name AS product_name
```

## 19. Raw URL query DSL

There is legacy support in `QueryBuilder` for URL query strings like:

```text
(table:[products];method:[GET];fields:[id,name];filters:[price>=40];orders:[price DESC];limit:10)
```

However, `DynamicQueryService` disables raw URL query DSL by default:

```cpp
config.allowRawFilter = false;
```

If raw URL query parsing is not explicitly enabled, a request that relies on raw URL query parsing returns:

```json
{
  "ok": false,
  "error": "raw_query_disabled",
  "message": "Raw URL query DSL is disabled by default"
}
```

New UI and client code should use the JSON request envelope instead.

## 20. Configuration flags that affect this contract

`DynamicApiConfig` controls several safety defaults:

```cpp
std::size_t defaultLimit = 100;
std::size_t maxLimit = 1000;

bool allowRawFilter = false;
bool allowRawJoin = false;
bool allowUpdateWithoutFilter = false;
bool allowDeleteWithoutFilter = false;
bool requirePermissions = false;
bool allowDestructiveSchemaApply = false;
```

Meaning:

| Flag | Default | Effect |
|---|---:|---|
| `defaultLimit` | `100` | Used when a structured query omits `limit`. |
| `maxLimit` | `1000` | Upper bound for `query.limit`. |
| `allowRawFilter` | `false` | Enables legacy raw URL query DSL, not JSON `query.filter`. |
| `allowRawJoin` | `false` | Enables JSON `query.join`. |
| `allowUpdateWithoutFilter` | `false` | Blocks PUT/PATCH without URL id. |
| `allowDeleteWithoutFilter` | `false` | Blocks DELETE without URL id. |
| `requirePermissions` | `false` | Enables token-based permission checks when configured. |
| `allowDestructiveSchemaApply` | `false` | Controls destructive schema apply permissions. |

## 21. Complete demo-database examples

These examples match the generated `dynamic_api_app` demo schema.

### List products

```json
{
  "method": "GET",
  "table": "products",
  "query": {
    "fields": ["id", "name", "price", "stock"],
    "limit": 25
  }
}
```

### Filter and sort products

```json
{
  "method": "GET",
  "table": "products",
  "query": {
    "fields": ["id", "name", "price", "stock"],
    "where": [
      { "field": "price", "op": "gte", "value": 40 }
    ],
    "order_by": ["price DESC"],
    "limit": 25
  }
}
```

### Read paid orders with product data

Requires `allowRawJoin=true` in the app config.

```json
{
  "method": "GET",
  "table": "orders",
  "query": {
    "join": [
      "JOIN products ON orders.product_id=products.id"
    ],
    "filter": [
      "orders.status='paid'"
    ],
    "order_by": [
      "orders.total_amount DESC"
    ],
    "limit": 10
  }
}
```

### Create a category

```json
{
  "method": "POST",
  "table": "categories",
  "data": {
    "name": "Accessories",
    "description": "Small add-ons",
    "created_at": "2026-05-08T10:00:00Z"
  }
}
```

### Create a product

```json
{
  "method": "POST",
  "table": "products",
  "data": {
    "category_id": 1,
    "name": "USB-C Hub",
    "description": "Compact 7-in-1 adapter",
    "price": 59.9,
    "stock": 35,
    "created_at": "2026-05-08T10:00:00Z"
  }
}
```

### Patch a product by id

```json
{
  "data": {
    "price": 54.9,
    "stock": 40
  }
}
```

Send it to:

```text
PATCH /api/dynamic/products/1
```

### Delete a product by id

```text
DELETE /api/dynamic/products/1
```

## 22. Recommendations for client code

Use this order when building a frontend client:

1. Call `/api/dynamic/meta/tables` to discover tables.
2. Call `/api/dynamic/meta/{table}/fields-info` to discover readable/writable/filterable/sortable fields.
3. Build JSON envelopes using structured `where` for normal filters.
4. Use table-specific id routes for updates and deletes.
5. Keep JOIN behind an explicit app-level opt-in.
6. Avoid raw URL query DSL for new clients.
7. Treat `group_by`/`having` as low-level until aggregate expression support is expanded.

## 23. Known current limitations

The current contract is useful for demos and simple CRUD/query UIs, but these limitations are important:

- No `offset` for data queries.
- `fields` does not support aliases or aggregate expressions.
- `fields` validation is base-table-centric and does not understand joined-table fields.
- `group_by`/`having` are not a complete aggregation DSL.
- Raw JOIN is disabled by default and is not relation-aware.
- Generic `PUT`/`PATCH`/`DELETE` through `POST /api/dynamic` with JSON filters is less predictable than id-specific HTTP routes because the safety guard currently checks URL id before JSON query filters are applied.
- Flat write bodies are not the recommended Dynamic API contract; wrap write values in `data`.

## 24. Future contract direction

A cleaner future contract should add:

- `POST /api/dynamic/query` as a dedicated query endpoint.
- Relation-aware structured joins instead of raw join strings.
- Expression allowlists for aggregates such as `COUNT(id)`.
- `offset` / pagination metadata.
- JSON-filter-aware safety checks for update/delete.
- First-class field aliases for joined results.
- Parameterized `where` all the way through the QueryBuilder instead of converting structured values into raw filter strings.
