/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_service.h"

#include "schema_loader.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace qornix_dynamic_api {

namespace {

bool writeUploadedSchemaXml(const std::string& path, const std::string& xml, std::string& error) {
    if (path.empty()) {
        error = "Uploaded schema path is empty";
        return false;
    }
    try {
        std::filesystem::path output(path);
        if (!output.parent_path().empty()) {
            std::filesystem::create_directories(output.parent_path());
        }
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = "Failed to open uploaded schema path for writing: " + path;
            return false;
        }
        file << xml;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

DynamicApiResponse schemaApplyConflict(
    const std::string& message,
    const SchemaPlan& plan,
    const SchemaDocumentDiff& diff,
    boost::json::value (*parseJsonStringOrString)(const std::string&)
) {
    boost::json::object payload = okEnvelope(message);
    payload["ok"] = false;
    payload["plan_id"] = plan.planId;
    payload["diff"] = parseJsonStringOrString(diff.toJsonString());
    payload["plan"] = parseJsonStringOrString(plan.toJsonString());
    payload["sql_preview"] = plan.toSqlPreview();
    DynamicApiResponse blocked;
    blocked.status = boost::beast::http::status::conflict;
    blocked.message = message;
    blocked.body = payload;
    return blocked;
}

} // namespace

SchemaService::SchemaService(DynamicApiConfig config)
    : config_(std::move(config)) {}

std::unique_ptr<DatabaseInterface> SchemaService::openDatabase() const {
    return DatabaseInterface::init(config_.databaseConfig);
}

DriverCapabilities SchemaService::capabilitiesFor(DatabaseInterface& database) const {
    auto dbConfig = database.getDatabaseConfig();
    return DriverCapabilities::fromDriverName(dbConfig.driver.empty() ? config_.databaseConfig.driver : dbConfig.driver);
}

boost::json::array SchemaService::validationErrorsToJson(const XmlValidationResult& result) {
    boost::json::array errors;
    for (const auto& error : result.errors) {
        boost::json::object item;
        item["line"] = static_cast<std::uint64_t>(error.line);
        item["column"] = static_cast<std::uint64_t>(error.column);
        item["path"] = error.elementPath;
        item["code"] = error.code;
        item["message"] = error.message;
        errors.emplace_back(item);
    }
    return errors;
}

boost::json::array SchemaService::parseErrorsToJson(const SchemaDocumentParseResult& result) {
    boost::json::array errors;
    for (const auto& error : result.errors) {
        boost::json::object item;
        item["line"] = static_cast<std::uint64_t>(error.line);
        item["column"] = static_cast<std::uint64_t>(error.column);
        item["path"] = error.elementPath;
        item["code"] = error.code;
        item["message"] = error.message;
        errors.emplace_back(item);
    }
    return errors;
}

boost::json::value SchemaService::parseJsonStringOrString(const std::string& jsonText) {
    try {
        return boost::json::parse(jsonText);
    } catch (...) {
        return boost::json::string(jsonText);
    }
}

DynamicApiResponse SchemaService::parseDesired(const std::string& xml, std::unique_ptr<SchemaDocument>& desired) const {
    if (xml.empty()) {
        return DynamicApiResponse::error(boost::beast::http::status::bad_request, "empty_xml", "XML schema body is empty");
    }

    auto parseResult = SchemaDocument::parseString(
        xml,
        "uploaded_schema.xml",
        config_.schemaXsdPath,
        SchemaDocumentSourceType::UploadedXml
    );

    if (!parseResult.ok()) {
        boost::json::object payload = okEnvelope("XML schema validation failed");
        payload["ok"] = false;
        payload["errors"] = parseErrorsToJson(parseResult);
        DynamicApiResponse response;
        response.status = boost::beast::http::status::bad_request;
        response.message = "XML schema validation failed";
        response.body = payload;
        return response;
    }

    desired = std::move(parseResult.document);
    return DynamicApiResponse::ok(okEnvelope("XML schema parsed"));
}

DynamicApiResponse SchemaService::exportSchemaXml() const {
    try {
        auto database = openDatabase();
        auto snapshotResult = DatabaseIntrospector::introspect(*database);
        if (!snapshotResult.ok()) {
            boost::json::object payload = okEnvelope("Database introspection failed");
            payload["ok"] = false;
            boost::json::array errors;
            for (const auto& error : snapshotResult.errors) {
                boost::json::object item;
                item["code"] = error.code;
                item["message"] = error.message;
                item["context"] = error.context;
                errors.emplace_back(item);
            }
            payload["errors"] = errors;
            return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "snapshot_failed", boost::json::serialize(payload));
        }

        DatabaseSchemaExportOptions options;
        options.applicationName = "Qornix Dynamic API";
        options.sourceDocumentName = "database_export";
        auto xml = DatabaseSchemaExporter::exportSnapshotToCanonicalXml(snapshotResult.snapshot, options);
        if (!config_.exportedSchemaPath.empty()) {
            std::filesystem::path output(config_.exportedSchemaPath);
            if (!output.parent_path().empty()) {
                std::filesystem::create_directories(output.parent_path());
            }
            std::ofstream file(output, std::ios::binary | std::ios::trunc);
            file << xml;
        }
        DynamicApiResponse response;
        response.status = boost::beast::http::status::ok;
        response.message = "Schema exported";
        response.body = xml;
        return response;
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "schema_export_failed", e.what());
    }
}

DynamicApiResponse SchemaService::validateXml(const std::string& xml) const {
    try {
        XmlSchemaValidator validator(config_.schemaXsdPath);
        auto result = validator.validateString(xml, "uploaded_schema.xml");
        boost::json::object payload = okEnvelope(result.ok() ? "XML schema is valid" : "XML schema is invalid");
        payload["ok"] = result.ok();
        payload["valid"] = result.ok();
        payload["schema_path"] = result.schemaPath;
        payload["errors"] = validationErrorsToJson(result);
        DynamicApiResponse response;
        response.status = result.ok() ? boost::beast::http::status::ok : boost::beast::http::status::bad_request;
        response.message = result.ok() ? "XML schema is valid" : "XML schema is invalid";
        response.body = payload;
        return response;
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "schema_validate_failed", e.what());
    }
}

std::unique_ptr<SchemaDocument> SchemaService::currentSchemaDocument() const {
    auto database = openDatabase();
    auto snapshotResult = DatabaseIntrospector::introspect(*database);
    if (!snapshotResult.ok()) {
        throw std::runtime_error("database introspection failed");
    }

    DatabaseSchemaExportOptions options;
    options.applicationName = "Qornix Dynamic API";
    options.sourceDocumentName = "current_database";
    auto exportResult = DatabaseSchemaExporter::exportSnapshot(snapshotResult.snapshot, options);
    if (!exportResult.ok() || !exportResult.document) {
        throw std::runtime_error("database schema export failed");
    }
    return std::move(exportResult.document);
}

DynamicApiResponse SchemaService::diffXml(const std::string& xml) const {
    try {
        std::unique_ptr<SchemaDocument> desired;
        auto parseResponse = parseDesired(xml, desired);
        if (!desired) {
            return parseResponse;
        }

        auto current = currentSchemaDocument();
        auto diff = SchemaDiffEngine::compare(*desired, *current);

        boost::json::object payload = okEnvelope(diff.hasChanges() ? "Schema diff contains changes" : "Schema diff is empty");
        payload["has_changes"] = diff.hasChanges();
        payload["operation_count"] = static_cast<std::uint64_t>(diff.operations.size());
        payload["diff"] = parseJsonStringOrString(diff.toJsonString());
        payload["text"] = diff.toText();
        return DynamicApiResponse::ok(payload, "Schema diff built");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "schema_diff_failed", e.what());
    }
}

DynamicApiResponse SchemaService::buildDiffAndPlan(const std::string& xml, SchemaDocumentDiff& diff, SchemaPlan& plan) const {
    std::unique_ptr<SchemaDocument> desired;
    auto parseResponse = parseDesired(xml, desired);
    if (!desired) {
        return parseResponse;
    }

    auto database = openDatabase();
    auto current = currentSchemaDocument();
    diff = SchemaDiffEngine::compare(*desired, *current);

    auto capabilities = capabilitiesFor(*database);
    SchemaPlanOptions options;
    options.includeUnsupportedOperations = true;
    options.includeManualReviewOperations = true;
    options.includeSqlPreview = true;
    options.dryRun = true;
    plan = SchemaPlanner::buildPlan(diff, capabilities, SchemaPolicy::defaultPolicy(), options);
    return DynamicApiResponse::ok(okEnvelope("Schema plan built"));
}

DynamicApiResponse SchemaService::planXml(const SchemaPlanRequest& request) const {
    try {
        SchemaDocumentDiff diff;
        SchemaPlan plan;
        auto response = buildDiffAndPlan(request.xml, diff, plan);
        if (response.status != boost::beast::http::status::ok) {
            return response;
        }

        boost::json::object payload = okEnvelope("Schema plan built");
        payload["plan_id"] = plan.planId;
        payload["operation_count"] = static_cast<std::uint64_t>(plan.operations.size());
        payload["requires_confirmation"] = plan.needsConfirmation();
        payload["contains_destructive_operations"] = plan.hasDestructiveOperations();
        payload["contains_unsupported_operations"] = plan.hasUnsupportedOperations();
        payload["contains_manual_review_operations"] = plan.hasManualReviewOperations();
        payload["diff"] = parseJsonStringOrString(diff.toJsonString());
        payload["plan"] = parseJsonStringOrString(plan.toJsonString());
        payload["sql_preview"] = plan.toSqlPreview();
        return DynamicApiResponse::ok(payload, "Schema plan built");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "schema_plan_failed", e.what());
    }
}

DynamicApiResponse SchemaService::applyPlan(const SchemaApplyRequest& request) const {
    try {
        SchemaDocumentDiff diff;
        SchemaPlan plan;
        auto response = buildDiffAndPlan(request.xml, diff, plan);
        if (response.status != boost::beast::http::status::ok) {
            return response;
        }

        if (!request.planId.empty() && request.planId != plan.planId) {
            return DynamicApiResponse::error(boost::beast::http::status::conflict, "plan_id_mismatch", "Submitted plan_id does not match the current schema plan");
        }

        if (plan.hasDestructiveOperations() && !config_.allowDestructiveSchemaApply && !request.destructiveConfirmed) {
            boost::json::object payload = okEnvelope("Destructive operations require explicit confirmation");
            payload["ok"] = false;
            payload["plan_id"] = plan.planId;
            payload["plan"] = parseJsonStringOrString(plan.toJsonString());
            payload["sql_preview"] = plan.toSqlPreview();
            DynamicApiResponse blocked;
            blocked.status = boost::beast::http::status::conflict;
            blocked.message = "Destructive operations require explicit confirmation";
            blocked.body = payload;
            return blocked;
        }

        if (plan.hasManualReviewOperations() && !request.manualReviewConfirmed) {
            return schemaApplyConflict(
                "Manual-review operations require explicit confirmation",
                plan,
                diff,
                &SchemaService::parseJsonStringOrString
            );
        }

        const bool allowLegacyLiveApplyForUnsupported =
            !request.dryRun && request.manualReviewConfirmed;
        if (plan.hasUnsupportedOperations() && !allowLegacyLiveApplyForUnsupported) {
            return schemaApplyConflict(
                "Unsupported operations cannot be applied automatically. For a first-run demo, reset the demo database or confirm manual review and run a live apply.",
                plan,
                diff,
                &SchemaService::parseJsonStringOrString
            );
        }

        const bool applyingWithLegacyUnsupportedFallback =
            plan.hasUnsupportedOperations() && allowLegacyLiveApplyForUnsupported;
        plan.dryRun = request.dryRun;

        SchemaApplyResult result;
        std::string applyEngine = request.dryRun ? "schema_applier_dry_run" : "schema_loader_live_apply";
        if (request.dryRun) {
            result = SchemaApplier::dryRun(plan);
        } else {
            std::string writeError;
            if (!writeUploadedSchemaXml(config_.uploadedSchemaPath, request.xml, writeError)) {
                return DynamicApiResponse::error(
                    boost::beast::http::status::internal_server_error,
                    "schema_write_failed",
                    writeError
                );
            }

            auto legacyDatabaseUnique = openDatabase();
            std::shared_ptr<DatabaseInterface> legacyDatabase(std::move(legacyDatabaseUnique));
            result.planId = plan.planId;
            result.dryRun = false;

            if (!::SchemaLoader::loadSchemaFromFile(config_.uploadedSchemaPath)) {
                result.success = false;
                result.message = "Failed to load uploaded schema through SchemaLoader.";
            } else if (!::SchemaLoader::loadSchemaFromDB(legacyDatabase)) {
                result.success = false;
                result.message = "Failed to introspect current database schema through SchemaLoader.";
            } else {
                const bool applied = ::SchemaLoader::applySchemaChangesToDB(legacyDatabase);
                result.success = applied;
                result.message = applied
                    ? "Schema plan applied. Database schema was updated."
                    : "SchemaLoader failed to apply schema changes.";
            }
        }

        SchemaHistoryStore history(config_.historyPath);
        history.append(plan, result, request.appliedBy);

        boost::json::object payload = okEnvelope(result.success ? "Schema plan applied" : "Schema plan apply failed");
        payload["success"] = result.success;
        payload["dry_run"] = result.dryRun;
        payload["apply_engine"] = applyEngine;
        payload["legacy_unsupported_fallback"] = applyingWithLegacyUnsupportedFallback;
        if (applyingWithLegacyUnsupportedFallback) {
            payload["warning"] = "The safe planner reported unsupported operations for the current SQLite diff. Because this was a confirmed live apply, Qornix used the legacy SchemaLoader bridge to apply the canonical XML schema. For a clean first-run demo, use Reset demo database before applying the bundled demo schema.";
        }
        payload["uploaded_schema_path"] = config_.uploadedSchemaPath;
        payload["plan_id"] = plan.planId;
        payload["result"] = parseJsonStringOrString(result.toJsonString());
        payload["plan"] = parseJsonStringOrString(plan.toJsonString());
        payload["sql_preview"] = plan.toSqlPreview();
        DynamicApiResponse applyResponse;
        applyResponse.status = result.success ? boost::beast::http::status::ok : boost::beast::http::status::conflict;
        applyResponse.message = result.message;
        applyResponse.body = payload;
        return applyResponse;
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "schema_apply_failed", e.what());
    }
}

DynamicApiResponse SchemaService::history() const {
    try {
        SchemaHistoryStore history(config_.historyPath);
        auto records = history.readAll();
        boost::json::object payload = okEnvelope("Schema history loaded");
        boost::json::array array;
        for (const auto& record : records) {
            array.emplace_back(parseJsonStringOrString(SchemaHistoryStore::recordToJson(record)));
        }
        payload["records"] = array;
        payload["count"] = static_cast<std::uint64_t>(array.size());
        return DynamicApiResponse::ok(payload, "Schema history loaded");
    } catch (const std::exception& e) {
        return DynamicApiResponse::error(boost::beast::http::status::internal_server_error, "schema_history_failed", e.what());
    }
}

} // namespace qornix_dynamic_api
