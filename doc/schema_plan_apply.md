# Schema Plan and Apply

Qornix Web does not treat XML upload as a direct migration command.

The intended flow is:

```text
XML -> validation -> SchemaDocument -> normalized desired model
DB  -> DatabaseSnapshot -> current SchemaDocument
models -> semantic diff -> risk classification -> SchemaPlan -> SQL preview -> apply -> history
```

## Safety profile

- SQL preview is visible before apply.
- Destructive operations are labeled.
- Unsupported operations stay out of the executable plan.
- History records applied plans.
- Permissions can separate safe apply and destructive apply.
