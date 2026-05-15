# Schema normalization

`SchemaNormalizer` prepares validated schema models for semantic diff.

The normalizer works with `SchemaDocument`, not with raw XML text:

```text
user XML
  -> validate against qornix_orm/schema/schema_app.xsd
  -> SchemaDocument
  -> SchemaNormalizer
  -> normalized SchemaDocument desired
```

For database export the intended flow is similar:

```text
database
  -> DatabaseSnapshot
  -> SchemaDocument current
  -> SchemaNormalizer
  -> normalized SchemaDocument current
```

The future diff layer should compare two normalized semantic models:

```text
SchemaDocument desired
SchemaDocument current
```

It should not compare raw user XML with a raw database snapshot.

---

## Source files

```text
qornix_orm/core/schema_normalizer.h
qornix_orm/core/schema_normalizer.cpp
```

---

## Important rule

Normalization does not overwrite the uploaded XML schema.

It returns a copy:

```cpp
SchemaNormalizationResult normalized = SchemaNormalizer::normalize(document);
```

The original `SchemaDocument` remains unchanged. This keeps these artifacts separate:

```text
uploaded XML schema       user intent
validated SchemaDocument  parsed user intent
normalized SchemaDocument semantic model for diff
exported DB schema        canonical schema generated from DB
```

---

## What is normalized

Normalization is intentionally conservative and safe.

Current rules:

- trim identifier whitespace;
- keep field/column order inside an entity;
- sort comparable top-level objects for stable diff;
- sort indexes/constraints/functions inside an entity;
- normalize common type aliases;
- normalize default values such as `NULL` and `(NULL)`;
- normalize keyword-like values such as `INFO`, `BTREE`, `CASCADE`;
- normalize database engine names to lowercase;
- preserve quoted identifiers and return warnings for them.

---

## What is not normalized yet

The following areas are intentionally left for future driver-capability and semantic-diff improvements:

- advanced PostgreSQL type equivalence;
- driver-specific precision/scale rules;
- SQLite table rebuild semantics;
- expression equivalence for indexes/checks/views;
- full SQL parser based comparison;
- destructive operation decisions.

---

## Example

```cpp
#include "core/schema_document.h"
#include "core/schema_normalizer.h"

auto parsed = SchemaDocument::loadFromFile(
    "schema.xml",
    "qornix_orm/schema/schema_app.xsd",
    SchemaDocumentSourceType::UploadedXml
);

if (!parsed.ok()) {
    return;
}

SchemaNormalizationResult normalized = SchemaNormalizer::normalize(*parsed.document);

if (normalized.hasWarnings()) {
    for (const auto& warning : normalized.warnings) {
        std::cerr << warning.code << ": " << warning.message << std::endl;
    }
}

std::string canonicalXml = normalized.document.toCanonicalXml();
```

---

## Testing

The normalizer adds:

```text
qornix_orm/tests/schema_normalizer_test.cpp
qornix_orm/schema/fixtures/valid_normalization_input.xml
```

The test checks that:

- the original document remains unchanged;
- normalization returns a separate copy;
- entity ordering becomes stable;
- field order inside an entity is preserved;
- identifiers and default values are normalized;
- canonical XML contains normalized values.

