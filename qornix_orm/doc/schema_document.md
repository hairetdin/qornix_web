# SchemaDocument semantic model

`SchemaDocument` is the typed semantic model for XML files that conform to:

```text
qornix_orm/schema/schema_app.xsd
```

`SchemaDocument` is the semantic schema model used after runtime XSD validation.
It separates raw XML from the internal model that future normalization, semantic diff, plan and apply code will use.

## Why SchemaDocument exists

Qornix ORM must not compare raw user XML directly with a raw database snapshot.
User XML can differ from generated database XML in formatting, ordering, omitted default attributes, driver-specific metadata and intent.

The intended pipeline is:

```text
user XML
  -> validate against schema_app.xsd
  -> parse to SchemaDocument
  -> normalize
  -> desired SchemaDocument

database
  -> introspect
  -> DatabaseSnapshot
  -> export to SchemaDocument
  -> normalize
  -> current SchemaDocument

desired SchemaDocument
  -> semantic diff
  -> current SchemaDocument
```

`DatabaseSnapshot` remains useful as the technical result of database introspection, but the semantic comparison should happen between `SchemaDocument desired` and `SchemaDocument current`.

## Source metadata

`SchemaDocument` tracks where the model came from:

```cpp
SchemaDocumentSourceType::UploadedXml
SchemaDocumentSourceType::DatabaseExport
SchemaDocumentSourceType::Generated
SchemaDocumentSourceType::Unknown
```

This keeps these artifacts separate:

```text
uploaded user XML
validated SchemaDocument
canonical XML generated from SchemaDocument
database-exported XML
future normalized desired/current models
```

A database export must not overwrite the user's uploaded XML automatically.

## Public API

```cpp
#include "core/schema_document.h"

const auto result = SchemaDocument::loadFromFile(
    "schema.xml",
    "qornix_orm/schema/schema_app.xsd",
    SchemaDocumentSourceType::UploadedXml
);

if (!result.ok()) {
    for (const auto& error : result.errors) {
        std::cerr << error.code << ": " << error.message << std::endl;
    }
    return;
}

const auto& document = *result.document;
```

Load from string:

```cpp
auto result = SchemaDocument::parseString(
    xmlContent,
    "uploaded_schema.xml",
    "qornix_orm/schema/schema_app.xsd",
    SchemaDocumentSourceType::UploadedXml
);
```

Generate canonical XML:

```cpp
std::string canonicalXml = document.toCanonicalXml();
document.saveCanonicalXml("schema.canonical.xml");
```

Convert to the legacy snapshot structure:

```cpp
SchemaSnapshot snapshot = document.toSnapshot();
```

## Current scope

`SchemaDocument` covers:

- XSD-valid XML -> `SchemaDocument`;
- source metadata;
- typed access to configuration, entities, views, materialized views, triggers, functions, procedures, sequences and database mapping;
- canonical XML serialization;
- round-trip validation tests;
- conversion to `SchemaSnapshot` for compatibility with existing ORM code.

`SchemaDocument` does not implement semantic normalization, database introspection, diff, plan or apply. Those are handled by separate layers.

## Tests

The test file is:

```text
qornix_orm/tests/schema_document_test.cpp
```

It checks:

- valid minimal schema loads as `SchemaDocument`;
- valid schema with relations loads entities and foreign keys;
- canonical XML is valid against `schema_app.xsd`;
- canonical XML can be parsed back into `SchemaDocument`;
- invalid XML is rejected before a `SchemaDocument` is created.

## Relationship with SchemaLoader

`SchemaLoader::loadSchemaFromFile(...)` remains the existing loader for legacy global ORM state.

`SchemaDocument` is the new semantic model for the upcoming schema-core pipeline:

```text
SchemaDocument -> normalization -> semantic diff -> schema plan -> apply
```

Future work can gradually migrate `SchemaLoader` internals to use `SchemaDocument` instead of duplicating XML parsing logic.
