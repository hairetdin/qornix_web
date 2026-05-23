/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_service.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

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

RagServiceSearchItem projectSearchItem(const SearchResult& result) {
    RagServiceSearchItem item;
    item.path = result.document.relative_path;
    item.type = result.document.type;
    item.language = result.document.language;
    item.score = result.score;
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
    item.type = "QA";
    item.language = "knowledge_base";
    item.score = score;
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
            "",
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
    RagServiceIngestResponse response;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = project_path.value_or(rag_engine_->get_indexed_project_root());
    if (path.empty()) {
        path = ".";
    }

    response.job.id = ingestionJobId();
    response.job.root_path = path;
    response.job.source_id = "project:" + path;
    response.job.status = "running";

#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        sqlite_source_->recordIngestionJobStarted(response.job.id, response.job.source_id, path);
    }
#endif

    try {
        auto indexed = indexProject(path);
        const auto ingestion = rag_engine_->get_last_ingestion_result();
        response.success = indexed.success;
        response.message = indexed.message;
        response.stats = indexed.stats;
        response.job.status = indexed.success ? "completed" : "failed";
        response.job.files_seen = ingestion.files_seen;
        response.job.documents_imported = ingestion.documents_imported;
        response.job.duplicates_found = ingestion.duplicates_found;
        response.job.skipped = ingestion.skipped;
        response.job.errors = ingestion.errors;

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
            }
        }
#endif
    } catch (const std::exception& e) {
        response.success = false;
        response.message = e.what();
        response.job.status = "failed";
        response.job.error_message = response.message;
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
            }
        }
#endif
    }

    return response;
}

std::optional<RagServiceIngestionJob> RagService::findIngestionJob(const std::string& job_id) const {
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
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        for (const auto& job : sqlite_source_->listIngestionJobs(limit)) {
            jobs.push_back(serviceJob(job));
        }
    }
#else
    (void)limit;
#endif
    return jobs;
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

    auto results = rag_engine_->search(query, top_k);
    response.results.reserve(results.size());
    for (const auto& result : results) {
        response.results.push_back(projectSearchItem(result));
    }

    const auto qa_pairs = searchQa(query, std::min<size_t>(top_k, 5));
    for (size_t i = 0; i < qa_pairs.size(); ++i) {
        response.results.push_back(qaSearchItem(qa_pairs[i], 1.0 - (static_cast<double>(i) * 0.01)));
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

    std::string context_text;
    const auto qa_pairs = searchQa(question, std::min<size_t>(top_k, 5));
    for (size_t i = 0; i < qa_pairs.size(); ++i) {
        const auto& pair = qa_pairs[i];
        if (!context_text.empty()) {
            context_text += "\n---\n";
        }
        context_text += "[QA Knowledge Base] " + qaPath(pair) + "\n" + qaSnippet(pair);

        RagServiceAskContextItem item;
        item.path = qaPath(pair);
        item.score = 1.0 - (static_cast<double>(i) * 0.01);
        item.snippet = qaSnippet(pair);
        item.source_type = "qa";
        item.category = pair.category;
        item.pair_id = pair.id;
        response.context.push_back(std::move(item));
        response.sources.push_back(qaPath(pair));
    }

    auto results = rag_engine_->search(question, top_k);
    for (const auto& result : results) {
        std::ostringstream ctx;
        ctx << result.document.relative_path << ": " << result.snippet;
        if (!context_text.empty()) {
            context_text += "\n---\n";
        }
        context_text += ctx.str();

        RagServiceAskContextItem item;
        item.path = result.document.relative_path;
        item.score = result.fused_score;
        item.snippet = result.snippet;
        item.source_type = "project";
        response.context.push_back(std::move(item));
        response.sources.push_back(result.document.relative_path);
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
