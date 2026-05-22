/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "core.h"
#include "llm_client.h"
#include "qa_source.h"
#if QORNIX_HAS_SQLITE
#include "sqlite_source.h"
#endif

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct RagServiceSourceInfo {
    std::string id;
    std::string type;
    std::string name;
    size_t document_count = 0;
};

struct RagServiceSearchItem {
    std::string path;
    std::string type;
    std::string language;
    double score = 0.0;
    double vector_score = 0.0;
    double text_score = 0.0;
    double fused_score = 0.0;
    std::string snippet;
    size_t lines = 0;
    size_t size = 0;
    std::string source_type = "project";
    std::string category;
    std::string pair_id;
};

struct RagServiceSearchResponse {
    bool success = true;
    std::string query;
    std::vector<RagServiceSearchItem> results;
    long long response_time_ms = 0;
};

struct RagServiceAskContextItem {
    std::string path;
    double score = 0.0;
    std::string snippet;
    std::string source_type = "project";
    std::string category;
    std::string pair_id;
};

struct RagServiceAskResponse {
    bool success = true;
    std::string question;
    std::string answer;
    std::vector<RagServiceAskContextItem> context;
    std::vector<std::string> sources;
    std::string llm_status = "not_configured";
    long long response_time_ms = 0;
};

struct RagServiceIndexResponse {
    bool success = false;
    std::string message;
    ProjectStats stats;
};

struct RagServiceHealth {
    std::string status = "ok";
    bool indexed = false;
    size_t files = 0;
    size_t lines = 0;
    std::string embedding_backend;
    bool hybrid_search = true;
    bool llm_available = false;
    std::string llm_provider = "not_configured";
    std::string llm_model = "not_configured";
    bool configured_model_available = false;
    std::vector<std::string> available_models;
    std::string llm_api_url = "N/A";
    std::string llm_status = "not_configured";
    int llm_response_time_ms = -1;
};

class RagService {
public:
    RagService(std::shared_ptr<RagEngine> engine,
               std::shared_ptr<LLMClient> llm = nullptr
#if QORNIX_HAS_SQLITE
               , std::shared_ptr<SQLiteSource> sqlite = nullptr
#endif
    );

    std::shared_ptr<RagEngine> engine() const { return rag_engine_; }
    std::shared_ptr<LLMClient> llm() const { return llm_client_; }

    RagServiceIndexResponse indexProject(const std::optional<std::string>& project_path = std::nullopt);
    RagServiceSearchResponse search(const std::string& query, size_t top_k = 10);
    RagServiceAskResponse ask(const std::string& question, size_t top_k = 5, const std::string& client_ip = "");
    RagServiceHealth health() const;
    std::vector<RagServiceSourceInfo> sources() const;

    std::vector<qornix::rag::QASource::QAPair> listQaPairs(size_t page = 1,
                                                           size_t per_page = 20,
                                                           const std::string& source_id = "") const;
    bool addQaPair(const std::string& question,
                   const std::string& answer,
                   const std::string& category = "general",
                   std::string* pair_id = nullptr);
    bool updateQaPair(const std::string& pair_id,
                      const std::string& question,
                      const std::string& answer,
                      const std::string& category);
    bool deleteQaPair(const std::string& pair_id);

private:
    std::shared_ptr<RagEngine> rag_engine_;
    std::shared_ptr<LLMClient> llm_client_;
#if QORNIX_HAS_SQLITE
    std::shared_ptr<SQLiteSource> sqlite_source_;
#endif

    std::vector<qornix::rag::QASource::QAPair> searchQa(const std::string& query, size_t limit) const;
};

