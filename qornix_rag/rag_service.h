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
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct RagServiceSourceInfo {
    std::string id;
    std::string type;
    std::string name;
    size_t document_count = 0;
};

struct RagServiceSearchItem {
    std::string path;
    std::string source_path;
    std::string citation_id;
    std::string type;
    std::string language;
    double score = 0.0;
    double confidence = 0.0;
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
    std::string expanded_query;
    bool query_expansion_applied = false;
    bool reranking_applied = false;
    std::vector<RagServiceSearchItem> results;
    long long response_time_ms = 0;
};

struct RagServiceAskContextItem {
    std::string path;
    std::string source_path;
    std::string citation_id;
    double score = 0.0;
    double confidence = 0.0;
    std::string snippet;
    std::string source_type = "project";
    std::string category;
    std::string pair_id;
};

struct RagServiceAskResponse {
    bool success = true;
    std::string question;
    std::string expanded_query;
    bool query_expansion_applied = false;
    bool reranking_applied = false;
    std::string answer;
    std::vector<RagServiceAskContextItem> context;
    std::vector<std::string> sources;
    std::vector<std::string> citations;
    double retrieval_confidence = 0.0;
    std::string grounding_status = "no_context";
    std::string llm_status = "not_configured";
    long long response_time_ms = 0;
};

struct RagServiceIndexResponse {
    bool success = false;
    std::string message;
    ProjectStats stats;
};

struct RagServiceIngestionJob {
    std::string id;
    std::string source_id;
    std::string root_path;
    std::string status;
    size_t files_seen = 0;
    size_t documents_imported = 0;
    size_t duplicates_found = 0;
    size_t skipped = 0;
    size_t errors = 0;
    int progress_percent = 0;
    bool background = false;
    std::string error_message;
    std::string started_at;
    std::string finished_at;
};

struct RagServiceIngestResponse {
    bool success = false;
    std::string message;
    RagServiceIngestionJob job;
    ProjectStats stats;
};

struct RagServiceQaListOptions {
    std::string query;
    std::string category;
    std::string source_id;
    size_t limit = 25;
    size_t offset = 0;
};

struct RagServiceQaListResponse {
    bool success = true;
    std::string source_id;
    std::vector<qornix::rag::QASource::QAPair> items;
    size_t total = 0;
    size_t limit = 25;
    size_t offset = 0;
    bool has_more = false;
};

struct RagServiceQaSuggestion {
    std::string id;
    std::string question;
    std::string category;
};

struct RagServiceHealth {
    std::string status = "ok";
    bool indexed = false;
    size_t files = 0;
    size_t lines = 0;
    std::string embedding_backend;
    std::string embedding_model_id;
    std::string embedding_model_name;
    size_t embedding_dim = 0;
    std::string vector_store_backend;
    std::string vector_store_status;
    bool hybrid_search = true;
    bool query_expansion = true;
    bool reranking = true;
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
    RagServiceIngestResponse ingestProject(const std::optional<std::string>& project_path = std::nullopt);
    RagServiceIngestResponse startBackgroundIngestProject(const std::optional<std::string>& project_path = std::nullopt);
    std::optional<RagServiceIngestionJob> findIngestionJob(const std::string& job_id) const;
    std::vector<RagServiceIngestionJob> listIngestionJobs(size_t limit = 20) const;
    bool deletePersistedDocument(const std::string& relative_path,
                                 const std::string& source_id = "");
    RagServiceSearchResponse search(const std::string& query, size_t top_k = 10);
    RagServiceAskResponse ask(const std::string& question, size_t top_k = 5, const std::string& client_ip = "");
    RagServiceHealth health() const;
    std::vector<RagServiceSourceInfo> sources() const;

    std::vector<qornix::rag::QASource::QAPair> listQaPairs(size_t page = 1,
                                                           size_t per_page = 20,
                                                           const std::string& source_id = "") const;
    RagServiceQaListResponse listQaPairs(const RagServiceQaListOptions& options) const;
    std::vector<RagServiceQaSuggestion> suggestQaPairs(const std::string& query,
                                                       size_t limit = 10,
                                                       const std::string& source_id = "") const;
    std::vector<std::string> listQaCategories(const std::string& query = "",
                                              size_t limit = 50,
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
    RagServiceIngestResponse runIngestionJob(RagServiceIngestionJob job, const std::string& path);
    void updateActiveIngestionJob(const RagServiceIngestionJob& job);
    void removeActiveIngestionJob(const std::string& job_id);

    mutable std::mutex ingestion_jobs_mutex_;
    std::unordered_map<std::string, RagServiceIngestionJob> active_ingestion_jobs_;
};
