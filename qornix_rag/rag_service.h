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


struct RagServiceRetrievalQuery {
    std::string query;
    std::string origin;
    size_t result_count = 0;
};

struct RagServiceGroundingClaim {
    std::string claim;
    std::string status;
    double support_score = 0.0;
    std::vector<std::string> citations;
};

struct RagServiceFeedbackEntry {
    std::string id;
    std::string request_id;
    std::string query;
    std::string question;
    std::string answer;
    std::string rating;
    std::string category;
    std::string comment;
    std::vector<std::string> citations;
    std::string client_ip;
};

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
    std::vector<std::string> tags;
    std::string attribution;
    std::string source_locator;
    std::string citation_label;
    std::map<std::string, std::string> metadata;
};

struct RagServiceSearchResponse {
    bool success = true;
    std::string query;
    std::string expanded_query;
    bool query_expansion_applied = false;
    bool reranking_applied = false;
    bool filters_applied = false;
    bool multi_query_applied = false;
    std::string rewritten_query;
    std::string retrieval_strategy;
    std::string reranker_type;
    std::vector<RagServiceRetrievalQuery> retrieval_queries;
    std::vector<MetadataFilter> filters;
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
    std::vector<std::string> tags;
    std::string attribution;
    std::string source_locator;
    std::string citation_label;
    std::map<std::string, std::string> metadata;
};

struct RagServiceConversationTurn {
    std::string role;
    std::string content;
};

struct RagServiceAskResponse {
    bool success = true;
    std::string question;
    std::string expanded_query;
    bool query_expansion_applied = false;
    bool reranking_applied = false;
    bool filters_applied = false;
    bool multi_query_applied = false;
    std::string rewritten_query;
    std::string retrieval_strategy;
    std::string reranker_type;
    std::vector<RagServiceRetrievalQuery> retrieval_queries;
    std::vector<MetadataFilter> filters;
    std::string answer;
    std::vector<RagServiceAskContextItem> context;
    std::vector<std::string> sources;
    std::vector<std::string> citations;
    std::vector<std::string> answer_citations;
    std::vector<std::string> missing_citations;
    std::vector<std::string> uncited_context_citations;
    bool citations_post_processed = false;
    double retrieval_confidence = 0.0;
    std::string grounding_status = "no_context";
    std::string grounding_evaluator = "none";
    std::string claim_grounding_status = "not_evaluated";
    std::vector<RagServiceGroundingClaim> grounded_claims;
    size_t conversation_turns_used = 0;
    std::string llm_status = "not_configured";
    std::string llm_parser_error;
    std::string llm_finish_reason;
    bool llm_truncated = false;
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
    std::string tag;
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

struct RagServiceQaImportResponse {
    bool success = false;
    size_t imported = 0;
    size_t duplicates = 0;
    size_t errors = 0;
    std::vector<std::string> imported_ids;
    std::vector<std::string> error_messages;
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
    std::string embedding_active_model_id;
    size_t embedding_registry_size = 0;
    std::vector<std::string> embedding_registry_model_ids;
    std::vector<std::string> embedding_registry_warnings;
    std::string vector_store_backend;
    std::string vector_store_status;
    std::string vector_store_health_status;
    std::string vector_store_health_detail;
    bool vector_store_ready = false;
    size_t vector_store_size = 0;
    size_t vector_store_dimension = 0;
    bool hybrid_search = true;
    bool query_expansion = true;
    bool multi_query_retrieval = true;
    bool embedding_reranker = true;
    bool reranking = true;
    XapianSearchDiagnostics xapian;
    bool llm_available = false; // provider reachable
    bool llm_ready = false; // provider reachable and configured model usable
    bool llm_provider_available = false;
    std::string llm_provider = "not_configured";
    std::string llm_model = "not_configured";
    bool configured_model_available = false;
    std::vector<std::string> available_models;
    std::string llm_api_url = "N/A";
    std::string llm_status = "not_configured";
    int llm_response_time_ms = -1;
};

struct RagServiceEmbeddingModelItem {
    std::string id;
    std::string backend;
    std::string name;
    std::string version;
    std::string model_path;
    std::string tokenizer_path;
    std::string tokenizer_type;
    std::string pooling;
    size_t dimension = 0;
    size_t max_seq_len = 0;
    size_t tokenizer_vocab_size = 0;
    size_t effective_chunk_token_limit = 0;
    bool active = false;
    bool ready = false;
    bool discovered = false;
    bool files_present = false;
    bool persistent_cache_enabled = false;
    std::string status;
    std::string tokenizer_status;
    std::string model_status;
    std::string model_signature;
    std::string source;
};

struct RagServiceEmbeddingModelsResponse {
    bool success = true;
    std::string active_model_id;
    std::string effective_model_id;
    std::string backend;
    std::vector<RagServiceEmbeddingModelItem> models;
    std::vector<std::string> warnings;
};

struct RagServiceEmbeddingSwitchResponse {
    bool success = false;
    std::string message;
    std::string previous_model_id;
    std::string active_model_id;
    std::string effective_model_id;
    bool reindex_required = true;
    bool reindexed = false;
    bool force_reembed = false;
    ProjectStats stats;
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

    RagServiceIndexResponse indexProject(const std::optional<std::string>& scan_path = std::nullopt);
    RagServiceIngestResponse ingestProject(const std::optional<std::string>& scan_path = std::nullopt);
    RagServiceIngestResponse startBackgroundIngestProject(const std::optional<std::string>& scan_path = std::nullopt);
    std::optional<RagServiceIngestionJob> findIngestionJob(const std::string& job_id) const;
    std::vector<RagServiceIngestionJob> listIngestionJobs(size_t limit = 20) const;
    bool deletePersistedDocument(const std::string& relative_path,
                                 const std::string& source_id = "");
    RagServiceSearchResponse search(const std::string& query, size_t top_k = 10);
    RagServiceSearchResponse search(const std::string& query,
                                    size_t top_k,
                                    const std::vector<MetadataFilter>& filters);
    RagServiceAskResponse ask(const std::string& question, size_t top_k = 5, const std::string& client_ip = "");
    RagServiceAskResponse ask(const std::string& question,
                              size_t top_k,
                              const std::string& client_ip,
                              const std::vector<RagServiceConversationTurn>& history);
    RagServiceAskResponse ask(const std::string& question,
                              size_t top_k,
                              const std::string& client_ip,
                              const std::vector<RagServiceConversationTurn>& history,
                              const std::vector<MetadataFilter>& filters,
                              const std::optional<std::string>& system_prompt = std::nullopt,
                              const std::optional<std::string>& prompt_template = std::nullopt);
    RagServiceHealth health() const;
    std::vector<RagServiceSourceInfo> sources() const;
    RagServiceEmbeddingModelsResponse embeddingModels() const;
    RagServiceEmbeddingSwitchResponse switchEmbeddingModel(const std::string& model_id,
                                                           bool reindex = false,
                                                           bool force_reembed = true,
                                                           const std::optional<std::string>& scan_path = std::nullopt);

    bool recordFeedback(const RagServiceFeedbackEntry& feedback);

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
    std::vector<std::string> listQaTags(const std::string& query = "",
                                        size_t limit = 50,
                                        const std::string& source_id = "") const;
    bool addQaPair(const std::string& question,
                   const std::string& answer,
                   const std::string& category = "general",
                   std::string* pair_id = nullptr,
                   const std::vector<std::string>& tags = {},
                   const std::vector<std::string>& aliases = {},
                   const std::map<std::string, std::string>& metadata = {});
    bool updateQaPair(const std::string& pair_id,
                      const std::string& question,
                      const std::string& answer,
                      const std::string& category,
                      const std::vector<std::string>& tags = {},
                      const std::vector<std::string>& aliases = {},
                      const std::map<std::string, std::string>& metadata = {});
    bool deleteQaPair(const std::string& pair_id);
    std::string exportQaPairsJson(const RagServiceQaListOptions& options = {}) const;
    RagServiceQaImportResponse importQaPairsJson(const std::string& json,
                                                 const std::string& default_category = "general");

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
