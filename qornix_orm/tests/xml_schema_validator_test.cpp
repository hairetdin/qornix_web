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
#include <vector>

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

void assertValidFile(XmlSchemaValidator& validator, const std::string& fixtureName) {
    const auto result = validator.validateFile(fixturePath(fixtureName).string());
    if (!result.ok()) {
        std::cerr << "Expected valid fixture to pass: " << fixtureName << "\n";
        for (const auto& error : result.errors) {
            std::cerr << "  line " << error.line << ": " << error.message << "\n";
        }
    }
    assert(result.ok());
}

void assertInvalidFile(XmlSchemaValidator& validator, const std::string& fixtureName) {
    const auto result = validator.validateFile(fixturePath(fixtureName).string());
    if (result.ok()) {
        std::cerr << "Expected invalid fixture to fail: " << fixtureName << "\n";
    }
    assert(!result.ok());
    assert(!result.errors.empty());
}

void test_valid_fixtures() {
    std::cout << "  [1/4] valid XML fixtures against schema_app.xsd ... ";
    XmlSchemaValidator validator(schemaPath().string());

    assertValidFile(validator, "valid_minimal_schema.xml");
    assertValidFile(validator, "valid_schema_with_relations.xml");

    std::cout << "OK" << std::endl;
}

void test_invalid_fixtures() {
    std::cout << "  [2/4] invalid XML fixtures against schema_app.xsd ... ";
    XmlSchemaValidator validator(schemaPath().string());

    assertInvalidFile(validator, "invalid_missing_required_field.xml");
    assertInvalidFile(validator, "invalid_fk_reference.xml");
    assertInvalidFile(validator, "invalid_duplicate_names.xml");

    std::cout << "OK" << std::endl;
}

void test_validate_string() {
    std::cout << "  [3/4] validate XML string ... ";
    XmlSchemaValidator validator(schemaPath().string());

    const std::string xml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="1.0">
    <Name>Inline Schema</Name>
    <Version>1.0.0</Version>
    <Configuration>
        <DbConnectionString>sqlite://:memory:</DbConnectionString>
        <LogLevel>INFO</LogLevel>
        <Timeout>30</Timeout>
        <DatabaseEngine>sqlite</DatabaseEngine>
    </Configuration>
    <DataStructure/>
</Application>)XML";

    const auto result = validator.validateString(xml, "inline_valid_schema.xml");
    assert(result.ok());

    std::cout << "OK" << std::endl;
}

void test_unsupported_schema_version() {
    std::cout << "  [4/4] unsupported schemaFormatVersion ... ";
    XmlSchemaValidator validator(schemaPath().string());

    const std::string xml = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="999.0">
    <Name>Unsupported Version</Name>
    <Version>1.0.0</Version>
    <Configuration>
        <DbConnectionString>sqlite://:memory:</DbConnectionString>
        <LogLevel>INFO</LogLevel>
        <Timeout>30</Timeout>
        <DatabaseEngine>sqlite</DatabaseEngine>
    </Configuration>
    <DataStructure/>
</Application>)XML";

    const auto result = validator.validateString(xml, "unsupported_version.xml");
    assert(!result.ok());
    assert(!result.errors.empty());
    assert(result.errors.front().code == "QORNIX_SCHEMA_VERSION_UNSUPPORTED");

    std::cout << "OK" << std::endl;
}

} // namespace

int main() {
    std::cout << "Running qornix_orm XML schema validator tests..." << std::endl;

    test_valid_fixtures();
    test_invalid_fixtures();
    test_validate_string();
    test_unsupported_schema_version();

    std::cout << "All XML schema validator tests passed" << std::endl;
    return 0;
}
