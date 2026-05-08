/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// entity_base.h
#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <stdexcept>
#include "app_struct.h"
#include "database_interface.h"

class EntityBase {
private:
    bool executeCppFunction(const FunctionDefinition &funcDef,
                            const std::map<std::string, std::string> &params);

    bool executePythonFunction(const FunctionDefinition &funcDef,
                               const std::map<std::string, std::string> &params);

protected:
    std::shared_ptr<DatabaseInterface> db_;
    EntityDefinition entityDef_;
    std::map<std::string, std::string> values_;

    virtual std::string buildCreateTableQuery() const;

public:
    EntityBase(std::shared_ptr<DatabaseInterface> db, const EntityDefinition &definition);

    // Method for adding an entity to SchemaLoader
    bool addToSchemaLoader();

    TableManager objects() {
        return db_->table(entityDef_.tableName);
    }

    std::string getPrimaryKeyFieldName() const;

    bool hasField(const std::string &fieldName) const;

    // Methods for working with entity
    EntityDefinition getEntityDefinition() const;

    static EntityDefinition newEntity(const std::string &tableName);
    bool deleteEntity();
    bool removeEntity() { return deleteEntity(); };
    bool saveEntity();

    void setVerboseName(const std::string& verboseName) { entityDef_.verboseName = verboseName; }
    void setVerboseNamePlural(const std::string& verboseNamePlural) { entityDef_.verboseNamePlural = verboseNamePlural; }
    void setDescription(const std::string& description) { entityDef_.description = description; }

    // Index operations
    void addIndex(const IndexDefinition &index);

    std::vector<IndexDefinition> getIndexes() const;

    // Constraint operations
    void addConstraint(const ConstraintDefinition &constraint);

    std::vector<ConstraintDefinition> getConstraints() const;

    // EntityFunctions support
    void addEntityFunction(const FunctionDefinition &function);

    std::vector<FunctionDefinition> getEntityFunctions() const;

    bool executeEntityFunction(const std::string &functionName,
                               const std::map<std::string, std::string> &params);

    bool validateIndexes() const;

    // Entity metadata accessors
    std::string getName() const;

    std::string getTableName() const;

    std::string getVerboseName() const;

    std::string getVerboseNamePlural() const;

    std::string getDescription() const;


    // Entity field accessors
    const std::vector<FieldDefinition> &getFields() const;

    const FieldDefinition *getField(const std::string &name) const;

    // Method for adding a field using a map of attributes
    void addField(const std::string &name, const std::map<std::string, std::string> &attributes);

    // Method for updating a field using a map of attributes
    bool updateField(const std::string &name, const std::map<std::string, std::string> &attributes);

    bool deleteField(const std::string &name);
    bool removeField(const std::string &name) { return deleteField(name); };

    // Method for getting field attributes as a map
    std::map<std::string, std::string> getFieldAttributes(const std::string &name) const;

    // Methods for working with foreign keys
    void addForeignKey(const std::string &name,
                       const std::map<std::string, std::string> &attributes);

    bool updateForeignKey(const std::string &name,
                          const std::map<std::string, std::string> &attributes);

    bool removeForeignKey(const std::string &name);

    const std::vector<ForeignKeyField> &getForeignKeys() const;

    const ForeignKeyField *getForeignKey(const std::string &name) const;
};
