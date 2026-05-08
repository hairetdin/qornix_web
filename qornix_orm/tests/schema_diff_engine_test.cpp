/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "core/schema_diff_engine.h"

#include <cassert>
#include <iostream>
#include <string>

#ifndef QORNIX_ORM_SOURCE_DIR
#define QORNIX_ORM_SOURCE_DIR "."
#endif

namespace {

std::string fixturePath(const std::string& relativePath) {
    return std::string(QORNIX_ORM_SOURCE_DIR) + "/" + relativePath;
}

std::string minimalXml() {
    return R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="1.0">
    <Name>Minimal Schema</Name>
    <Version>1.0.0</Version>
    <Description>Minimal valid Qornix ORM application schema.</Description>
    <Configuration>
        <DbConnectionString>sqlite://:memory:</DbConnectionString>
        <LogLevel>INFO</LogLevel>
        <Timeout>30</Timeout>
        <DatabaseEngine>sqlite</DatabaseEngine>
    </Configuration>
    <DataStructure/>
</Application>)XML";
}

std::string productXml(const std::string& nameMaxLength, bool withSku, bool withNameIndex) {
    std::string sku = withSku ? R"XML(
            <Field name="sku" type="VARCHAR" maxLength="64" nullable="true"/>)XML" : "";
    std::string index = withNameIndex ? R"XML(
            <Index name="idx_products_name" unique="false">
                <FieldName>name</FieldName>
            </Index>)XML" : "";

    return R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="1.0">
    <Name>Product Schema</Name>
    <Version>1.0.0</Version>
    <Description>Product schema for semantic diff tests.</Description>
    <Configuration>
        <DbConnectionString>sqlite://:memory:</DbConnectionString>
        <LogLevel>INFO</LogLevel>
        <Timeout>30</Timeout>
        <DatabaseEngine>sqlite</DatabaseEngine>
    </Configuration>
    <DataStructure>
        <Entity name="Product" tableName="products">
            <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true"/>
            <Field name="name" type="VARCHAR" maxLength=")XML" + nameMaxLength + R"XML(" nullable="false"/>)XML" + sku + index + R"XML(
        </Entity>
    </DataStructure>
</Application>)XML";
}

std::string relationXml(const std::string& onDelete) {
    return R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="1.0">
    <Name>Relation Schema</Name>
    <Version>1.0.0</Version>
    <Description>Relation schema for semantic diff tests.</Description>
    <Configuration>
        <DbConnectionString>sqlite://:memory:</DbConnectionString>
        <LogLevel>INFO</LogLevel>
        <Timeout>30</Timeout>
        <DatabaseEngine>sqlite</DatabaseEngine>
    </Configuration>
    <DataStructure>
        <Entity name="Category" tableName="categories">
            <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true"/>
        </Entity>
        <Entity name="Product" tableName="products">
            <Field name="id" type="INTEGER" primaryKey="true" autoIncrement="true"/>
            <ForeignKeyField name="category_id" type="INTEGER" nullable="false" references="categories" toField="id" onDelete=")XML" + onDelete + R"XML(" onUpdate="CASCADE"/>
        </Entity>
    </DataStructure>
</Application>)XML";
}

SchemaDocument parseXml(const std::string& xml, const std::string& name) {
    auto parsed = SchemaDocument::parseString(
        xml,
        name,
        fixturePath("schema/schema_app.xsd"),
        SchemaDocumentSourceType::UploadedXml
    );

    if (!parsed.ok()) {
        for (const auto& error : parsed.errors) {
            std::cerr << error.code << ": " << error.message << std::endl;
        }
    }

    assert(parsed.ok());
    return *parsed.document;
}

bool hasKind(const SchemaDocumentDiff& diff, SchemaDiffOperationKind kind) {
    return !diff.operationsByKind(kind).empty();
}

void testEmptyDiff() {
    auto desired = SchemaDocument::loadFromFile(
        fixturePath("schema/fixtures/valid_schema_with_relations.xml"),
        fixturePath("schema/schema_app.xsd"),
        SchemaDocumentSourceType::UploadedXml
    );
    assert(desired.ok());

    auto current = SchemaDocument::loadFromFile(
        fixturePath("schema/fixtures/valid_schema_with_relations.xml"),
        fixturePath("schema/schema_app.xsd"),
        SchemaDocumentSourceType::DatabaseExport
    );
    assert(current.ok());

    const auto diff = SchemaDiffEngine::compare(*desired.document, *current.document);
    assert(!diff.hasChanges());
}

void testTableAddedAndOnlyInCurrent() {
    const auto desired = parseXml(productXml("160", false, false), "desired_product");
    const auto current = parseXml(minimalXml(), "current_minimal");

    const auto addedDiff = SchemaDiffEngine::compare(desired, current);
    assert(hasKind(addedDiff, SchemaDiffOperationKind::TableAdded));

    const auto onlyCurrentDiff = SchemaDiffEngine::compare(current, desired);
    assert(hasKind(onlyCurrentDiff, SchemaDiffOperationKind::TableOnlyInCurrent));
}

void testColumnDiffs() {
    const auto desired = parseXml(productXml("200", false, false), "desired_product_name_200");
    const auto current = parseXml(productXml("160", true, false), "current_product_name_160_with_sku");

    const auto diff = SchemaDiffEngine::compare(desired, current);
    assert(hasKind(diff, SchemaDiffOperationKind::ColumnChanged));
    assert(hasKind(diff, SchemaDiffOperationKind::ColumnOnlyInCurrent));

    const auto reversed = SchemaDiffEngine::compare(current, desired);
    assert(hasKind(reversed, SchemaDiffOperationKind::ColumnAdded));
}

void testIndexDiffs() {
    const auto desired = parseXml(productXml("160", false, true), "desired_product_with_index");
    const auto current = parseXml(productXml("160", false, false), "current_product_without_index");

    const auto diff = SchemaDiffEngine::compare(desired, current);
    assert(hasKind(diff, SchemaDiffOperationKind::IndexAdded));
}

void testForeignKeyDiffs() {
    const auto desired = parseXml(relationXml("CASCADE"), "desired_relation_cascade");
    const auto current = parseXml(relationXml("RESTRICT"), "current_relation_restrict");

    const auto diff = SchemaDiffEngine::compare(desired, current);
    assert(hasKind(diff, SchemaDiffOperationKind::ForeignKeyChanged));
}

void testMachineReadableOutput() {
    const auto desired = parseXml(productXml("200", false, false), "desired_json_output");
    const auto current = parseXml(productXml("160", false, false), "current_json_output");

    const auto diff = SchemaDiffEngine::compare(desired, current);
    const auto json = diff.toJsonString();
    const auto text = diff.toText();

    assert(json.find("\"hasChanges\":true") != std::string::npos);
    assert(json.find("column_changed") != std::string::npos);
    assert(text.find("column_changed") != std::string::npos);
}

} // namespace

int main() {
    testEmptyDiff();
    testTableAddedAndOnlyInCurrent();
    testColumnDiffs();
    testIndexDiffs();
    testForeignKeyDiffs();
    testMachineReadableOutput();

    std::cout << "schema_diff_engine_test passed" << std::endl;
    return 0;
}
