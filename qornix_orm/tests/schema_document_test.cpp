/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#include "core/schema_document.h"
#include "core/xml_schema_validator.h"

#ifndef QORNIX_ORM_SOURCE_DIR
#define QORNIX_ORM_SOURCE_DIR "."
#endif

namespace {

std::filesystem::path sourceDir() {
    return std::filesystem::path(QORNIX_ORM_SOURCE_DIR);
}

std::filesystem::path schemaPath() {
    return sourceDir() / "schema" / "schema_app.xsd";
}

std::filesystem::path fixturePath(const std::string& name) {
    return sourceDir() / "schema" / "fixtures" / name;
}

void printErrors(const SchemaDocumentParseResult& result) {
    for (const auto& error : result.errors) {
        std::cerr << "  [" << error.code << "]";
        if (error.line != 0) {
            std::cerr << " line " << error.line;
        }
        if (!error.elementPath.empty()) {
            std::cerr << " path " << error.elementPath;
        }
        std::cerr << ": " << error.message << "\n";
    }
}

void test_load_minimal_schema() {
    std::cout << "  [1/4] load minimal SchemaDocument ... ";

    auto result = SchemaDocument::loadFromFile(
        fixturePath("valid_minimal_schema.xml").string(),
        schemaPath().string()
    );

    if (!result.ok()) {
        printErrors(result);
    }

    assert(result.ok());
    assert(result.document != nullptr);
    assert(result.document->metadata().name == "Minimal Schema");
    assert(result.document->sourceMetadata().schemaFormatVersion == "1.0");
    assert(result.document->entities().empty());
    assert(result.document->configuration().databaseEngine == "sqlite");

    std::cout << "OK" << std::endl;
}

void test_load_schema_with_relations() {
    std::cout << "  [2/4] load SchemaDocument with entities and FK ... ";

    auto result = SchemaDocument::loadFromFile(
        fixturePath("valid_schema_with_relations.xml").string(),
        schemaPath().string()
    );

    if (!result.ok()) {
        printErrors(result);
    }

    assert(result.ok());
    assert(result.document != nullptr);
    assert(result.document->entities().size() == 2);

    const auto entities = result.document->entityMapByName();
    assert(entities.count("Category") == 1);
    assert(entities.count("Product") == 1);
    assert(entities.at("Product").foreignKeys.size() == 1);
    assert(entities.at("Product").foreignKeys.front().references == "categories");

    const auto snapshot = result.document->toSnapshot();
    assert(snapshot.entities.count("Category") == 1);
    assert(snapshot.entities.count("Product") == 1);

    std::cout << "OK" << std::endl;
}

void test_canonical_xml_round_trip() {
    std::cout << "  [3/4] canonical XML round-trip ... ";

    auto result = SchemaDocument::loadFromFile(
        fixturePath("valid_schema_with_relations.xml").string(),
        schemaPath().string()
    );
    assert(result.ok());
    assert(result.document != nullptr);

    const auto canonicalXml = result.document->toCanonicalXml();
    assert(canonicalXml.find("<Application") != std::string::npos);
    assert(canonicalXml.find("schemaFormatVersion=\"1.0\"") != std::string::npos);
    assert(canonicalXml.find("<Entity") != std::string::npos);

    XmlSchemaValidator validator(schemaPath().string());
    const auto validation = validator.validateString(canonicalXml, "canonical_round_trip.xml");
    if (!validation.ok()) {
        for (const auto& error : validation.errors) {
            std::cerr << "  line " << error.line << ": " << error.message << "\n";
        }
    }
    assert(validation.ok());

    auto roundTrip = SchemaDocument::parseString(
        canonicalXml,
        "canonical_round_trip.xml",
        schemaPath().string(),
        SchemaDocumentSourceType::Generated
    );
    assert(roundTrip.ok());
    assert(roundTrip.document != nullptr);
    assert(roundTrip.document->entities().size() == result.document->entities().size());
    assert(roundTrip.document->sourceMetadata().type == SchemaDocumentSourceType::Generated);

    std::cout << "OK" << std::endl;
}

void test_invalid_xml_rejected() {
    std::cout << "  [4/4] invalid XML is rejected before SchemaDocument ... ";

    auto result = SchemaDocument::loadFromFile(
        fixturePath("invalid_fk_reference.xml").string(),
        schemaPath().string()
    );

    assert(!result.ok());
    assert(result.document == nullptr);
    assert(!result.errors.empty());

    std::cout << "OK" << std::endl;
}

} // namespace

int main() {
    std::cout << "Running qornix_orm SchemaDocument tests..." << std::endl;

    test_load_minimal_schema();
    test_load_schema_with_relations();
    test_canonical_xml_round_trip();
    test_invalid_xml_rejected();

    std::cout << "All SchemaDocument tests passed" << std::endl;
    return 0;
}
