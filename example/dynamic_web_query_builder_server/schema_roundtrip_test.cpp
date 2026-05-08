/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "demo_database.h"
#include "schema_loader.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const EntityDefinition& requireEntity(
    const std::map<std::string, EntityDefinition>& entities,
    const std::string& entityName
) {
    const auto it = entities.find(entityName);
    require(it != entities.end(), "Missing entity: " + entityName);
    return it->second;
}

void requireField(const EntityDefinition& entity, const std::string& fieldName) {
    for (const auto& field : entity.fields) {
        if (field.name == fieldName) {
            return;
        }
    }
    throw std::runtime_error("Missing field: " + entity.name + "." + fieldName);
}

void requireForeignKey(
    const EntityDefinition& entity,
    const std::string& localField,
    const std::string& referencedEntity,
    const std::string& referencedField
) {
    for (const auto& fk : entity.foreignKeys) {
        if (fk.name == localField && fk.references == referencedEntity && fk.toField == referencedField) {
            return;
        }
    }

    throw std::runtime_error(
        "Missing foreign key: " + entity.name + "." + localField + " -> " +
        referencedEntity + "." + referencedField
    );
}

void requireIndex(const EntityDefinition& entity, const std::string& fieldName) {
    for (const auto& index : entity.indexes) {
        for (const auto& indexField : index.fieldNames) {
            if (indexField == fieldName) {
                return;
            }
        }
    }
    throw std::runtime_error("Missing index for field: " + entity.name + "." + fieldName);
}

void requireConstraint(
    const EntityDefinition& entity,
    const std::string& constraintType,
    const std::string& expressionPart
) {
    for (const auto& constraint : entity.constraints) {
        if (constraint.type == constraintType &&
            constraint.expression.find(expressionPart) != std::string::npos) {
            return;
        }
    }
    throw std::runtime_error("Missing constraint on entity: " + entity.name);
}

void requireView(
    const std::vector<ViewDefinition>& views,
    const std::string& viewName,
    const std::string& queryPart
) {
    for (const auto& view : views) {
        if ((view.name == viewName || view.viewName == viewName) &&
            view.query.find(queryPart) != std::string::npos) {
            return;
        }
    }
    throw std::runtime_error("Missing view: " + viewName);
}

void requireTrigger(
    const std::vector<TriggerDefinition>& triggers,
    const std::string& triggerName,
    const std::string& tableName,
    const std::string& event,
    const std::string& timing
) {
    for (const auto& trigger : triggers) {
        if ((trigger.name == triggerName || trigger.triggerName == triggerName) &&
            trigger.tableName == tableName &&
            trigger.event == event &&
            trigger.timing == timing) {
            return;
        }
    }
    throw std::runtime_error("Missing trigger: " + triggerName);
}

void cleanupFixtures(const std::shared_ptr<DatabaseInterface>& db) {
    db->executeNonQuery("DROP TRIGGER IF EXISTS schema_roundtrip_orders_status_trigger");
    db->executeNonQuery("DROP VIEW IF EXISTS schema_roundtrip_paid_orders");
    db->executeNonQuery("DROP TABLE IF EXISTS schema_roundtrip_checks");
}

void createFixtures(const std::shared_ptr<DatabaseInterface>& db) {
    cleanupFixtures(db);
    db->executeNonQuery(R"SQL(
CREATE TABLE schema_roundtrip_checks (
    id INTEGER PRIMARY KEY,
    value INTEGER NOT NULL CHECK (value > 0)
)
)SQL");
    db->executeNonQuery(R"SQL(
CREATE VIEW schema_roundtrip_paid_orders AS
SELECT orders.id, customers.city, orders.total_amount
FROM orders
JOIN customers ON orders.customer_id = customers.id
WHERE orders.status = 'paid'
)SQL");
    db->executeNonQuery(R"SQL(
CREATE TRIGGER schema_roundtrip_orders_status_trigger
AFTER UPDATE OF status ON orders
BEGIN
    SELECT 1;
END
)SQL");
}

} // namespace

int main() {
    try {
        auto db = DatabaseInterface::init(dynamic_web_query_builder_example::demoDatabaseConfig());
        std::shared_ptr<DatabaseInterface> sharedDb(std::move(db));
        createFixtures(sharedDb);

        const auto schemaPath = dynamic_web_query_builder_example::demoSchemaPath();
        require(SchemaLoader::saveSchemaFromDatabase(sharedDb, schemaPath.string()), "Schema export failed");
        require(std::filesystem::exists(schemaPath), "Generated schema file does not exist");
        require(std::filesystem::file_size(schemaPath) > 0, "Generated schema file is empty");
        require(SchemaLoader::loadSchemaFromFile(schemaPath.string()), "Generated schema XML cannot be loaded");

        const auto& entities = SchemaLoader::getEntities();
        const auto& categories = requireEntity(entities, "categories");
        const auto& products = requireEntity(entities, "products");
        const auto& customers = requireEntity(entities, "customers");
        const auto& orders = requireEntity(entities, "orders");
        const auto& checkFixtures = requireEntity(entities, "schema_roundtrip_checks");

        requireField(categories, "id");
        requireField(categories, "name");
        requireIndex(categories, "name");

        requireField(products, "id");
        requireField(products, "category_id");
        requireField(products, "price");
        requireForeignKey(products, "category_id", "categories", "id");

        requireField(customers, "id");
        requireField(customers, "email");
        requireIndex(customers, "email");

        requireField(orders, "id");
        requireField(orders, "customer_id");
        requireField(orders, "product_id");
        requireField(orders, "total_amount");
        requireForeignKey(orders, "customer_id", "customers", "id");
        requireForeignKey(orders, "product_id", "products", "id");

        requireField(checkFixtures, "value");
        requireConstraint(checkFixtures, "CHECK", "value > 0");
        requireView(SchemaLoader::getViews(), "schema_roundtrip_paid_orders", "orders.status = 'paid'");
        requireTrigger(
            SchemaLoader::getTriggers(),
            "schema_roundtrip_orders_status_trigger",
            "orders",
            "UPDATE",
            "AFTER"
        );
        require(SchemaLoader::loadSchemaFromDB(sharedDb), "Current database schema cannot be loaded before apply");
        require(SchemaLoader::applySchemaChangesToDB(sharedDb), "Generated XML schema cannot be applied to database");

        cleanupFixtures(sharedDb);
        std::cout << "Schema round-trip test passed: " << schemaPath.string() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Schema round-trip test failed: " << e.what() << std::endl;
        return 1;
    }
}
