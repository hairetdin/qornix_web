# Dynamic Query API

Dynamic Query API executes schema-aware queries against entities described by XML/database metadata.

## Endpoint

```text
POST /api/dynamic/query
```

## Request

```json
{
  "from": "products",
  "select": ["id", "name", "price"],
  "where": [
    {"field": "price", "op": "gte", "value": 10}
  ],
  "orderBy": [
    {"field": "name", "direction": "asc"}
  ],
  "limit": 100,
  "offset": 0
}
```

## Safety defaults

```yaml
dynamic_api:
  query:
    default_limit: 100
    max_limit: 1000
    allow_raw_filter: false
    allow_raw_join: false
    allow_delete_without_filter: false
    allow_update_without_filter: false
```

The service validates entity names, field names and operators before query execution.
