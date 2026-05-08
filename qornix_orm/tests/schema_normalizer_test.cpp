#include "core/schema_document.h"
#include "core/schema_normalizer.h"

#include <cassert>
#include <string>

#ifndef QORNIX_ORM_SOURCE_DIR
#define QORNIX_ORM_SOURCE_DIR "."
#endif

namespace {

std::string sourcePath(const std::string& relativePath) {
    return std::string(QORNIX_ORM_SOURCE_DIR) + "/" + relativePath;
}

} // namespace

int main() {
    const auto xsdPath = sourcePath("schema/schema_app.xsd");
    const auto xmlPath = sourcePath("schema/fixtures/valid_normalization_input.xml");

    auto parsed = SchemaDocument::loadFromFile(xmlPath, xsdPath);
    assert(parsed.ok());
    assert(parsed.document != nullptr);

    const auto& original = *parsed.document;
    assert(original.entities().size() == 2);
    assert(original.entities()[0].name == " Product ");
    assert(original.entities()[0].tableName == " products ");

    auto normalized = SchemaNormalizer::normalize(original);
    const auto& normalizedDoc = normalized.document;

    // The input object must remain unchanged; normalization produces a copy.
    assert(original.entities()[0].name == " Product ");
    assert(original.configuration().databaseEngine == "sqlite");

    assert(normalizedDoc.configuration().databaseEngine == "sqlite");
    assert(normalizedDoc.configuration().logLevel == "INFO");

    // Comparable top-level objects are sorted by table name/name for stable semantic diff.
    assert(normalizedDoc.entities().size() == 2);
    assert(normalizedDoc.entities()[0].name == "Category");
    assert(normalizedDoc.entities()[0].tableName == "categories");
    assert(normalizedDoc.entities()[1].name == "Product");
    assert(normalizedDoc.entities()[1].tableName == "products");

    // Column order is preserved inside an entity, but identifiers/defaults are normalized.
    const auto& product = normalizedDoc.entities()[1];
    assert(product.fields.size() == 2);
    assert(product.fields[0].name == "id");
    assert(product.fields[1].name == "name");
    assert(product.fields[1].defaultValue.empty());

    assert(product.indexes.size() == 1);
    assert(product.indexes[0].name == "idx_products_name");
    assert(product.indexes[0].fieldNames.size() == 1);
    assert(product.indexes[0].fieldNames[0] == "name");
    assert(product.indexes[0].type == "BTREE");

    // Canonical XML generated from the normalized document should preserve normalized values.
    const auto canonicalXml = normalizedDoc.toCanonicalXml();
    assert(canonicalXml.find("tableName=\"categories\"") != std::string::npos);
    assert(canonicalXml.find("tableName=\"products\"") != std::string::npos);
    assert(canonicalXml.find("LogLevel>INFO</LogLevel") != std::string::npos);
    assert(canonicalXml.find("DatabaseEngine>sqlite</DatabaseEngine") != std::string::npos);

    return 0;
}
