# XML schema validation

`qornix_orm/schema/schema_app.xsd` is the XML contract for application schemas.
Before an XML file is converted to the internal schema model, it must pass XSD validation.

The validation layer is implemented by:

```text
qornix_orm/core/xml_schema_validator.h
qornix_orm/core/xml_schema_validator.cpp
```

## Validation flow

```text
user XML
  -> well-formed XML check
  -> root element check: <Application>
  -> schemaFormatVersion check
  -> XSD validation against qornix_orm/schema/schema_app.xsd
  -> SchemaLoader parsing
  -> later SchemaDocument conversion
```

If validation fails, ORM code must stop processing the file. Invalid XML must not reach diff, plan, or apply steps.

## Basic usage

```cpp
#include "core/xml_schema_validator.h"

XmlSchemaValidator validator("qornix_orm/schema/schema_app.xsd");

auto result = validator.validateFile("schema.xml");
if (!result.ok()) {
    for (const auto& error : result.errors) {
        std::cerr << error.code << ": " << error.message << std::endl;
    }
    return;
}
```

## Validate XML string

```cpp
XmlSchemaValidator validator("qornix_orm/schema/schema_app.xsd");

auto result = validator.validateString(xmlContent, "uploaded_schema.xml");
if (!result.ok()) {
    // return validation errors to the caller
}
```

## Result model

`XmlValidationResult` contains:

```text
valid                       true when validation passed
schemaValidationAvailable   false when qornix_orm was built without libxml2
schemaPath                  XSD file used for validation
documentPath                XML file/source name
errors                      structured validation errors
```

Each `XmlValidationError` contains:

```text
line
column
elementPath
code
message
```

## schemaFormatVersion

Current supported XML schema format:

```xml
<Application schemaFormatVersion="1.0">
```

If `schemaFormatVersion` is omitted, it is treated as `1.0` for backward compatibility.

Unsupported versions fail before XSD validation continues.

## Locating schema_app.xsd

`XmlSchemaValidator` accepts an explicit XSD path. If no path is provided, it searches common development paths and also checks this environment variable first:

```bash
export QORNIX_ORM_SCHEMA_APP_XSD=/path/to/qornix_orm/schema/schema_app.xsd
```

Applications that run outside the framework source tree should prefer passing an explicit XSD path or setting `QORNIX_ORM_SCHEMA_APP_XSD`.

## Build dependency

Runtime XSD validation uses `libxml2`.

CMake behavior:

```text
libxml2 found      -> QORNIX_HAS_LIBXML2 is enabled, XSD validation works
libxml2 not found  -> validator reports QORNIX_XSD_VALIDATION_UNAVAILABLE
```

On Ubuntu/Debian:

```bash
sudo apt-get install libxml2-dev
```

## Tests

The validator is covered by:

```text
qornix_orm/tests/xml_schema_validator_test.cpp
```

The test uses fixtures from:

```text
qornix_orm/schema/fixtures/
```

Expected behavior:

```text
valid_minimal_schema.xml             valid
valid_schema_with_relations.xml      valid
invalid_missing_required_field.xml   invalid
invalid_fk_reference.xml             invalid
invalid_duplicate_names.xml          invalid
```
