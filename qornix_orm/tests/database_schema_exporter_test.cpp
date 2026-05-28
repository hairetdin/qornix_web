/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <cassert>
#include <iostream>
#include <string>

#include "core/database_schema_exporter.h"
#include "core/xml_schema_validator.h"

namespace {

DatabaseSnapshot buildSnapshot() {
    DatabaseSnapshot snapshot;
    snapshot.metadata.driverName = "sqlite";
    snapshot.metadata.databaseName = "shop";
    snapshot.metadata.connectionName = ":memory:";
    snapshot.metadata.capturedAt = "2026-05-06T00:00:00Z";

    snapshot.tables.push_back(DbTable{"categories", "", "table", "CREATE TABLE categories (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", {}});
    snapshot.tables.push_back(DbTable{"products", "", "table", "CREATE TABLE products (id INTEGER PRIMARY KEY, category_id INTEGER, name TEXT NOT NULL)", {}});

    snapshot.columns.push_back(DbColumn{"categories", "id", "INTEGER", false, true, true, false, "", "", "", 0});
    snapshot.columns.push_back(DbColumn{"categories", "name", "TEXT", false, false, false, false, "", "", "", 1});
    snapshot.columns.push_back(DbColumn{"products", "id", "INTEGER", false, true, true, false, "", "", "", 0});
    snapshot.columns.push_back(DbColumn{"products", "category_id", "INTEGER", true, false, false, false, "", "", "", 1});
    snapshot.columns.push_back(DbColumn{"products", "name", "VARCHAR(120)", false, false, false, false, "", "", "", 2});

    DbPrimaryKey categoriesPk;
    categoriesPk.tableName = "categories";
    categoriesPk.name = "pk_categories";
    categoriesPk.columnNames = {"id"};
    snapshot.primaryKeys.push_back(categoriesPk);

    DbPrimaryKey productsPk;
    productsPk.tableName = "products";
    productsPk.name = "pk_products";
    productsPk.columnNames = {"id"};
    snapshot.primaryKeys.push_back(productsPk);

    DbForeignKey fk;
    fk.tableName = "products";
    fk.name = "fk_products_category";
    fk.columnName = "category_id";
    fk.referencedTableName = "categories";
    fk.referencedColumnName = "id";
    fk.onDelete = "CASCADE";
    fk.onUpdate = "NO ACTION";
    snapshot.foreignKeys.push_back(fk);

    DbIndex index;
    index.tableName = "products";
    index.name = "idx_products_name";
    index.columnNames = {"name"};
    index.unique = false;
    index.type = "BTREE";
    snapshot.indexes.push_back(index);

    DbView view;
    view.name = "active_products";
    view.definition = "SELECT id, name FROM products";
    snapshot.views.push_back(view);

    return snapshot;
}

} // namespace

int main() {
    DatabaseSchemaExportOptions options;
    options.applicationName = "ShopSchema";
    options.applicationVersion = "1.0";
    options.description = "Database export test";

    const auto snapshot = buildSnapshot();
    auto exportResult = DatabaseSchemaExporter::exportSnapshot(snapshot, options);
    assert(exportResult.ok());
    assert(exportResult.document != nullptr);

    const auto& document = *exportResult.document;
    assert(document.sourceMetadata().type == SchemaDocumentSourceType::DatabaseExport);
    assert(document.sourceMetadata().databaseEngine == "sqlite");
    assert(document.metadata().name == "ShopSchema");
    assert(document.configuration().databaseEngine == "sqlite");
    assert(document.entities().size() == 2);
    assert(document.views().size() == 1);

    const auto entityMap = document.entityMapByName();
    assert(entityMap.count("Categories") == 1);
    assert(entityMap.count("Products") == 1);

    const auto products = entityMap.at("Products");
    assert(products.tableName == "products");
    assert(products.fields.size() == 2);       // id, name
    assert(products.foreignKeys.size() == 1);  // category_id
    assert(products.indexes.size() == 1);
    assert(products.foreignKeys.front().references == "categories");
    assert(products.foreignKeys.front().toField == "id");

    const auto xml = document.toCanonicalXml();
    assert(xml.find("schemaFormatVersion=\"1.0\"") != std::string::npos);
    assert(xml.find("tableName=\"products\"") != std::string::npos);
    assert(xml.find("ForeignKeyField") != std::string::npos);
    assert(xml.find("idx_products_name") != std::string::npos);

#ifdef QORNIX_HAS_LIBXML2
    const std::string xsdPath = std::string(QORNIX_ORM_SOURCE_DIR) + "/schema/schema_app.xsd";
    XmlSchemaValidator validator(xsdPath);
    const auto validation = validator.validateString(xml, "database_schema_exporter_test.xml");
    if (!validation.ok()) {
        std::cerr << validation.summary() << std::endl;
        for (const auto& error : validation.errors) {
            std::cerr << error.code << ": " << error.message << std::endl;
        }
    }
    assert(validation.ok());
#endif

    std::cout << "database_schema_exporter_test passed" << std::endl;
    return 0;
}
