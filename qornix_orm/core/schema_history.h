/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <string>
#include <vector>

#include "schema_applier.h"
#include "schema_plan.h"

struct SchemaHistoryRecord {
    std::string id;
    std::string timestamp;
    std::string planId;
    std::string schemaFormatVersion;
    std::string desiredSchemaHash;
    std::string currentSchemaHashBefore;
    std::string currentSchemaHashAfter;
    std::string appliedBy;
    bool success = false;
    bool dryRun = false;
    std::string resultMessage;
    std::string sqlPreview;
    std::string planJson;
    std::string resultJson;
};

struct SchemaHistoryAppendResult {
    bool success = false;
    std::string recordId;
    std::string message;
};

/**
 * Lightweight append-only history store for schema plans/apply results.
 *
 * The first implementation is file-backed JSON lines so schema history exists
 * independently of the UI/application layer. A future application can expose it
 * through HTTP without re-implementing audit trail logic.
 */
class SchemaHistoryStore {
public:
    explicit SchemaHistoryStore(std::string historyFilePath = "schema_history.jsonl");

    const std::string& historyFilePath() const;

    SchemaHistoryAppendResult append(
        const SchemaPlan& plan,
        const SchemaApplyResult& result,
        const std::string& appliedBy = "unknown",
        const std::string& desiredSchemaHash = "",
        const std::string& currentSchemaHashBefore = "",
        const std::string& currentSchemaHashAfter = "",
        const std::string& schemaFormatVersion = "1.0"
    ) const;

    std::vector<SchemaHistoryRecord> readAll() const;

    static std::string buildRecordId(const SchemaPlan& plan);
    static std::string nowIso8601();
    static std::string recordToJson(const SchemaHistoryRecord& record);

private:
    std::string historyFilePath_;
};
