/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "schema_history.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string quoteJson(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const auto ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch;
        }
    }
    out << '"';
    return out.str();
}

std::string readJsonValue(const std::string& line, const std::string& key) {
    const auto needle = "\"" + key + "\":";
    const auto pos = line.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    auto start = pos + needle.size();
    while (start < line.size() && line[start] == ' ') {
        ++start;
    }
    if (start >= line.size()) {
        return {};
    }
    if (line[start] == '"') {
        ++start;
        std::ostringstream value;
        bool escaped = false;
        for (std::size_t i = start; i < line.size(); ++i) {
            const auto ch = line[i];
            if (escaped) {
                value << ch;
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                break;
            }
            value << ch;
        }
        return value.str();
    }
    auto end = line.find_first_of(",}", start);
    if (end == std::string::npos) {
        end = line.size();
    }
    return line.substr(start, end - start);
}

bool readJsonBool(const std::string& line, const std::string& key) {
    return readJsonValue(line, key) == "true";
}

} // namespace

SchemaHistoryStore::SchemaHistoryStore(std::string historyFilePath)
    : historyFilePath_(std::move(historyFilePath)) {}

const std::string& SchemaHistoryStore::historyFilePath() const {
    return historyFilePath_;
}

SchemaHistoryAppendResult SchemaHistoryStore::append(
    const SchemaPlan& plan,
    const SchemaApplyResult& result,
    const std::string& appliedBy,
    const std::string& desiredSchemaHash,
    const std::string& currentSchemaHashBefore,
    const std::string& currentSchemaHashAfter,
    const std::string& schemaFormatVersion
) const {
    SchemaHistoryRecord record;
    record.id = buildRecordId(plan);
    record.timestamp = nowIso8601();
    record.planId = plan.planId;
    record.schemaFormatVersion = schemaFormatVersion;
    record.desiredSchemaHash = desiredSchemaHash;
    record.currentSchemaHashBefore = currentSchemaHashBefore;
    record.currentSchemaHashAfter = currentSchemaHashAfter;
    record.appliedBy = appliedBy;
    record.success = result.success;
    record.dryRun = result.dryRun;
    record.resultMessage = result.message;
    record.sqlPreview = plan.toSqlPreview();
    record.planJson = plan.toJsonString();
    record.resultJson = result.toJsonString();

    std::ofstream file(historyFilePath_, std::ios::app);
    if (!file) {
        return {false, {}, "Failed to open schema history file: " + historyFilePath_};
    }
    file << recordToJson(record) << '\n';
    return {true, record.id, "Schema history record appended."};
}

std::vector<SchemaHistoryRecord> SchemaHistoryStore::readAll() const {
    std::vector<SchemaHistoryRecord> records;
    std::ifstream file(historyFilePath_);
    if (!file) {
        return records;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        SchemaHistoryRecord record;
        record.id = readJsonValue(line, "id");
        record.timestamp = readJsonValue(line, "timestamp");
        record.planId = readJsonValue(line, "planId");
        record.schemaFormatVersion = readJsonValue(line, "schemaFormatVersion");
        record.desiredSchemaHash = readJsonValue(line, "desiredSchemaHash");
        record.currentSchemaHashBefore = readJsonValue(line, "currentSchemaHashBefore");
        record.currentSchemaHashAfter = readJsonValue(line, "currentSchemaHashAfter");
        record.appliedBy = readJsonValue(line, "appliedBy");
        record.success = readJsonBool(line, "success");
        record.dryRun = readJsonBool(line, "dryRun");
        record.resultMessage = readJsonValue(line, "resultMessage");
        records.push_back(std::move(record));
    }
    return records;
}

std::string SchemaHistoryStore::buildRecordId(const SchemaPlan& plan) {
    const auto seed = plan.planId + nowIso8601();
    std::ostringstream out;
    out << "schema_history_" << std::hex << std::hash<std::string>{}(seed);
    return out.str();
}

std::string SchemaHistoryStore::nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string SchemaHistoryStore::recordToJson(const SchemaHistoryRecord& record) {
    std::ostringstream out;
    out << '{'
        << "\"id\":" << quoteJson(record.id) << ','
        << "\"timestamp\":" << quoteJson(record.timestamp) << ','
        << "\"planId\":" << quoteJson(record.planId) << ','
        << "\"schemaFormatVersion\":" << quoteJson(record.schemaFormatVersion) << ','
        << "\"desiredSchemaHash\":" << quoteJson(record.desiredSchemaHash) << ','
        << "\"currentSchemaHashBefore\":" << quoteJson(record.currentSchemaHashBefore) << ','
        << "\"currentSchemaHashAfter\":" << quoteJson(record.currentSchemaHashAfter) << ','
        << "\"appliedBy\":" << quoteJson(record.appliedBy) << ','
        << "\"success\":" << (record.success ? "true" : "false") << ','
        << "\"dryRun\":" << (record.dryRun ? "true" : "false") << ','
        << "\"resultMessage\":" << quoteJson(record.resultMessage) << ','
        << "\"sqlPreview\":" << quoteJson(record.sqlPreview) << ','
        << "\"planJson\":" << quoteJson(record.planJson) << ','
        << "\"resultJson\":" << quoteJson(record.resultJson)
        << '}';
    return out.str();
}
