/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app_struct.h"
#include "xml_schema_validator.h"

/**
 * Identifies where a SchemaDocument came from.
 *
 * SchemaDocument keeps uploaded XML, generated XML and database exports as
 * different artifacts. This prevents a database export from overwriting the
 * user's desired XML schema by accident.
 */
enum class SchemaDocumentSourceType {
    Unknown,
    UploadedXml,
    DatabaseExport,
    Generated
};

struct SchemaDocumentSourceMetadata {
    SchemaDocumentSourceType type = SchemaDocumentSourceType::Unknown;
    std::string sourcePath;
    std::string documentName;
    std::string databaseEngine;
    std::string schemaFormatVersion = "1.0";
    std::string createdAt;
};

struct SchemaDocumentParseError {
    std::string code;
    std::string message;
    std::string elementPath;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct SchemaDocumentParseResult;
class SchemaNormalizer;
class DatabaseSchemaExporter;

/**
 * Typed semantic model of qornix_orm/schema/schema_app.xsd.
 *
 * Raw XML is not the object that future diff/plan/apply code should compare.
 * The intended flow is:
 *
 *   XML -> XSD validation -> SchemaDocument -> normalization -> semantic diff
 *
 * DatabaseSnapshot will later be converted into another SchemaDocument so the
 * diff layer can compare SchemaDocument desired vs SchemaDocument current.
 */
class SchemaDocument {
    friend class SchemaNormalizer;
    friend class DatabaseSchemaExporter;
class DatabaseSchemaExporter;

public:
    SchemaDocument() = default;

    static SchemaDocumentParseResult loadFromFile(
        const std::string& xmlFilePath,
        const std::string& xsdPath = XmlSchemaValidator::defaultSchemaPath(),
        SchemaDocumentSourceType sourceType = SchemaDocumentSourceType::UploadedXml
    );

    static SchemaDocumentParseResult parseString(
        const std::string& xmlContent,
        const std::string& documentName = "<memory>",
        const std::string& xsdPath = XmlSchemaValidator::defaultSchemaPath(),
        SchemaDocumentSourceType sourceType = SchemaDocumentSourceType::UploadedXml
    );

    static std::string sourceTypeToString(SchemaDocumentSourceType sourceType);

    const SchemaDocumentSourceMetadata& sourceMetadata() const {
        return sourceMetadata_;
    }

    const SchemaMetadata& metadata() const {
        return metadata_;
    }

    const ConfigurationDefinition& configuration() const {
        return configuration_;
    }

    const std::vector<EntityDefinition>& entities() const {
        return entities_;
    }

    const std::vector<ViewDefinition>& views() const {
        return views_;
    }

    const std::vector<MaterializedViewDefinition>& materializedViews() const {
        return materializedViews_;
    }

    const std::vector<StoredProcedureDefinition>& storedProcedures() const {
        return storedProcedures_;
    }

    const std::vector<TriggerDefinition>& triggers() const {
        return triggers_;
    }

    const std::vector<DatabaseFunctionDefinition>& databaseFunctions() const {
        return databaseFunctions_;
    }

    const std::vector<SequenceDefinition>& sequences() const {
        return sequences_;
    }

    const DatabaseMappingDefinition& databaseMapping() const {
        return databaseMapping_;
    }

    std::map<std::string, EntityDefinition> entityMapByName() const;
    SchemaSnapshot toSnapshot() const;

    std::string toCanonicalXml() const;
    bool saveCanonicalXml(const std::string& outputFilePath) const;

private:
    SchemaDocumentSourceMetadata sourceMetadata_;
    SchemaMetadata metadata_;
    ConfigurationDefinition configuration_;
    std::vector<EntityDefinition> entities_;
    std::vector<ViewDefinition> views_;
    std::vector<MaterializedViewDefinition> materializedViews_;
    std::vector<StoredProcedureDefinition> storedProcedures_;
    std::vector<TriggerDefinition> triggers_;
    std::vector<DatabaseFunctionDefinition> databaseFunctions_;
    std::vector<SequenceDefinition> sequences_;
    DatabaseMappingDefinition databaseMapping_;
};

struct SchemaDocumentParseResult {
    bool success = false;
    std::unique_ptr<SchemaDocument> document;
    std::vector<SchemaDocumentParseError> errors;

    bool ok() const {
        return success;
    }
};
