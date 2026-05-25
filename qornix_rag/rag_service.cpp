/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_service.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <sstream>
#include <thread>

using qornix::rag::QASource;

namespace {

std::string qaPairId(const std::string& source_id) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return source_id + "_qa_" + std::to_string(millis);
}

std::string ingestionJobId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "ingest_" + std::to_string(millis);
}

#if QORNIX_HAS_SQLITE
RagServiceIngestionJob serviceJob(const SQLiteSource::IngestionJobRecord& record) {
    RagServiceIngestionJob job;
    job.id = record.id;
    job.source_id = record.source_id;
    job.root_path = record.root_path;
    job.status = record.status;
    job.files_seen = record.files_seen;
    job.documents_imported = record.documents_imported;
    job.duplicates_found = record.duplicates_found;
    job.skipped = record.skipped;
    job.errors = record.errors;
    job.progress_percent = record.status == "completed" || record.status == "failed" ? 100 : 0;
    job.background = false;
    job.error_message = record.error_message;
    job.started_at = record.started_at;
    job.finished_at = record.finished_at;
    return job;
}
#endif

std::string qaPath(const QASource::QAPair& pair) {
    return "qa://" + (pair.category.empty() ? std::string("general") : pair.category) + "/" + pair.id;
}

std::string qaSnippet(const QASource::QAPair& pair) {
    return "Q: " + pair.question + "\nA: " + pair.answer;
}

double confidenceFromScore(double score) {
    if (score <= 0.0) {
        return 0.0;
    }
    if (score >= 1.0) {
        return 1.0;
    }
    return score;
}

std::string sourcePathForDocument(const Document& doc) {
    auto it = doc.metadata.find("chunk_of");
    return it == doc.metadata.end() || it->second.empty()
        ? doc.relative_path
        : it->second;
}

std::string citationId(const std::string& prefix, size_t index) {
    return prefix + std::to_string(index + 1);
}

RagServiceSearchItem projectSearchItem(const SearchResult& result) {
    RagServiceSearchItem item;
    item.path = result.document.relative_path;
    item.source_path = sourcePathForDocument(result.document);
    auto chunk_it = result.document.metadata.find("chunk_index");
    item.citation_id = chunk_it == result.document.metadata.end()
        ? "S"
        : "S" + chunk_it->second;
    item.type = result.document.type;
    item.language = result.document.language;
    item.score = result.score;
    item.confidence = confidenceFromScore(result.fused_score > 0.0 ? result.fused_score : result.score);
    item.vector_score = result.vector_score;
    item.text_score = result.text_score;
    item.fused_score = result.fused_score;
    item.snippet = result.snippet;
    item.lines = result.document.lines_count;
    item.size = result.document.size_bytes;
    item.source_type = "project";
    return item;
}

RagServiceSearchItem qaSearchItem(const QASource::QAPair& pair, double score) {
    RagServiceSearchItem item;
    item.path = qaPath(pair);
    item.source_path = item.path;
    item.type = "QA";
    item.language = "knowledge_base";
    item.score = score;
    item.confidence = confidenceFromScore(score);
    item.text_score = score;
    item.fused_score = score;
    item.snippet = qaSnippet(pair);
    item.lines = 2;
    item.size = pair.question.size() + pair.answer.size();
    item.source_type = "qa";
    item.category = pair.category;
    item.pair_id = pair.id;
    return item;
}

std::string groundingStatus(double confidence, size_t context_count) {
    if (context_count == 0) {
        return "no_context";
    }
    if (confidence >= 0.75) {
        return "grounded";
    }
    if (confidence >= 0.35) {
        return "partial";
    }
    return "weak";
}

bool isFallbackAnswer(const std::string& answer) {
    return answer.rfind("LLM недоступен", 0) == 0 ||
           answer.rfind("LLM не настроен", 0) == 0 ||
           answer.rfind("Ошибка:", 0) == 0;
}

} // namespace

RagService::RagService(std::shared_ptr<RagEngine> engine,
                       std::shared_ptr<LLMClient> llm
#if QORNIX_HAS_SQLITE
                       , std::shared_ptr<SQLiteSource> sqlite
#endif
)
    : rag_engine_(std::move(engine)),
      llm_client_(std::move(llm))
#if QORNIX_HAS_SQLITE
      , sqlite_source_(std::move(sqlite))
#endif
{}

RagServiceIndexResponse RagService::indexProject(const std::optional<std::string>& project_path) {
    RagServiceIndexResponse response;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = project_path.value_or(rag_engine_->get_indexed_project_root());
    if (path.empty()) {
        path = ".";
    }
    rag_engine_->index_project(path);
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        const auto persisted = sqlite_source_->persistIndexedDocuments(
            rag_engine_->get_documents_snapshot(),
            "project:" + path,
            rag_engine_->get_embedding_model_id(),
            rag_engine_->get_embedding_backend()
        );
        if (persisted.documents > 0) {
            std::cout << "💾 Persisted RAG index snapshot: "
                      << persisted.documents << " documents, "
                      << persisted.chunks << " chunks, "
                      << persisted.embeddings << " embeddings, "
                      << persisted.deleted_documents << " stale documents deleted" << std::endl;
        }
    }
#endif
    response.success = true;
    response.message = "Project indexed successfully";
    response.stats = rag_engine_->get_statistics();
    return response;
}

RagServiceIngestResponse RagService::ingestProject(const std::optional<std::string>& project_path) {
    if (!rag_engine_) {
        RagServiceIngestResponse response;
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = project_path.value_or(rag_engine_->get_indexed_project_root());
    if (path.empty()) {
        path = ".";
    }

    RagServiceIngestionJob job;
    job.id = ingestionJobId();
    job.root_path = path;
    job.source_id = "project:" + path;
    job.status = "running";
    job.progress_percent = 0;

    return runIngestionJob(std::move(job), path);
}

RagServiceIngestResponse RagService::startBackgroundIngestProject(const std::optional<std::string>& project_path) {
    RagServiceIngestResponse response;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = project_path.value_or(rag_engine_->get_indexed_project_root());
    if (path.empty()) {
        path = ".";
    }

    RagServiceIngestionJob job;
    job.id = ingestionJobId();
    job.root_path = path;
    job.source_id = "project:" + path;
    job.status = "queued";
    job.progress_percent = 0;
    job.background = true;
    updateActiveIngestionJob(job);

#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        sqlite_source_->recordIngestionJobStarted(job.id, job.source_id, path);
    }
#endif

    response.success = true;
    response.message = "Ingestion job queued";
    response.job = job;

    std::thread([this, job, path]() mutable {
        job.status = "running";
        job.progress_percent = 5;
        job.background = true;
        updateActiveIngestionJob(job);
        runIngestionJob(std::move(job), path);
    }).detach();

    return response;
}

RagServiceIngestResponse RagService::runIngestionJob(RagServiceIngestionJob job, const std::string& path) {
    RagServiceIngestResponse response;
    response.job = job;
    response.job.status = "running";
    response.job.progress_percent = std::max(response.job.progress_percent, 5);
    updateActiveIngestionJob(response.job);

#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        sqlite_source_->recordIngestionJobStarted(response.job.id, response.job.source_id, path);
    }
#endif

    try {
        response.job.progress_percent = 15;
        updateActiveIngestionJob(response.job);
        auto indexed = indexProject(path);
        const auto ingestion = rag_engine_->get_last_ingestion_result();
        response.success = indexed.success;
        response.message = indexed.message;
        response.stats = indexed.stats;
        response.job.status = indexed.success ? "completed" : "failed";
        response.job.progress_percent = 100;
        response.job.files_seen = ingestion.files_seen;
        response.job.documents_imported = ingestion.documents_imported;
        response.job.duplicates_found = ingestion.duplicates_found;
        response.job.skipped = ingestion.skipped;
        response.job.errors = ingestion.errors;
        updateActiveIngestionJob(response.job);

#if QORNIX_HAS_SQLITE
        if (sqlite_source_) {
            sqlite_source_->recordIngestionJobFinished(
                response.job.id,
                response.job.status,
                ingestion,
                response.success ? "" : response.message
            );
            if (auto persisted = sqlite_source_->findIngestionJob(response.job.id)) {
                response.job = serviceJob(*persisted);
                response.job.progress_percent = 100;
                response.job.background = job.background;
            }
        }
#endif
    } catch (const std::exception& e) {
        response.success = false;
        response.message = e.what();
        response.job.status = "failed";
        response.job.progress_percent = 100;
        response.job.error_message = response.message;
        updateActiveIngestionJob(response.job);
#if QORNIX_HAS_SQLITE
        if (sqlite_source_) {
            qornix::rag::IngestionJobResult empty_result;
            sqlite_source_->recordIngestionJobFinished(
                response.job.id,
                response.job.status,
                empty_result,
                response.message
            );
            if (auto persisted = sqlite_source_->findIngestionJob(response.job.id)) {
                response.job = serviceJob(*persisted);
                response.job.progress_percent = 100;
                response.job.background = job.background;
            }
        }
#endif
    }

    removeActiveIngestionJob(response.job.id);
    return response;
}

std::optional<RagServiceIngestionJob> RagService::findIngestionJob(const std::string& job_id) const {
    {
        std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
        auto it = active_ingestion_jobs_.find(job_id);
        if (it != active_ingestion_jobs_.end()) {
            return it->second;
        }
    }
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        if (auto job = sqlite_source_->findIngestionJob(job_id)) {
            return serviceJob(*job);
        }
    }
#else
    (void)job_id;
#endif
    return std::nullopt;
}

std::vector<RagServiceIngestionJob> RagService::listIngestionJobs(size_t limit) const {
    std::vector<RagServiceIngestionJob> jobs;
    {
        std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
        for (const auto& [_, job] : active_ingestion_jobs_) {
            jobs.push_back(job);
            if (jobs.size() >= limit) {
                return jobs;
            }
        }
    }
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        for (const auto& job : sqlite_source_->listIngestionJobs(limit)) {
            if (jobs.size() >= limit) break;
            jobs.push_back(serviceJob(job));
        }
    }
#else
    (void)limit;
#endif
    return jobs;
}

void RagService::updateActiveIngestionJob(const RagServiceIngestionJob& job) {
    std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
    active_ingestion_jobs_[job.id] = job;
}

void RagService::removeActiveIngestionJob(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
    active_ingestion_jobs_.erase(job_id);
}

bool RagService::deletePersistedDocument(const std::string& relative_path,
                                         const std::string& source_id) {
#if QORNIX_HAS_SQLITE
    return sqlite_source_ && sqlite_source_->deletePersistedDocument(relative_path, source_id);
#else
    (void)relative_path;
    (void)source_id;
    return false;
#endif
}

RagServiceSearchResponse RagService::search(const std::string& query, size_t top_k) {
    RagServiceSearchResponse response;
    response.query = query;
    const auto started = std::chrono::steady_clock::now();

    if (!rag_engine_ || query.empty()) {
        response.success = false;
        return response;
    }

    response.expanded_query = rag_engine_->expand_query(query);
    response.query_expansion_applied = response.expanded_query != query;
    response.reranking_applied = rag_engine_->is_reranking_enabled();

    auto results = rag_engine_->search(query, top_k);
    response.results.reserve(results.size());
    for (const auto& result : results) {
        response.results.push_back(projectSearchItem(result));
    }

    const auto qa_pairs = searchQa(query, std::min<size_t>(top_k, 5));
    for (size_t i = 0; i < qa_pairs.size(); ++i) {
        auto item = qaSearchItem(qa_pairs[i], 1.0 - (static_cast<double>(i) * 0.01));
        item.citation_id = citationId("Q", i);
        response.results.push_back(std::move(item));
    }

    const auto finished = std::chrono::steady_clock::now();
    response.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
    return response;
}

RagServiceAskResponse RagService::ask(const std::string& question, size_t top_k, const std::string& client_ip) {
    RagServiceAskResponse response;
    response.question = question;
    if (!rag_engine_ || question.empty()) {
        response.success = false;
        response.llm_status = "invalid_request";
        return response;
    }
    response.expanded_query = rag_engine_->expand_query(question);
    response.query_expansion_applied = response.expanded_query != question;
    response.reranking_applied = rag_engine_->is_reranking_enabled();

    std::string context_text;
    const auto qa_pairs = searchQa(question, std::min<size_t>(top_k, 5));
    for (size_t i = 0; i < qa_pairs.size(); ++i) {
        const auto& pair = qa_pairs[i];
        const std::string citation = citationId("Q", i);
        if (!context_text.empty()) {
            context_text += "\n---\n";
        }
        context_text += "[" + citation + "] [QA Knowledge Base] " + qaPath(pair) + "\n" + qaSnippet(pair);

        RagServiceAskContextItem item;
        item.path = qaPath(pair);
        item.source_path = item.path;
        item.citation_id = citation;
        item.score = 1.0 - (static_cast<double>(i) * 0.01);
        item.confidence = confidenceFromScore(item.score);
        item.snippet = qaSnippet(pair);
        item.source_type = "qa";
        item.category = pair.category;
        item.pair_id = pair.id;
        response.context.push_back(std::move(item));
        response.sources.push_back(qaPath(pair));
        response.citations.push_back(citation);
    }

    auto results = rag_engine_->search(question, top_k);
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        const std::string citation = citationId("S", i);
        const std::string source_path = sourcePathForDocument(result.document);
        std::ostringstream ctx;
        ctx << "[" << citation << "] " << source_path << ": " << result.snippet;
        if (!context_text.empty()) {
            context_text += "\n---\n";
        }
        context_text += ctx.str();

        RagServiceAskContextItem item;
        item.path = result.document.relative_path;
        item.source_path = source_path;
        item.citation_id = citation;
        item.score = result.fused_score;
        item.confidence = confidenceFromScore(result.fused_score > 0.0 ? result.fused_score : result.score);
        item.snippet = result.snippet;
        item.source_type = "project";
        response.context.push_back(std::move(item));
        response.sources.push_back(source_path);
        response.citations.push_back(citation);
    }

    for (const auto& item : response.context) {
        response.retrieval_confidence = std::max(response.retrieval_confidence, item.confidence);
    }
    response.grounding_status = groundingStatus(response.retrieval_confidence, response.context.size());

    if (!context_text.empty()) {
        context_text =
            "Use the bracketed source ids such as [S1] or [Q1] when citing facts from context.\n"
            "If the context is insufficient, say so explicitly.\n\n" + context_text;
    }

    if (llm_client_ && llm_client_->is_enabled()) {
        const auto started = std::chrono::steady_clock::now();
        response.answer = llm_client_->ask(question, context_text, client_ip);
        const auto finished = std::chrono::steady_clock::now();
        response.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
        response.llm_status = isFallbackAnswer(response.answer)
            ? "fallback"
            : (llm_client_->is_available() ? "ok" : "unavailable");
    } else {
        response.answer = "LLM недоступен. Вот релевантные фрагменты:\n" + context_text;
        response.llm_status = "unavailable";
        response.response_time_ms = 0;
    }

    return response;
}

RagServiceHealth RagService::health() const {
    RagServiceHealth health;
    if (!rag_engine_) {
        health.status = "not_configured";
        return health;
    }

    auto stats = rag_engine_->get_statistics();
    health.indexed = rag_engine_->is_indexed();
    health.files = stats.total_files;
    health.lines = stats.total_lines;
    health.embedding_backend = rag_engine_->get_embedding_backend();
    const auto embedding_info = rag_engine_->get_embedding_model_info();
    health.embedding_model_id = embedding_info.id;
    health.embedding_model_name = embedding_info.name;
    health.embedding_dim = embedding_info.dimension;
    health.embedding_active_model_id = embedding_info.active_model_id;
    health.embedding_registry_size = embedding_info.registry_size;
    health.embedding_registry_model_ids = embedding_info.registry_model_ids;
    health.embedding_registry_warnings = embedding_info.registry_warnings;
    health.vector_store_backend = rag_engine_->get_vector_store_backend();
    health.vector_store_status = rag_engine_->get_vector_store_status();
    health.query_expansion = rag_engine_->is_query_expansion_enabled();
    health.reranking = rag_engine_->is_reranking_enabled();

    if (llm_client_ && llm_client_->is_enabled()) {
        health.llm_response_time_ms = llm_client_->health_check();
        health.available_models = llm_client_->list_available_models();
        health.configured_model_available = llm_client_->configured_model_available();
        health.llm_available = health.llm_response_time_ms >= 0;
        health.llm_provider = llm_client_->provider_name();
        health.llm_model = llm_client_->get_model();
        health.llm_api_url = llm_client_->get_api_url();
        health.llm_status = health.llm_response_time_ms >= 0
            ? "ok"
            : (health.available_models.empty() ? "provider_unavailable" : "model_not_found");
    }

    return health;
}

std::vector<RagServiceSourceInfo> RagService::sources() const {
    std::vector<RagServiceSourceInfo> output;
    if (!rag_engine_) {
        return output;
    }
    for (const auto& source : rag_engine_->getDataSources()) {
        output.push_back(RagServiceSourceInfo{
            source->getId(),
            data_source_type_to_string(source->getType()),
            source->getName(),
            source->count()
        });
    }
    return output;
}

std::vector<QASource::QAPair> RagService::listQaPairs(size_t page, size_t per_page, const std::string& source_id) const {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        return sqlite_source_->getAllPairs(page, per_page);
    }
#endif
    if (!rag_engine_) {
        return {};
    }
    for (const auto& source : rag_engine_->getDataSources()) {
        if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
            if (source_id.empty() || qa->getId() == source_id) {
                auto pairs = qa->getAllPairs();
                const size_t start = (page > 0 ? page - 1 : 0) * per_page;
                if (start >= pairs.size()) {
                    return {};
                }
                const size_t end = std::min(start + per_page, pairs.size());
                return std::vector<QASource::QAPair>(pairs.begin() + static_cast<std::ptrdiff_t>(start),
                                                     pairs.begin() + static_cast<std::ptrdiff_t>(end));
            }
        }
    }
    return {};
}

RagServiceQaListResponse RagService::listQaPairs(const RagServiceQaListOptions& options) const {
    RagServiceQaListResponse response;
    response.limit = options.limit == 0 ? 25 : std::min<size_t>(options.limit, 500);
    response.offset = options.offset;
    response.source_id = options.source_id;

#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (options.source_id.empty() ||
                           options.source_id == sqlite_source_->getId() ||
                           options.source_id == sqlite_source_->getSourceId())) {
        SQLiteSource::QAListOptions sqlite_options;
        sqlite_options.query = options.query;
        sqlite_options.category = options.category;
        sqlite_options.limit = response.limit;
        sqlite_options.offset = response.offset;
        auto listed = sqlite_source_->listQAPairs(sqlite_options);
        response.source_id = sqlite_source_->getId();
        response.items = std::move(listed.items);
        response.total = listed.total;
        response.limit = listed.limit;
        response.offset = listed.offset;
        response.has_more = response.offset + response.items.size() < response.total;
        return response;
    }
#endif

    if (!rag_engine_) {
        return response;
    }

    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    const std::string query = lower(options.query);
    const std::string category = options.category;

    for (const auto& source : rag_engine_->getDataSources()) {
        if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
            if (!options.source_id.empty() && qa->getId() != options.source_id) {
                continue;
            }

            response.source_id = qa->getId();
            auto pairs = qa->getAllPairs();
            std::vector<QASource::QAPair> filtered;
            for (const auto& pair : pairs) {
                const std::string pair_category = pair.category.empty() ? "general" : pair.category;
                if (!category.empty() && pair_category != category) {
                    continue;
                }
                if (!query.empty()) {
                    const std::string haystack = lower(pair.id + " " + pair.question + " " + pair.answer + " " + pair_category);
                    if (haystack.find(query) == std::string::npos) {
                        continue;
                    }
                }
                filtered.push_back(pair);
            }

            response.total = filtered.size();
            if (response.offset < filtered.size()) {
                const size_t end = std::min(response.offset + response.limit, filtered.size());
                response.items.assign(filtered.begin() + static_cast<std::ptrdiff_t>(response.offset),
                                      filtered.begin() + static_cast<std::ptrdiff_t>(end));
            }
            response.has_more = response.offset + response.items.size() < response.total;
            return response;
        }
    }

    return response;
}

std::vector<RagServiceQaSuggestion> RagService::suggestQaPairs(const std::string& query,
                                                               size_t limit,
                                                               const std::string& source_id) const {
    std::vector<RagServiceQaSuggestion> suggestions;
    if (query.empty()) {
        return suggestions;
    }
    limit = limit == 0 ? 10 : std::min<size_t>(limit, 50);

#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        for (const auto& item : sqlite_source_->suggestQAPairs(query, limit)) {
            suggestions.push_back(RagServiceQaSuggestion{item.id, item.question, item.category});
        }
        return suggestions;
    }
#endif

    RagServiceQaListOptions options;
    options.query = query;
    options.source_id = source_id;
    options.limit = limit;
    auto listed = listQaPairs(options);
    for (const auto& pair : listed.items) {
        suggestions.push_back(RagServiceQaSuggestion{pair.id, pair.question, pair.category});
    }
    return suggestions;
}

std::vector<std::string> RagService::listQaCategories(const std::string& query,
                                                      size_t limit,
                                                      const std::string& source_id) const {
    limit = limit == 0 ? 50 : std::min<size_t>(limit, 200);
#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        return sqlite_source_->listQACategories(query, limit);
    }
#endif

    std::vector<std::string> categories;
    if (!rag_engine_) {
        return categories;
    }
    auto contains = [](const std::vector<std::string>& values, const std::string& value) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    const std::string lowered_query = lower(query);
    for (const auto& source : rag_engine_->getDataSources()) {
        if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
            if (!source_id.empty() && qa->getId() != source_id) {
                continue;
            }
            for (const auto& pair : qa->getAllPairs()) {
                const std::string category = pair.category.empty() ? "general" : pair.category;
                if (!lowered_query.empty() && lower(category).find(lowered_query) == std::string::npos) {
                    continue;
                }
                if (!contains(categories, category)) {
                    categories.push_back(category);
                    if (categories.size() >= limit) {
                        return categories;
                    }
                }
            }
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

bool RagService::addQaPair(const std::string& question,
                           const std::string& answer,
                           const std::string& category,
                           std::string* pair_id) {
    if (question.empty() || answer.empty()) {
        return false;
    }
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        const std::string id = qaPairId(sqlite_source_->getId());
        if (!sqlite_source_->addQAPair(id, question, answer, category)) {
            return false;
        }
        if (pair_id) {
            *pair_id = id;
        }
        return true;
    }
#endif
    return false;
}

bool RagService::updateQaPair(const std::string& pair_id,
                              const std::string& question,
                              const std::string& answer,
                              const std::string& category) {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        return sqlite_source_->updateQAPair(pair_id, answer, category, "", question);
    }
#else
    (void)pair_id;
    (void)question;
    (void)answer;
    (void)category;
#endif
    return false;
}

bool RagService::deleteQaPair(const std::string& pair_id) {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        return sqlite_source_->deleteQAPair(pair_id);
    }
#else
    (void)pair_id;
#endif
    return false;
}

std::vector<QASource::QAPair> RagService::searchQa(const std::string& query, size_t limit) const {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        auto pairs = sqlite_source_->searchByQuestion(query);
        if (pairs.size() > limit) {
            pairs.resize(limit);
        }
        return pairs;
    }
#else
    (void)query;
    (void)limit;
#endif
    return {};
}
