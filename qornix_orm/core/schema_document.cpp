/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_document.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

#include <pugixml.hpp>

namespace {

SchemaDocumentParseError makeError(
    std::string code,
    std::string message,
    std::string elementPath = {},
    std::size_t line = 0,
    std::size_t column = 0
) {
    SchemaDocumentParseError error;
    error.code = std::move(code);
    error.message = std::move(message);
    error.elementPath = std::move(elementPath);
    error.line = line;
    error.column = column;
    return error;
}

SchemaDocumentParseResult failure(std::vector<SchemaDocumentParseError> errors) {
    SchemaDocumentParseResult result;
    result.success = false;
    result.errors = std::move(errors);
    return result;
}

std::vector<SchemaDocumentParseError> fromValidationErrors(
    const std::vector<XmlValidationError>& validationErrors
) {
    std::vector<SchemaDocumentParseError> errors;
    errors.reserve(validationErrors.size());
    for (const auto& validationError : validationErrors) {
        errors.push_back(makeError(
            validationError.code,
            validationError.message,
            validationError.elementPath,
            validationError.line,
            validationError.column
        ));
    }
    return errors;
}

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void appendAttributeIfNotEmpty(pugi::xml_node node, const char* name, const std::string& value) {
    if (!value.empty()) {
        node.append_attribute(name) = value.c_str();
    }
}

void appendBoolAttributeIfTrue(pugi::xml_node node, const char* name, bool value) {
    if (value) {
        node.append_attribute(name) = value;
    }
}

void appendText(pugi::xml_node parent, const char* name, const std::string& value) {
    auto child = parent.append_child(name);
    child.text().set(value.c_str());
}

void appendTextIfNotEmpty(pugi::xml_node parent, const char* name, const std::string& value) {
    if (!value.empty()) {
        appendText(parent, name, value);
    }
}

std::string attr(pugi::xml_node node, const char* name, const std::string& defaultValue = {}) {
    const auto attribute = node.attribute(name);
    return attribute ? attribute.as_string() : defaultValue;
}

bool attrBool(pugi::xml_node node, const char* name, bool defaultValue = false) {
    const auto attribute = node.attribute(name);
    return attribute ? attribute.as_bool(defaultValue) : defaultValue;
}

long long attrLongLong(pugi::xml_node node, const char* name, long long defaultValue) {
    const auto attribute = node.attribute(name);
    return attribute ? attribute.as_llong(defaultValue) : defaultValue;
}

FieldDefinition parseField(pugi::xml_node fieldNode) {
    FieldDefinition field;
    field.name = attr(fieldNode, "name");
    field.type = attr(fieldNode, "type");
    field.nullable = attrBool(fieldNode, "nullable", false);
    field.primaryKey = attrBool(fieldNode, "primaryKey", false);
    field.unique = attrBool(fieldNode, "unique", false);
    field.maxLength = attr(fieldNode, "maxLength");
    field.defaultValue = attr(fieldNode, "defaultValue");
    field.verboseName = attr(fieldNode, "verboseName");
    field.description = attr(fieldNode, "description");
    field.autoIncrement = attrBool(fieldNode, "autoIncrement", false);
    field.collation = attr(fieldNode, "collation");
    field.computedExpression = attr(fieldNode, "computedExpression");
    field.isComputed = attrBool(fieldNode, "isComputed", false);
    field.precision = attr(fieldNode, "precision");
    field.scale = attr(fieldNode, "scale");

    const auto enumValuesNode = fieldNode.child("EnumValues");
    for (const auto valueNode : enumValuesNode.children("Value")) {
        field.enumValues.push_back(valueNode.text().as_string());
    }

    return field;
}

ForeignKeyField parseForeignKeyField(pugi::xml_node fkNode) {
    ForeignKeyField field;
    const auto base = parseField(fkNode);

    field.name = base.name;
    field.type = base.type;
    field.nullable = base.nullable;
    field.primaryKey = base.primaryKey;
    field.unique = base.unique;
    field.maxLength = base.maxLength;
    field.defaultValue = base.defaultValue;
    field.verboseName = base.verboseName;
    field.description = base.description;
    field.autoIncrement = base.autoIncrement;
    field.collation = base.collation;
    field.computedExpression = base.computedExpression;
    field.isComputed = base.isComputed;
    field.precision = base.precision;
    field.scale = base.scale;
    field.enumValues = base.enumValues;

    field.references = attr(fkNode, "references");
    field.onDelete = attr(fkNode, "onDelete", "NO_ACTION");
    field.onUpdate = attr(fkNode, "onUpdate", "NO_ACTION");
    field.toField = attr(fkNode, "toField", "id");
    field.relatedName = attr(fkNode, "relatedName");
    return field;
}

IndexDefinition parseIndex(pugi::xml_node indexNode) {
    IndexDefinition index;
    index.name = attr(indexNode, "name");
    index.unique = attrBool(indexNode, "unique", false);
    index.type = attr(indexNode, "type", "BTREE");
    index.concurrently = attrBool(indexNode, "concurrently", false);
    index.tablespace = attr(indexNode, "tablespace");

    for (const auto fieldNameNode : indexNode.children("FieldName")) {
        index.fieldNames.push_back(fieldNameNode.text().as_string());
    }
    return index;
}

ConstraintDefinition parseConstraint(pugi::xml_node constraintNode) {
    ConstraintDefinition constraint;
    constraint.name = constraintNode.child("Name").text().as_string();
    constraint.constraintName = attr(constraintNode, "constraintName");
    constraint.type = attr(constraintNode, "type");
    constraint.description = constraintNode.child("Description").text().as_string();
    constraint.expression = constraintNode.child("Expression").text().as_string();
    return constraint;
}

ParameterDefinition parseParameter(pugi::xml_node parameterNode) {
    ParameterDefinition parameter;
    parameter.name = attr(parameterNode, "name");
    parameter.type = attr(parameterNode, "type");
    parameter.required = attrBool(parameterNode, "required", false);
    parameter.mode = attr(parameterNode, "mode");
    return parameter;
}

FunctionDefinition parseFunction(pugi::xml_node functionNode) {
    FunctionDefinition function;
    const auto defaultFunctionName = attr(functionNode, "name");
    function.name = functionNode.child("Name").text().as_string(defaultFunctionName.c_str());
    function.description = functionNode.child("Description").text().as_string();
    function.returnType = functionNode.child("ReturnType").text().as_string();
    function.code = functionNode.child("Code").text().as_string();
    function.fileName = attr(functionNode, "fileName");
    function.language = attr(functionNode, "language", "python");

    const auto parametersNode = functionNode.child("Parameters");
    for (const auto parameterNode : parametersNode.children("Parameter")) {
        function.parameters.push_back(parseParameter(parameterNode));
    }
    return function;
}

std::map<std::string, std::string> parseOptions(pugi::xml_node parent) {
    std::map<std::string, std::string> options;
    const auto optionsNode = parent.child("Options");
    for (const auto optionNode : optionsNode.children("Option")) {
        options[attr(optionNode, "key")] = attr(optionNode, "value");
    }
    return options;
}

PartitionDefinition parsePartition(pugi::xml_node partitionsNode) {
    PartitionDefinition partition;
    partition.strategy = partitionsNode.child("Strategy").text().as_string();
    partition.expression = partitionsNode.child("Expression").text().as_string();

    const auto columnsNode = partitionsNode.child("Columns");
    for (const auto columnNode : columnsNode.children("Column")) {
        partition.columns.push_back(columnNode.text().as_string());
    }

    const auto boundsNode = partitionsNode.child("Bounds");
    for (const auto boundNode : boundsNode.children("Bound")) {
        PartitionBoundDefinition bound;
        bound.partitionName = boundNode.child("PartitionName").text().as_string();
        bound.lowerBound = boundNode.child("LowerBound").text().as_string();
        bound.upperBound = boundNode.child("UpperBound").text().as_string();
        const auto valuesNode = boundNode.child("Values");
        for (const auto valueNode : valuesNode.children("Value")) {
            bound.values.push_back(valueNode.text().as_string());
        }
        partition.bounds.push_back(std::move(bound));
    }

    return partition;
}

EntityDefinition parseEntity(pugi::xml_node entityNode) {
    EntityDefinition entity;
    entity.name = attr(entityNode, "name");
    entity.tableName = attr(entityNode, "tableName", entity.name);
    entity.verboseName = attr(entityNode, "verboseName");
    entity.verboseNamePlural = attr(entityNode, "verboseNamePlural");
    entity.description = attr(entityNode, "description");
    entity.tablespace = attr(entityNode, "tablespace");
    entity.isPartitioned = attrBool(entityNode, "isPartitioned", false);
    entity.options = parseOptions(entityNode);

    for (const auto fieldNode : entityNode.children("Field")) {
        entity.fields.push_back(parseField(fieldNode));
    }
    for (const auto fkNode : entityNode.children("ForeignKeyField")) {
        entity.foreignKeys.push_back(parseForeignKeyField(fkNode));
    }
    for (const auto indexNode : entityNode.children("Index")) {
        entity.indexes.push_back(parseIndex(indexNode));
    }
    for (const auto constraintNode : entityNode.children("Constraint")) {
        entity.constraints.push_back(parseConstraint(constraintNode));
    }

    const auto functionsNode = entityNode.child("EntityFunctions");
    for (const auto functionNode : functionsNode.children("Function")) {
        entity.functions.push_back(parseFunction(functionNode));
    }

    const auto partitionsNode = entityNode.child("Partitions");
    if (partitionsNode) {
        entity.partitions.push_back(parsePartition(partitionsNode));
        entity.isPartitioned = true;
    }

    return entity;
}

ViewDefinition parseView(pugi::xml_node viewNode) {
    ViewDefinition view;
    view.name = viewNode.child("Name").text().as_string();
    view.viewName = attr(viewNode, "viewName");
    view.description = viewNode.child("Description").text().as_string();
    view.query = viewNode.child("Query").text().as_string();
    view.isUpdatable = attrBool(viewNode, "isUpdatable", false);
    return view;
}

MaterializedViewDefinition parseMaterializedView(pugi::xml_node viewNode) {
    MaterializedViewDefinition view;
    view.name = viewNode.child("Name").text().as_string();
    view.viewName = attr(viewNode, "viewName");
    view.description = viewNode.child("Description").text().as_string();
    view.query = viewNode.child("Query").text().as_string();
    view.refreshStrategy = viewNode.child("RefreshStrategy").text().as_string();
    view.withData = attrBool(viewNode, "withData", true);
    return view;
}

StoredProcedureDefinition parseStoredProcedure(pugi::xml_node procedureNode) {
    StoredProcedureDefinition procedure;
    procedure.name = procedureNode.child("Name").text().as_string();
    procedure.procedureName = attr(procedureNode, "procedureName");
    procedure.description = procedureNode.child("Description").text().as_string();
    procedure.language = attr(procedureNode, "language", "sql");
    procedure.security = attr(procedureNode, "security", "DEFINER");
    procedure.returnType = procedureNode.child("ReturnType").text().as_string();
    procedure.code = procedureNode.child("Code").text().as_string();
    return procedure;
}

TriggerDefinition parseTrigger(pugi::xml_node triggerNode) {
    TriggerDefinition trigger;
    trigger.name = triggerNode.child("Name").text().as_string();
    trigger.triggerName = attr(triggerNode, "triggerName");
    trigger.tableName = attr(triggerNode, "tableName");
    trigger.description = triggerNode.child("Description").text().as_string();
    trigger.event = triggerNode.child("Event").text().as_string();
    trigger.timing = triggerNode.child("Timing").text().as_string();
    trigger.function = triggerNode.child("Function").text().as_string();
    trigger.condition = triggerNode.child("Condition").text().as_string();
    trigger.enabled = attrBool(triggerNode, "enabled", true);
    return trigger;
}

DatabaseFunctionDefinition parseDatabaseFunction(pugi::xml_node functionNode) {
    DatabaseFunctionDefinition function;
    function.name = functionNode.child("Name").text().as_string();
    function.functionName = attr(functionNode, "functionName");
    function.description = functionNode.child("Description").text().as_string();
    function.language = attr(functionNode, "language", "sql");
    function.volatileType = attr(functionNode, "volatile", "VOLATILE");
    function.security = attr(functionNode, "security", "DEFINER");
    function.returnType = functionNode.child("ReturnType").text().as_string();
    function.code = functionNode.child("Code").text().as_string();
    return function;
}

SequenceDefinition parseSequence(pugi::xml_node sequenceNode) {
    SequenceDefinition sequence;
    sequence.name = attr(sequenceNode, "name");
    sequence.tableName = attr(sequenceNode, "tableName");
    sequence.columnName = attr(sequenceNode, "columnName");
    sequence.startValue = attrLongLong(sequenceNode, "startValue", 1);
    sequence.increment = attrLongLong(sequenceNode, "increment", 1);
    sequence.minValue = attrLongLong(sequenceNode, "minValue", 1);
    sequence.maxValue = attrLongLong(sequenceNode, "maxValue", LONG_LONG_MAX);
    sequence.cycle = attrBool(sequenceNode, "cycle", false);
    sequence.cache = attrLongLong(sequenceNode, "cache", 1);
    return sequence;
}

void appendField(pugi::xml_node entityNode, const FieldDefinition& field, bool foreignKeyField = false) {
    auto fieldNode = entityNode.append_child(foreignKeyField ? "ForeignKeyField" : "Field");
    fieldNode.append_attribute("name") = field.name.c_str();
    fieldNode.append_attribute("type") = field.type.c_str();
    appendBoolAttributeIfTrue(fieldNode, "nullable", field.nullable);
    appendBoolAttributeIfTrue(fieldNode, "primaryKey", field.primaryKey);
    appendBoolAttributeIfTrue(fieldNode, "unique", field.unique);
    appendAttributeIfNotEmpty(fieldNode, "maxLength", field.maxLength);
    appendAttributeIfNotEmpty(fieldNode, "defaultValue", field.defaultValue);
    appendAttributeIfNotEmpty(fieldNode, "verboseName", field.verboseName);
    appendAttributeIfNotEmpty(fieldNode, "description", field.description);
    appendBoolAttributeIfTrue(fieldNode, "autoIncrement", field.autoIncrement);
    appendAttributeIfNotEmpty(fieldNode, "collation", field.collation);
    appendAttributeIfNotEmpty(fieldNode, "computedExpression", field.computedExpression);
    appendBoolAttributeIfTrue(fieldNode, "isComputed", field.isComputed);
    appendAttributeIfNotEmpty(fieldNode, "precision", field.precision);
    appendAttributeIfNotEmpty(fieldNode, "scale", field.scale);

    if (!field.enumValues.empty()) {
        auto enumValuesNode = fieldNode.append_child("EnumValues");
        for (const auto& value : field.enumValues) {
            appendText(enumValuesNode, "Value", value);
        }
    }
}

void appendForeignKeyField(pugi::xml_node entityNode, const ForeignKeyField& field) {
    auto fieldNode = entityNode.append_child("ForeignKeyField");
    fieldNode.append_attribute("name") = field.name.c_str();
    fieldNode.append_attribute("type") = field.type.c_str();
    appendBoolAttributeIfTrue(fieldNode, "nullable", field.nullable);
    appendBoolAttributeIfTrue(fieldNode, "primaryKey", field.primaryKey);
    appendBoolAttributeIfTrue(fieldNode, "unique", field.unique);
    appendAttributeIfNotEmpty(fieldNode, "maxLength", field.maxLength);
    appendAttributeIfNotEmpty(fieldNode, "defaultValue", field.defaultValue);
    appendAttributeIfNotEmpty(fieldNode, "verboseName", field.verboseName);
    appendAttributeIfNotEmpty(fieldNode, "description", field.description);
    appendBoolAttributeIfTrue(fieldNode, "autoIncrement", field.autoIncrement);
    appendAttributeIfNotEmpty(fieldNode, "collation", field.collation);
    appendAttributeIfNotEmpty(fieldNode, "computedExpression", field.computedExpression);
    appendBoolAttributeIfTrue(fieldNode, "isComputed", field.isComputed);
    appendAttributeIfNotEmpty(fieldNode, "precision", field.precision);
    appendAttributeIfNotEmpty(fieldNode, "scale", field.scale);
    fieldNode.append_attribute("references") = field.references.c_str();
    appendAttributeIfNotEmpty(fieldNode, "onDelete", field.onDelete);
    appendAttributeIfNotEmpty(fieldNode, "onUpdate", field.onUpdate);
    appendAttributeIfNotEmpty(fieldNode, "toField", field.toField);
    appendAttributeIfNotEmpty(fieldNode, "relatedName", field.relatedName);

    if (!field.enumValues.empty()) {
        auto enumValuesNode = fieldNode.append_child("EnumValues");
        for (const auto& value : field.enumValues) {
            appendText(enumValuesNode, "Value", value);
        }
    }
}

void appendIndex(pugi::xml_node entityNode, const IndexDefinition& index) {
    auto indexNode = entityNode.append_child("Index");
    indexNode.append_attribute("name") = index.name.c_str();
    appendBoolAttributeIfTrue(indexNode, "unique", index.unique);
    appendAttributeIfNotEmpty(indexNode, "type", index.type);
    appendBoolAttributeIfTrue(indexNode, "concurrently", index.concurrently);
    appendAttributeIfNotEmpty(indexNode, "tablespace", index.tablespace);
    for (const auto& fieldName : index.fieldNames) {
        appendText(indexNode, "FieldName", fieldName);
    }
}

void appendConstraint(pugi::xml_node entityNode, const ConstraintDefinition& constraint) {
    auto constraintNode = entityNode.append_child("Constraint");
    constraintNode.append_attribute("constraintName") = constraint.constraintName.c_str();
    constraintNode.append_attribute("type") = constraint.type.c_str();
    appendText(constraintNode, "Name", constraint.name);
    appendTextIfNotEmpty(constraintNode, "Description", constraint.description);
    appendText(constraintNode, "Expression", constraint.expression);
}

void appendFunction(pugi::xml_node functionsNode, const FunctionDefinition& function) {
    auto functionNode = functionsNode.append_child("Function");
    appendAttributeIfNotEmpty(functionNode, "name", function.name);
    appendAttributeIfNotEmpty(functionNode, "fileName", function.fileName);
    appendAttributeIfNotEmpty(functionNode, "language", function.language);
    appendText(functionNode, "Name", function.name);
    appendTextIfNotEmpty(functionNode, "Description", function.description);

    if (!function.parameters.empty()) {
        auto parametersNode = functionNode.append_child("Parameters");
        for (const auto& parameter : function.parameters) {
            auto parameterNode = parametersNode.append_child("Parameter");
            parameterNode.append_attribute("name") = parameter.name.c_str();
            parameterNode.append_attribute("type") = parameter.type.c_str();
            appendBoolAttributeIfTrue(parameterNode, "required", parameter.required);
        }
    }

    appendTextIfNotEmpty(functionNode, "ReturnType", function.returnType);
    appendTextIfNotEmpty(functionNode, "Code", function.code);
}

void appendOptions(pugi::xml_node parent, const std::map<std::string, std::string>& options) {
    if (options.empty()) {
        return;
    }
    auto optionsNode = parent.append_child("Options");
    for (const auto& [key, value] : options) {
        auto optionNode = optionsNode.append_child("Option");
        optionNode.append_attribute("key") = key.c_str();
        optionNode.append_attribute("value") = value.c_str();
    }
}

void appendPartitions(pugi::xml_node entityNode, const std::vector<PartitionDefinition>& partitions) {
    if (partitions.empty()) {
        return;
    }

    const auto& partition = partitions.front();
    auto partitionsNode = entityNode.append_child("Partitions");
    appendText(partitionsNode, "Strategy", partition.strategy);

    auto columnsNode = partitionsNode.append_child("Columns");
    for (const auto& column : partition.columns) {
        appendText(columnsNode, "Column", column);
    }

    appendTextIfNotEmpty(partitionsNode, "Expression", partition.expression);
    if (!partition.bounds.empty()) {
        auto boundsNode = partitionsNode.append_child("Bounds");
        for (const auto& bound : partition.bounds) {
            auto boundNode = boundsNode.append_child("Bound");
            appendText(boundNode, "PartitionName", bound.partitionName);
            appendTextIfNotEmpty(boundNode, "LowerBound", bound.lowerBound);
            appendTextIfNotEmpty(boundNode, "UpperBound", bound.upperBound);
            if (!bound.values.empty()) {
                auto valuesNode = boundNode.append_child("Values");
                for (const auto& value : bound.values) {
                    appendText(valuesNode, "Value", value);
                }
            }
        }
    }
}

void appendEntity(pugi::xml_node dataStructureNode, const EntityDefinition& entity) {
    auto entityNode = dataStructureNode.append_child("Entity");
    entityNode.append_attribute("name") = entity.name.c_str();
    appendAttributeIfNotEmpty(entityNode, "tableName", entity.tableName);
    appendAttributeIfNotEmpty(entityNode, "verboseName", entity.verboseName);
    appendAttributeIfNotEmpty(entityNode, "verboseNamePlural", entity.verboseNamePlural);
    appendAttributeIfNotEmpty(entityNode, "description", entity.description);
    appendAttributeIfNotEmpty(entityNode, "tablespace", entity.tablespace);
    appendBoolAttributeIfTrue(entityNode, "isPartitioned", entity.isPartitioned);

    for (const auto& field : entity.fields) {
        appendField(entityNode, field);
    }
    for (const auto& foreignKey : entity.foreignKeys) {
        appendForeignKeyField(entityNode, foreignKey);
    }
    for (const auto& index : entity.indexes) {
        appendIndex(entityNode, index);
    }
    for (const auto& constraint : entity.constraints) {
        appendConstraint(entityNode, constraint);
    }
    if (!entity.functions.empty()) {
        auto functionsNode = entityNode.append_child("EntityFunctions");
        for (const auto& function : entity.functions) {
            appendFunction(functionsNode, function);
        }
    }
    appendOptions(entityNode, entity.options);
    appendPartitions(entityNode, entity.partitions);
}

void appendViews(pugi::xml_node dataStructureNode, const std::vector<ViewDefinition>& views) {
    if (views.empty()) {
        return;
    }
    auto viewsNode = dataStructureNode.append_child("Views");
    for (const auto& view : views) {
        auto viewNode = viewsNode.append_child("View");
        viewNode.append_attribute("viewName") = view.viewName.c_str();
        appendBoolAttributeIfTrue(viewNode, "isUpdatable", view.isUpdatable);
        appendText(viewNode, "Name", view.name);
        appendTextIfNotEmpty(viewNode, "Description", view.description);
        appendText(viewNode, "Query", view.query);
    }
}

void appendMaterializedViews(
    pugi::xml_node dataStructureNode,
    const std::vector<MaterializedViewDefinition>& materializedViews
) {
    if (materializedViews.empty()) {
        return;
    }
    auto viewsNode = dataStructureNode.append_child("MaterializedViews");
    for (const auto& view : materializedViews) {
        auto viewNode = viewsNode.append_child("MaterializedView");
        viewNode.append_attribute("viewName") = view.viewName.c_str();
        appendBoolAttributeIfTrue(viewNode, "withData", view.withData);
        appendText(viewNode, "Name", view.name);
        appendTextIfNotEmpty(viewNode, "Description", view.description);
        appendText(viewNode, "Query", view.query);
        appendTextIfNotEmpty(viewNode, "RefreshStrategy", view.refreshStrategy);
    }
}

void appendStoredProcedures(
    pugi::xml_node dataStructureNode,
    const std::vector<StoredProcedureDefinition>& procedures
) {
    if (procedures.empty()) {
        return;
    }
    auto proceduresNode = dataStructureNode.append_child("StoredProcedures");
    for (const auto& procedure : procedures) {
        auto procedureNode = proceduresNode.append_child("StoredProcedure");
        procedureNode.append_attribute("procedureName") = procedure.procedureName.c_str();
        appendAttributeIfNotEmpty(procedureNode, "language", procedure.language);
        appendAttributeIfNotEmpty(procedureNode, "security", procedure.security);
        appendText(procedureNode, "Name", procedure.name);
        appendTextIfNotEmpty(procedureNode, "Description", procedure.description);
        appendTextIfNotEmpty(procedureNode, "ReturnType", procedure.returnType);
        appendTextIfNotEmpty(procedureNode, "Code", procedure.code);
    }
}

void appendTriggers(pugi::xml_node dataStructureNode, const std::vector<TriggerDefinition>& triggers) {
    if (triggers.empty()) {
        return;
    }
    auto triggersNode = dataStructureNode.append_child("Triggers");
    for (const auto& trigger : triggers) {
        auto triggerNode = triggersNode.append_child("Trigger");
        triggerNode.append_attribute("triggerName") = trigger.triggerName.c_str();
        triggerNode.append_attribute("tableName") = trigger.tableName.c_str();
        appendBoolAttributeIfTrue(triggerNode, "enabled", trigger.enabled);
        appendText(triggerNode, "Name", trigger.name);
        appendTextIfNotEmpty(triggerNode, "Description", trigger.description);
        appendText(triggerNode, "Event", trigger.event);
        appendText(triggerNode, "Timing", trigger.timing);
        appendText(triggerNode, "Function", trigger.function);
        appendTextIfNotEmpty(triggerNode, "Condition", trigger.condition);
    }
}

void appendDatabaseFunctions(
    pugi::xml_node dataStructureNode,
    const std::vector<DatabaseFunctionDefinition>& functions
) {
    if (functions.empty()) {
        return;
    }
    auto functionsNode = dataStructureNode.append_child("DatabaseFunctions");
    for (const auto& function : functions) {
        auto functionNode = functionsNode.append_child("DatabaseFunction");
        functionNode.append_attribute("functionName") = function.functionName.c_str();
        appendAttributeIfNotEmpty(functionNode, "language", function.language);
        appendAttributeIfNotEmpty(functionNode, "volatile", function.volatileType);
        appendAttributeIfNotEmpty(functionNode, "security", function.security);
        appendText(functionNode, "Name", function.name);
        appendTextIfNotEmpty(functionNode, "Description", function.description);
        appendText(functionNode, "ReturnType", function.returnType);
        appendText(functionNode, "Code", function.code);
    }
}

void appendSequences(pugi::xml_node dataStructureNode, const std::vector<SequenceDefinition>& sequences) {
    if (sequences.empty()) {
        return;
    }
    auto sequencesNode = dataStructureNode.append_child("Sequences");
    for (const auto& sequence : sequences) {
        auto sequenceNode = sequencesNode.append_child("Sequence");
        sequenceNode.append_attribute("name") = sequence.name.c_str();
        appendAttributeIfNotEmpty(sequenceNode, "tableName", sequence.tableName);
        appendAttributeIfNotEmpty(sequenceNode, "columnName", sequence.columnName);
        if (sequence.startValue != 1) sequenceNode.append_attribute("startValue") = std::to_string(sequence.startValue).c_str();
        if (sequence.increment != 1) sequenceNode.append_attribute("increment") = std::to_string(sequence.increment).c_str();
        if (sequence.minValue != 1) sequenceNode.append_attribute("minValue") = std::to_string(sequence.minValue).c_str();
        if (sequence.maxValue != LONG_LONG_MAX) sequenceNode.append_attribute("maxValue") = std::to_string(sequence.maxValue).c_str();
        appendBoolAttributeIfTrue(sequenceNode, "cycle", sequence.cycle);
        if (sequence.cache != 1) sequenceNode.append_attribute("cache") = std::to_string(sequence.cache).c_str();
    }
}

} // namespace

std::string SchemaDocument::sourceTypeToString(SchemaDocumentSourceType sourceType) {
    switch (sourceType) {
        case SchemaDocumentSourceType::UploadedXml:
            return "uploaded_xml";
        case SchemaDocumentSourceType::DatabaseExport:
            return "database_export";
        case SchemaDocumentSourceType::Generated:
            return "generated";
        case SchemaDocumentSourceType::Unknown:
        default:
            return "unknown";
    }
}

SchemaDocumentParseResult SchemaDocument::loadFromFile(
    const std::string& xmlFilePath,
    const std::string& xsdPath,
    SchemaDocumentSourceType sourceType
) {
    XmlSchemaValidator validator(xsdPath);
    const auto validation = validator.validateFile(xmlFilePath);
    if (!validation.ok()) {
        return failure(fromValidationErrors(validation.errors));
    }

    const auto xmlContent = readFile(xmlFilePath);
    if (xmlContent.empty()) {
        return failure({makeError(
            "QORNIX_SCHEMA_DOCUMENT_READ_ERROR",
            "Cannot read XML file: " + xmlFilePath
        )});
    }

    auto result = parseString(xmlContent, xmlFilePath, xsdPath, sourceType);
    if (result.document != nullptr) {
        result.document->sourceMetadata_.sourcePath = xmlFilePath;
    }
    return result;
}

SchemaDocumentParseResult SchemaDocument::parseString(
    const std::string& xmlContent,
    const std::string& documentName,
    const std::string& xsdPath,
    SchemaDocumentSourceType sourceType
) {
    XmlSchemaValidator validator(xsdPath);
    const auto validation = validator.validateString(xmlContent, documentName);
    if (!validation.ok()) {
        return failure(fromValidationErrors(validation.errors));
    }

    pugi::xml_document xml;
    const auto parseResult = xml.load_string(xmlContent.c_str());
    if (!parseResult) {
        return failure({makeError(
            "QORNIX_SCHEMA_DOCUMENT_PARSE_ERROR",
            parseResult.description()
        )});
    }

    const auto appNode = xml.child("Application");
    if (!appNode) {
        return failure({makeError(
            "QORNIX_SCHEMA_DOCUMENT_ROOT_ERROR",
            "Root element must be <Application>"
        )});
    }

    auto document = std::make_unique<SchemaDocument>();
    document->sourceMetadata_.type = sourceType;
    document->sourceMetadata_.documentName = documentName;
    document->sourceMetadata_.schemaFormatVersion = attr(appNode, "schemaFormatVersion", "1.0");

    document->metadata_.name = appNode.child("Name").text().as_string();
    document->metadata_.version = appNode.child("Version").text().as_string();
    document->metadata_.description = appNode.child("Description").text().as_string();

    const auto configNode = appNode.child("Configuration");
    document->configuration_.dbConnectionString = configNode.child("DbConnectionString").text().as_string();
    document->configuration_.logLevel = configNode.child("LogLevel").text().as_string("INFO");
    document->configuration_.timeout = configNode.child("Timeout").text().as_int(30);
    document->configuration_.databaseEngine = configNode.child("DatabaseEngine").text().as_string();
    document->sourceMetadata_.databaseEngine = document->configuration_.databaseEngine;
    document->metadata_.configuration = document->configuration_;

    const auto dataStructureNode = appNode.child("DataStructure");
    for (const auto entityNode : dataStructureNode.children("Entity")) {
        document->entities_.push_back(parseEntity(entityNode));
    }

    const auto viewsNode = dataStructureNode.child("Views");
    for (const auto viewNode : viewsNode.children("View")) {
        document->views_.push_back(parseView(viewNode));
    }

    const auto materializedViewsNode = dataStructureNode.child("MaterializedViews");
    for (const auto viewNode : materializedViewsNode.children("MaterializedView")) {
        document->materializedViews_.push_back(parseMaterializedView(viewNode));
    }

    const auto databaseFunctionsNode = dataStructureNode.child("DatabaseFunctions");
    for (const auto functionNode : databaseFunctionsNode.children("DatabaseFunction")) {
        document->databaseFunctions_.push_back(parseDatabaseFunction(functionNode));
    }

    const auto storedProceduresNode = dataStructureNode.child("StoredProcedures");
    for (const auto procedureNode : storedProceduresNode.children("StoredProcedure")) {
        document->storedProcedures_.push_back(parseStoredProcedure(procedureNode));
    }

    const auto triggersNode = dataStructureNode.child("Triggers");
    for (const auto triggerNode : triggersNode.children("Trigger")) {
        document->triggers_.push_back(parseTrigger(triggerNode));
    }

    const auto sequencesNode = dataStructureNode.child("Sequences");
    for (const auto sequenceNode : sequencesNode.children("Sequence")) {
        document->sequences_.push_back(parseSequence(sequenceNode));
    }

    const auto databaseMappingNode = appNode.child("DatabaseMapping");
    for (const auto entityMappingNode : databaseMappingNode.children("EntityMapping")) {
        EntityMappingDefinition entityMapping;
        entityMapping.entityName = attr(entityMappingNode, "entityName");
        entityMapping.tableName = attr(entityMappingNode, "tableName");
        for (const auto fieldMappingNode : entityMappingNode.children("FieldMapping")) {
            FieldMappingDefinition fieldMapping;
            fieldMapping.fieldName = attr(fieldMappingNode, "fieldName");
            fieldMapping.columnName = attr(fieldMappingNode, "columnName");
            fieldMapping.postgresqlType = attr(fieldMappingNode, "postgresqlType");
            fieldMapping.mysqlType = attr(fieldMappingNode, "mysqlType");
            fieldMapping.sqliteType = attr(fieldMappingNode, "sqliteType");
            entityMapping.fieldMappings.push_back(std::move(fieldMapping));
        }
        document->databaseMapping_.entityMappings.push_back(std::move(entityMapping));
    }

    const auto typeMappingNode = databaseMappingNode.child("TypeMapping");
    for (const auto typeMapNode : typeMappingNode.children("TypeMap")) {
        TypeMapDefinition typeMap;
        typeMap.appType = attr(typeMapNode, "appType");
        typeMap.postgresqlType = attr(typeMapNode, "postgresqlType");
        typeMap.mysqlType = attr(typeMapNode, "mysqlType");
        typeMap.sqliteType = attr(typeMapNode, "sqliteType");
        document->databaseMapping_.typeMapping.typeMaps.push_back(std::move(typeMap));
    }
    document->metadata_.databaseMapping = document->databaseMapping_;

    SchemaDocumentParseResult result;
    result.success = true;
    result.document = std::move(document);
    return result;
}

std::map<std::string, EntityDefinition> SchemaDocument::entityMapByName() const {
    std::map<std::string, EntityDefinition> result;
    for (const auto& entity : entities_) {
        result[entity.name] = entity;
    }
    return result;
}

SchemaSnapshot SchemaDocument::toSnapshot() const {
    SchemaSnapshot snapshot;
    snapshot.metadata = metadata_;
    snapshot.entities = entityMapByName();
    snapshot.views = views_;
    snapshot.materializedViews = materializedViews_;
    snapshot.storedProcedures = storedProcedures_;
    snapshot.triggers = triggers_;
    snapshot.databaseFunctions = databaseFunctions_;
    snapshot.sequences = sequences_;
    return snapshot;
}

std::string SchemaDocument::toCanonicalXml() const {
    pugi::xml_document xml;
    auto declaration = xml.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    auto appNode = xml.append_child("Application");
    appNode.append_attribute("schemaFormatVersion") = sourceMetadata_.schemaFormatVersion.empty()
        ? "1.0"
        : sourceMetadata_.schemaFormatVersion.c_str();

    appendText(appNode, "Name", metadata_.name);
    appendText(appNode, "Version", metadata_.version);
    appendTextIfNotEmpty(appNode, "Description", metadata_.description);

    auto configNode = appNode.append_child("Configuration");
    appendText(configNode, "DbConnectionString", configuration_.dbConnectionString);
    appendText(configNode, "LogLevel", configuration_.logLevel.empty() ? "INFO" : configuration_.logLevel);
    appendText(configNode, "Timeout", std::to_string(configuration_.timeout));
    appendTextIfNotEmpty(configNode, "DatabaseEngine", configuration_.databaseEngine);

    auto dataStructureNode = appNode.append_child("DataStructure");
    for (const auto& entity : entities_) {
        appendEntity(dataStructureNode, entity);
    }
    appendViews(dataStructureNode, views_);
    appendMaterializedViews(dataStructureNode, materializedViews_);
    appendDatabaseFunctions(dataStructureNode, databaseFunctions_);
    appendStoredProcedures(dataStructureNode, storedProcedures_);
    appendTriggers(dataStructureNode, triggers_);
    appendSequences(dataStructureNode, sequences_);

    if (!databaseMapping_.entityMappings.empty() || !databaseMapping_.typeMapping.typeMaps.empty()) {
        auto databaseMappingNode = appNode.append_child("DatabaseMapping");
        for (const auto& entityMapping : databaseMapping_.entityMappings) {
            auto entityMappingNode = databaseMappingNode.append_child("EntityMapping");
            entityMappingNode.append_attribute("entityName") = entityMapping.entityName.c_str();
            entityMappingNode.append_attribute("tableName") = entityMapping.tableName.c_str();
            for (const auto& fieldMapping : entityMapping.fieldMappings) {
                auto fieldMappingNode = entityMappingNode.append_child("FieldMapping");
                fieldMappingNode.append_attribute("fieldName") = fieldMapping.fieldName.c_str();
                fieldMappingNode.append_attribute("columnName") = fieldMapping.columnName.c_str();
                appendAttributeIfNotEmpty(fieldMappingNode, "postgresqlType", fieldMapping.postgresqlType);
                appendAttributeIfNotEmpty(fieldMappingNode, "mysqlType", fieldMapping.mysqlType);
                appendAttributeIfNotEmpty(fieldMappingNode, "sqliteType", fieldMapping.sqliteType);
            }
        }

        if (!databaseMapping_.typeMapping.typeMaps.empty()) {
            auto typeMappingNode = databaseMappingNode.append_child("TypeMapping");
            for (const auto& typeMap : databaseMapping_.typeMapping.typeMaps) {
                auto typeMapNode = typeMappingNode.append_child("TypeMap");
                typeMapNode.append_attribute("appType") = typeMap.appType.c_str();
                appendAttributeIfNotEmpty(typeMapNode, "postgresqlType", typeMap.postgresqlType);
                appendAttributeIfNotEmpty(typeMapNode, "mysqlType", typeMap.mysqlType);
                appendAttributeIfNotEmpty(typeMapNode, "sqliteType", typeMap.sqliteType);
            }
        }
    }

    std::ostringstream output;
    xml.save(output, "    ", pugi::format_default, pugi::encoding_utf8);
    return output.str();
}

bool SchemaDocument::saveCanonicalXml(const std::string& outputFilePath) const {
    std::ofstream output(outputFilePath, std::ios::binary);
    if (!output) {
        return false;
    }
    output << toCanonicalXml();
    return output.good();
}
