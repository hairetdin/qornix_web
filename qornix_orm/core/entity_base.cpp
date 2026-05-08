/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// entity_base.cpp
#include "entity_base.h"
#include <sstream>
#include <algorithm>
#include <iostream>
#include <dlfcn.h> // For dynamic loading of C++ functions

#include "schema_loader.h"
#ifdef PYTHON_SUPPORT
#include <Python.h> // For Python function support
#endif

EntityBase::EntityBase(std::shared_ptr<DatabaseInterface> db,
                       const EntityDefinition& entityDef)
    : db_(db), entityDef_(entityDef) {
    // Set default values from schema
    for (const auto& field : entityDef_.fields) {
        if (!field.defaultValue.empty()) {
            values_[field.name] = field.defaultValue;
        }
    }
}

// Methods for working with entity
EntityDefinition EntityBase::getEntityDefinition() const {
    return entityDef_;
}

EntityDefinition EntityBase::newEntity(const std::string& tableName) {
    EntityDefinition entityDef;
    entityDef.tableName = tableName;
    entityDef.name = tableName;  // Set the default name to the table name

    // Initialize the remaining fields with default values
    entityDef.verboseName = tableName;
    entityDef.verboseNamePlural = tableName + "s";
    entityDef.description = "Auto-generated entity for table: " + tableName;

    return entityDef;
}

bool EntityBase::saveEntity() {
    try {
        if (!db_) {
            std::cerr << "EntityBase::saveEntity() - Database interface is null" << std::endl;
            return false;
        }

        if (!db_->isConnected()) {
            std::cerr << "EntityBase::saveEntity() - Database is not connected" << std::endl;
            return false;
        }

        // Update all reverse relations
        auto& schemaEntities = const_cast<std::map<std::string, EntityDefinition>&>(SchemaLoader::getEntities());
        schemaEntities[entityDef_.name] = entityDef_;
        SchemaLoader::processEntityReverseRelationships(entityDef_, schemaEntities);

        // Apply schema changes to the database
        if (!SchemaLoader::applySchemaChangesToDB(db_)) {
            std::cerr << "EntityBase::saveEntity() - Failed to apply schema changes to database" << std::endl;
            return false;
        }

        std::cout << "Entity saved successfully to schema: " << entityDef_.name << std::endl;

        return true;
    } catch (const std::exception& e) {
        std::cerr << "EntityBase::saveEntity() - Error: " << e.what() << std::endl;
        return false;
    }
}

bool EntityBase::deleteEntity() {
    try {

        // Check whether the entity exists in the schema
        const auto& schemaEntities = SchemaLoader::getEntities();
        auto entityIt = schemaEntities.find(entityDef_.name);

        if (entityIt == schemaEntities.end()) {
            std::cerr << "EntityBase::deleteEntity() - Entity does not exist in schema: " << entityDef_.name << std::endl;
            return false;
        }

        // Remove the entity from SchemaLoader
        auto& mutableSchemaEntities = const_cast<std::map<std::string, EntityDefinition>&>(SchemaLoader::getEntities());
        mutableSchemaEntities.erase(entityDef_.name);

        // Apply schema changes to the database
        if (!SchemaLoader::applySchemaChangesToDB(db_)) {
            std::cerr << "EntityBase::deleteEntity() - Failed to apply schema changes to database" << std::endl;
            return false;
        }

        // Remove from entities loaded from the database
        auto& mutableSchemaDbEntities = const_cast<std::map<std::string, EntityDefinition>&>(SchemaLoader::getDbEntities());
        mutableSchemaDbEntities.erase(entityDef_.name);

        std::cout << "Entity deleted successfully from schema: " << entityDef_.name << std::endl;

        return true;
    } catch (const std::exception& e) {
        std::cerr << "EntityBase::deleteEntity() - Error: " << e.what() << std::endl;
        return false;
    }
}

// End of methods for working with entity

void EntityBase::addIndex(const IndexDefinition& index) {
    entityDef_.indexes.push_back(index);
}

std::vector<IndexDefinition> EntityBase::getIndexes() const {
    return entityDef_.indexes;
}

void EntityBase::addConstraint(const ConstraintDefinition& constraint) {
    entityDef_.constraints.push_back(constraint);
}

std::vector<ConstraintDefinition> EntityBase::getConstraints() const {
    return entityDef_.constraints;
}

bool EntityBase::validateIndexes() const {
    // Validate indexes - check if all field names in indexes exist
    for (const auto& index : entityDef_.indexes) {
        for (const auto& fieldName : index.fieldNames) {
            if (!hasField(fieldName)) {
                std::cerr << "Index '" << index.name << "' references non-existent field '" << fieldName << "'" << std::endl;
                return false;
            }
        }
    }
    return true;
}

std::string EntityBase::getName() const {
    return entityDef_.name;
}

std::string EntityBase::getTableName() const {
    return entityDef_.tableName;
}

std::string EntityBase::getVerboseName() const {
    return entityDef_.verboseName;
}

std::string EntityBase::getVerboseNamePlural() const {
    return entityDef_.verboseNamePlural;
}

std::string EntityBase::getDescription() const {
    return entityDef_.description;
}

std::string EntityBase::buildCreateTableQuery() const {
    std::ostringstream sql;
    sql << "CREATE TABLE " << entityDef_.tableName << " (";

    bool first = true;
    // Add regular fields
    for (const auto& field : entityDef_.fields) {
        if (!first) sql << ", ";

        std::string dbType = SchemaLoader::mapAppTypeToDBType(
            field.type,
            db_->getDatabaseConfig().driver,
            field.precision,
            field.scale,
            field.maxLength
        );
        sql << field.name << " " << dbType;

        if (!field.nullable) {
            sql << " NOT NULL";
        }

        if (!field.defaultValue.empty()) {
            sql << " DEFAULT '" << field.defaultValue << "'";
        }

        if (field.primaryKey) {
            sql << " PRIMARY KEY";
        }

        first = false;
    }

    // Add foreign key fields
    for (const auto& fkField : entityDef_.foreignKeys) {
        if (!first) sql << ", ";
        std::string dbType = SchemaLoader::mapAppTypeToDBType(
            fkField.type,
            db_->getDatabaseConfig().driver,
            fkField.precision,
            fkField.scale,
            fkField.maxLength
        );
        sql << fkField.name << " " << dbType;

        if (!fkField.nullable) {
            sql << " NOT NULL";
        }

        // Add foreign key constraint
        sql << " REFERENCES " << fkField.references << "(" << fkField.toField << ")";

        // Add ON DELETE action
        if (!fkField.onDelete.empty() && fkField.onDelete != "NO_ACTION") {
            sql << " ON DELETE " << fkField.onDelete;
        }

        // Add ON UPDATE action
        if (!fkField.onUpdate.empty() && fkField.onUpdate != "NO_ACTION") {
            sql << " ON UPDATE " << fkField.onUpdate;
        }

        first = false;
    }

    // Add constraint definitions
    for (const auto& constraint : entityDef_.constraints) {
        if (!first) sql << ", ";
        sql << "CONSTRAINT " << constraint.constraintName << " " << constraint.type << " (" << constraint.expression << ")";
        first = false;
    }

    sql << ")";

    return sql.str();
}

std::string EntityBase::getPrimaryKeyFieldName() const {
    for (const auto& field : entityDef_.fields) {
        if (field.primaryKey) {
            return field.name;
        }
    }
    return "id"; // Default fallback
}

bool EntityBase::hasField(const std::string& fieldName) const {
    // Check regular fields
    for (const auto& field : entityDef_.fields) {
        if (field.name == fieldName) {
            return true;
        }
    }

    // Check foreign key fields
    for (const auto& fkField : entityDef_.foreignKeys) {
        if (fkField.name == fieldName) {
            return true;
        }
    }

    return false;
}

void EntityBase::addEntityFunction(const FunctionDefinition& function) {
    entityDef_.functions.push_back(function);
}

std::vector<FunctionDefinition> EntityBase::getEntityFunctions() const {
    return entityDef_.functions;
}

bool EntityBase::executeEntityFunction(const std::string& functionName,
                                       const std::map<std::string, std::string>& params) {
    // Find the function definition
    FunctionDefinition* funcDef = nullptr;
    for (auto& func : entityDef_.functions) {
        if (func.name == functionName) {
            funcDef = &func;
            break;
        }
    }

    if (!funcDef) {
        throw std::invalid_argument("Function '" + functionName + "' not found in entity '" + entityDef_.name + "'");
    }

    // Validate parameters
    for (const auto& paramDef : funcDef->parameters) {
        if (paramDef.required && params.find(paramDef.name) == params.end()) {
            throw std::invalid_argument("Required parameter '" + paramDef.name + "' is missing for function '" + functionName + "'");
        }
    }

    // Execute based on language
    if (funcDef->language == "cpp") {
        return executeCppFunction(*funcDef, params);
    } else if (funcDef->language == "python") {
        return executePythonFunction(*funcDef, params);
    } else {
        throw std::runtime_error("Unsupported function language: " + funcDef->language);
    }
}

bool EntityBase::executeCppFunction(const FunctionDefinition& funcDef,
                                    const std::map<std::string, std::string>& params) {
    // For C++ functions, we need to load a shared library
    if (funcDef.fileName.empty()) {
        throw std::invalid_argument("C++ function requires a fileName attribute");
    }

    // Load the shared library
    void* handle = dlopen(funcDef.fileName.c_str(), RTLD_LAZY);
    if (!handle) {
        throw std::runtime_error("Cannot load C++ library: " + funcDef.fileName);
    }

    // Get the function symbol
    typedef bool (*FunctionPtr)(const std::map<std::string, std::string>&);
    FunctionPtr function = (FunctionPtr) dlsym(handle, funcDef.name.c_str());

    if (!function) {
        dlclose(handle);
        throw std::runtime_error("Cannot find function symbol: " + funcDef.name);
    }

    // Execute the function
    bool result = function(params);

    // Clean up
    dlclose(handle);
    return result;
}

bool EntityBase::executePythonFunction(const FunctionDefinition &funcDef,
                                       const std::map<std::string, std::string> &params) {
#ifdef PYTHON_SUPPORT
    // Initialize Python interpreter if not already done
    if (!Py_IsInitialized()) {
        Py_Initialize();
    }

    // If fileName is provided, load the module
    if (!funcDef.fileName.empty()) {
        // Extract module name from file path
        std::string moduleName = funcDef.fileName;
        size_t lastSlash = moduleName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            moduleName = moduleName.substr(lastSlash + 1);
        }
        size_t lastDot = moduleName.find_last_of(".");
        if (lastDot != std::string::npos) {
            moduleName = moduleName.substr(0, lastDot);
        }

        // Import the module
        PyObject* pModule = PyImport_ImportModule(moduleName.c_str());
        if (!pModule) {
            PyErr_Print();
            throw std::runtime_error("Cannot import Python module: " + moduleName);
        }

        // Get the function
        PyObject* pFunc = PyObject_GetAttrString(pModule, funcDef.name.c_str());
        if (!pFunc || !PyCallable_Check(pFunc)) {
            Py_DECREF(pModule);
            throw std::runtime_error("Cannot find Python function: " + funcDef.name);
        }

        // Prepare arguments
        PyObject* pArgs = PyDict_New();
        for (const auto& param : params) {
            PyDict_SetItemString(pArgs, param.first.c_str(),
                               PyUnicode_FromString(param.second.c_str()));
        }

        // Call the function
        PyObject* pResult = PyObject_CallFunctionObjArgs(pFunc, pArgs, NULL);

        // Clean up
        Py_DECREF(pArgs);
        Py_DECREF(pFunc);
        Py_DECREF(pModule);

        if (!pResult) {
            PyErr_Print();
            throw std::runtime_error("Error executing Python function: " + funcDef.name);
        }

        // Handle result (assuming boolean return)
        bool result = PyObject_IsTrue(pResult);
        Py_DECREF(pResult);
        return result;
    } else {
        // Execute inline code
        // This is a simplified implementation
        throw std::runtime_error("Inline Python code execution not implemented");
    }
#else
    throw std::runtime_error("Python support not compiled in");
#endif
}

// Methods for working with fields
const std::vector<FieldDefinition>& EntityBase::getFields() const {
    return entityDef_.fields;
}

const FieldDefinition* EntityBase::getField(const std::string& name) const {
    auto it = std::find_if(entityDef_.fields.begin(), entityDef_.fields.end(),
                           [&name](const FieldDefinition& field) {
                               return field.name == name;
                           });

    if (it != entityDef_.fields.end()) {
        return &(*it);
    }

    return nullptr;
}

void EntityBase::addField(const std::string& name, const std::map<std::string, std::string>& attributes) {
    // Check that a field with this name does not exist
    if (hasField(name)) {
        throw std::invalid_argument("Field '" + name + "' already exists in entity '" + entityDef_.name + "'");
    }

    FieldDefinition newField;
    newField.name = name;

    // Fill attributes from map
    auto it = attributes.find("type");
    newField.type = (it != attributes.end()) ? it->second : "VARCHAR";

    it = attributes.find("primaryKey");
    newField.primaryKey = (it != attributes.end()) ? (it->second == "true" || it->second == "1") : false;

    it = attributes.find("nullable");
    newField.nullable = (it != attributes.end()) ? (it->second == "true" || it->second == "1") : true;

    it = attributes.find("unique");
    newField.unique = (it != attributes.end()) ? (it->second == "true" || it->second == "1") : false;

    it = attributes.find("autoIncrement");
    newField.autoIncrement = (it != attributes.end()) ? (it->second == "true" || it->second == "1") : false;

    it = attributes.find("defaultValue");
    newField.defaultValue = (it != attributes.end()) ? it->second : "";

    it = attributes.find("maxLength");
    if (it != attributes.end()) {
        try {
            int val = std::stoi(it->second);  // Check whether the string is a number
            newField.maxLength = it->second;  // Save the original string
        } catch (...) {
            newField.maxLength = "255";  // Default value on error
        }
    } else {
        newField.maxLength = "255";
    }

    it = attributes.find("description");
    newField.description = (it != attributes.end()) ? it->second : "";

    it = attributes.find("precision");
    newField.precision = (it != attributes.end()) ? it->second : "";

    it = attributes.find("scale");
    newField.scale = (it != attributes.end()) ? it->second : "";

    entityDef_.fields.push_back(newField);

    // Set the initial value for the field
    if (newField.nullable) {
        values_[name] = newField.defaultValue;
    } else if (!newField.defaultValue.empty()) {
        values_[name] = newField.defaultValue;
    }
}

bool EntityBase::updateField(const std::string& name, const std::map<std::string, std::string>& attributes) {
    auto it = std::find_if(entityDef_.fields.begin(), entityDef_.fields.end(),
                           [&name](const FieldDefinition& field) {
                               return field.name == name;
                           });

    if (it == entityDef_.fields.end()) {
        std::cerr << "Field '" << name << "' not found in entity '" << entityDef_.name << "'" << std::endl;
        return false;
    }

    // Update field attributes from map
    for (const auto& attr : attributes) {
        if (attr.first == "type") {
            it->type = attr.second;
        } else if (attr.first == "primaryKey") {
            it->primaryKey = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "nullable") {
            it->nullable = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "unique") {
            it->unique = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "autoIncrement") {
            it->autoIncrement = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "defaultValue") {
            it->defaultValue = attr.second;
            // Update the value in the object
            values_[name] = attr.second;
        } else if (attr.first == "maxLength") {
            it->maxLength = attr.second;
        } else if (attr.first == "description") {
            it->description = attr.second;
        } else if (attr.first == "precision") {
            it->precision = attr.second;
        } else if (attr.first == "scale") {
            it->scale = attr.second;
        }
    }

    return true;
}

bool EntityBase::deleteField(const std::string& name) {
    // Check whether the field exists
    auto fieldIt = std::find_if(entityDef_.fields.begin(), entityDef_.fields.end(),
                               [&name](const FieldDefinition& field) {
                                   return field.name == name;
                               });

    if (fieldIt == entityDef_.fields.end()) {
        std::cerr << "Field '" << name << "' not found in entity '" << entityDef_.name << "'" << std::endl;
        return false;
    }

    // Check whether the field is used in foreign keys
    auto fkIt = std::find_if(entityDef_.foreignKeys.begin(), entityDef_.foreignKeys.end(),
                            [&name](const ForeignKeyField& fk) {
                                return fk.name == name;
                            });

    if (fkIt != entityDef_.foreignKeys.end()) {
        std::cerr << "Field '" << name << "' is used as a foreign key and cannot be removed directly" << std::endl;
        return false;
    }

    // Remove the field from the field list
    entityDef_.fields.erase(fieldIt);

    // Remove the field value from the internal value storage
    values_.erase(name);

    // Check whether the field is in indexes and remove the index if present
    for (auto indexIt = entityDef_.indexes.begin(); indexIt != entityDef_.indexes.end();) {
        auto& index = *indexIt;
        auto fieldNameIt = std::find(index.fieldNames.begin(), index.fieldNames.end(), name);
        if (fieldNameIt != index.fieldNames.end()) {
            // If the index contains only this field, remove the whole index
            if (index.fieldNames.size() == 1) {
                indexIt = entityDef_.indexes.erase(indexIt);
            } else {
                // Otherwise remove only this field from the index
                index.fieldNames.erase(fieldNameIt);
                ++indexIt;
            }
        } else {
            ++indexIt;
        }
    }

    return true;
}


std::map<std::string, std::string> EntityBase::getFieldAttributes(const std::string& name) const {
    const FieldDefinition* field = getField(name);
    if (!field) {
        return {};
    }

    std::map<std::string, std::string> attributes;
    attributes["name"] = field->name;
    attributes["type"] = field->type;
    attributes["primaryKey"] = field->primaryKey ? "true" : "false";
    attributes["nullable"] = field->nullable ? "true" : "false";
    attributes["unique"] = field->unique ? "true" : "false";
    attributes["autoIncrement"] = field->autoIncrement ? "true" : "false";
    attributes["defaultValue"] = field->defaultValue;
    attributes["maxLength"] = field->maxLength;
    attributes["description"] = field->description;
    attributes["precision"] = field->precision;
    attributes["scale"] = field->scale;

    return attributes;
}

// Methods for working with foreign keys
void EntityBase::addForeignKey(const std::string& name,
                               const std::map<std::string, std::string>& attributes) {
    // Check that a foreign key with this name does not exist
    auto it = std::find_if(entityDef_.foreignKeys.begin(), entityDef_.foreignKeys.end(),
                           [&name](const ForeignKeyField& fk) {
                               return fk.name == name;
                           });

    if (it != entityDef_.foreignKeys.end()) {
        throw std::invalid_argument("ForeignKey '" + name + "' already exists in entity '" + entityDef_.name + "'");
    }

    ForeignKeyField fkField;
    fkField.name = name;

    // Fill attributes from map
    auto attrIt = attributes.find("type");
    fkField.type = (attrIt != attributes.end()) ? attrIt->second : "FOREIGN_KEY";

    attrIt = attributes.find("nullable");
    fkField.nullable = (attrIt != attributes.end()) ? (attrIt->second == "true" || attrIt->second == "1") : false;

    attrIt = attributes.find("primaryKey");
    fkField.primaryKey = (attrIt != attributes.end()) ? (attrIt->second == "true" || attrIt->second == "1") : false;

    attrIt = attributes.find("unique");
    fkField.unique = (attrIt != attributes.end()) ? (attrIt->second == "true" || attrIt->second == "1") : false;

    attrIt = attributes.find("maxLength");
    fkField.maxLength = (attrIt != attributes.end()) ? attrIt->second : "255";

    attrIt = attributes.find("defaultValue");
    fkField.defaultValue = (attrIt != attributes.end()) ? attrIt->second : "";

    attrIt = attributes.find("verboseName");
    fkField.verboseName = (attrIt != attributes.end()) ? attrIt->second : "";

    attrIt = attributes.find("description");
    fkField.description = (attrIt != attributes.end()) ? attrIt->second : "";

    attrIt = attributes.find("references");
    fkField.references = (attrIt != attributes.end()) ? attrIt->second : "";

    attrIt = attributes.find("onDelete");
    fkField.onDelete = (attrIt != attributes.end()) ? attrIt->second : "NO_ACTION";

    attrIt = attributes.find("onUpdate");
    fkField.onUpdate = (attrIt != attributes.end()) ? attrIt->second : "NO_ACTION";

    attrIt = attributes.find("toField");
    fkField.toField = (attrIt != attributes.end()) ? attrIt->second : "id";

    attrIt = attributes.find("relatedName");
    fkField.relatedName = (attrIt != attributes.end()) ? attrIt->second : "";

    entityDef_.foreignKeys.push_back(fkField);

    // Also add the field to the field list if it does not exist yet
    bool fieldExists = false;
    for (const auto& field : entityDef_.fields) {
        if (field.name == name) {
            fieldExists = true;
            break;
        }
    }

    if (!fieldExists) {
        FieldDefinition autoField;
        autoField.name = name;
        autoField.type = "INTEGER";  // Default type for foreign keys
        autoField.nullable = fkField.nullable;
        autoField.primaryKey = fkField.primaryKey;
        autoField.unique = fkField.unique;
        autoField.defaultValue = fkField.defaultValue;
        autoField.verboseName = fkField.verboseName;
        autoField.description = fkField.description;
        autoField.references = fkField.references;

        entityDef_.fields.push_back(autoField);
    }
}

bool EntityBase::updateForeignKey(const std::string& name,
                                  const std::map<std::string, std::string>& attributes) {
    auto it = std::find_if(entityDef_.foreignKeys.begin(), entityDef_.foreignKeys.end(),
                           [&name](const ForeignKeyField& fk) {
                               return fk.name == name;
                           });

    if (it == entityDef_.foreignKeys.end()) {
        std::cerr << "ForeignKey '" << name << "' not found in entity '" << entityDef_.name << "'" << std::endl;
        return false;
    }

    // Update foreign-key attributes from map
    for (const auto& attr : attributes) {
        if (attr.first == "type") {
            it->type = attr.second;
        } else if (attr.first == "nullable") {
            it->nullable = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "primaryKey") {
            it->primaryKey = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "unique") {
            it->unique = (attr.second == "true" || attr.second == "1");
        } else if (attr.first == "maxLength") {
            it->maxLength = attr.second;
        } else if (attr.first == "defaultValue") {
            it->defaultValue = attr.second;
        } else if (attr.first == "verboseName") {
            it->verboseName = attr.second;
        } else if (attr.first == "description") {
            it->description = attr.second;
        } else if (attr.first == "references") {
            it->references = attr.second;
        } else if (attr.first == "onDelete") {
            it->onDelete = attr.second;
        } else if (attr.first == "onUpdate") {
            it->onUpdate = attr.second;
        } else if (attr.first == "toField") {
            it->toField = attr.second;
        } else if (attr.first == "relatedName") {
            it->relatedName = attr.second;
        }
    }

    // Update the related field in the field list
    for (auto& field : entityDef_.fields) {
        if (field.name == name) {
            field.type = (attributes.count("type")) ? attributes.at("type") : field.type;
            field.nullable = (attributes.count("nullable")) ?
                            (attributes.at("nullable") == "true" || attributes.at("nullable") == "1") : field.nullable;
            field.defaultValue = (attributes.count("defaultValue")) ? attributes.at("defaultValue") : field.defaultValue;
            field.description = (attributes.count("description")) ? attributes.at("description") : field.description;
            field.references = (attributes.count("references")) ? attributes.at("references") : field.references;
            break;
        }
    }

    return true;
}

bool EntityBase::removeForeignKey(const std::string& name) {
    auto it = std::find_if(entityDef_.foreignKeys.begin(), entityDef_.foreignKeys.end(),
                           [&name](const ForeignKeyField& fk) {
                               return fk.name == name;
                           });

    if (it == entityDef_.foreignKeys.end()) {
        std::cerr << "ForeignKey '" << name << "' not found in entity '" << entityDef_.name << "'" << std::endl;
        return false;
    }

    // Remove the foreign key from the entity definition
    entityDef_.foreignKeys.erase(it);

    // Remove the related field from the field list
    auto fieldIt = std::find_if(entityDef_.fields.begin(), entityDef_.fields.end(),
                                [&name](const FieldDefinition& field) {
                                    return field.name == name;
                                });

    if (fieldIt != entityDef_.fields.end()) {
        entityDef_.fields.erase(fieldIt);
    }

    return true;
}

const std::vector<ForeignKeyField>& EntityBase::getForeignKeys() const {
    return entityDef_.foreignKeys;
}

const ForeignKeyField* EntityBase::getForeignKey(const std::string& name) const {
    auto it = std::find_if(entityDef_.foreignKeys.begin(), entityDef_.foreignKeys.end(),
                           [&name](const ForeignKeyField& fk) {
                               return fk.name == name;
                           });

    if (it != entityDef_.foreignKeys.end()) {
        return &(*it);
    }

    return nullptr;
}

bool EntityBase::addToSchemaLoader() {
    try {
        // Add the entity to SchemaLoader
        SchemaLoader::entities_[entityDef_.name] = entityDef_;
        SchemaLoader::dbEntities_[entityDef_.name] = entityDef_;

        // Update reverse relations
        auto& schemaEntities = const_cast<std::map<std::string, EntityDefinition>&>(
            SchemaLoader::getEntities());
        SchemaLoader::processEntityReverseRelationships(entityDef_, schemaEntities);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "EntityBase::addToSchemaLoader() - Error: " << e.what() << std::endl;
        return false;
    }
}
