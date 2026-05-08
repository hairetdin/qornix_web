# Schema Manager

Schema Manager is the UI and API workflow for controlled database schema changes.

## API workflow

```text
GET  /api/dynamic/schema/export
POST /api/dynamic/schema/validate
POST /api/dynamic/schema/diff
POST /api/dynamic/schema/plan
POST /api/dynamic/schema/apply
GET  /api/dynamic/schema/history
```

`apply` works only with a previously built plan. Direct raw XML apply is not part of the productized workflow.

## UI workflow

```text
Load current DB schema
  -> edit/upload desired XML
  -> validate
  -> inspect diff
  -> inspect risk labels
  -> inspect SQL preview
  -> confirm plan
  -> apply
  -> inspect history
```
