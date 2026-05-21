/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "web.h"
#include "memory_source.h"
#include <sstream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <iostream>

namespace http = boost::beast::http;
namespace urls = boost::urls;

using qornix::rag::QASource;
using qornix::rag::MemorySource;
// DataSource and DataSourceType are in global namespace (defined in core.h)

// ============================================================================
// RagApiHandler constructor
// ============================================================================

RagApiHandler::RagApiHandler(
    std::shared_ptr<RagEngine> engine,
    std::shared_ptr<LLMClient> llm,
    std::shared_ptr<ICache> cache,
    std::shared_ptr<RateLimiter> limiter,
    std::shared_ptr<BatchProcessor> batch,
    std::shared_ptr<IPromptCache> pcache,
    std::shared_ptr<LLMRAGMetrics> metrics,
    std::shared_ptr<AnalyticsService> analytics,
    std::shared_ptr<MarkdownSource> markdown)
    : rag_engine_(std::move(engine)),
      llm_client_(std::move(llm)),
      cache_(std::move(cache)),
      rate_limiter_(std::move(limiter)),
      batch_processor_(std::move(batch)),
      prompt_cache_(std::move(pcache)),
      metrics_(std::move(metrics)),
      analytics_service_(std::move(analytics)),
      markdown_source_(std::move(markdown)) {}

RagApiHandler::RagApiHandler(std::shared_ptr<RagEngine> engine)
    : rag_engine_(std::move(engine)) {}

// ============================================================================
// RagWebHandler constructor
// ============================================================================

RagWebHandler::RagWebHandler(const std::string &templates_dir)
    : templates_dir_(templates_dir) {}

// ============================================================================
// RagApiHandler::handlePost
// ============================================================================

void RagApiHandler::handlePost(
    const http::request<http::string_body> &req,
    http::response<http::string_body> &res,
    const urls::url_view &url_view,
    const std::map<std::string, std::string> &) {
    try {
        // Determine request type by URL
        std::string path = url_view.path();

        if (path == "/api/index" || path.find("/api/index") != std::string::npos) {
            // Project indexing
            boost::json::value json_req = boost::json::parse(req.body());
            std::string project_path = rag_engine_->get_indexed_project_root();
            if (project_path.empty()) {
                project_path = ".";
            }

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("project_path")) {
                    std::string requested_path = obj.at("project_path").as_string().c_str();
                    if (!requested_path.empty() && requested_path != "." && requested_path != "./") {
                        project_path = requested_path;
                    }
                }
            }

            // Index the project
            rag_engine_->index_project(project_path);

            // Get statistics
            auto stats = rag_engine_->get_statistics();

            // Build response
            boost::json::object response;
            response["success"] = true;
            response["message"] = "Project indexed successfully";
            response["stats"] = {
                {"total_files", stats.total_files},
                {"total_lines", stats.total_lines},
                {"total_size_kb", stats.total_size_bytes / 1024},
                {"index_duration_ms", static_cast<std::int64_t>(stats.index_duration_ms)}
            };

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));

        } else if (path == "/api/search" || path.find("/api/search") != std::string::npos) {
            // Project search
            boost::json::value json_req = boost::json::parse(req.body());
            std::string query;
            size_t top_k = 10;
            [[maybe_unused]] bool full_context = false;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("query")) {
                    query = obj.at("query").as_string().c_str();
                }
                if (obj.contains("top_k")) {
                    top_k = static_cast<size_t>(obj.at("top_k").as_int64());
                }
                if (obj.contains("full_context")) {
                    full_context = obj.at("full_context").as_bool();
                }
            }

            if (query.empty()) {
                buildErrorResponse(res, http::status::bad_request, "Query is required");
                return;
            }

            // Search
            auto search_start = std::chrono::steady_clock::now();
            auto results = rag_engine_->search(query, top_k);
            auto search_end = std::chrono::steady_clock::now();
            long long search_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                search_end - search_start).count();

            // Phase 5: Log search to analytics
            if (analytics_service_) {
                AnalyticsService::SearchQuery log_entry;
                log_entry.query = query;
                log_entry.timestamp = std::chrono::system_clock::now();
                log_entry.result_count = results.size();
                log_entry.has_answer = results.size() > 0;
                log_entry.response_time_ms = static_cast<int>(search_time);

                // Extract client IP
                auto it_ip = req.find("X-Forwarded-For");
                if (it_ip != req.end()) {
                    log_entry.client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                } else {
                    it_ip = req.find("X-Real-IP");
                    if (it_ip != req.end()) {
                        log_entry.client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                    }
                }

                if (!results.empty()) {
                    log_entry.top_result_path = results[0].document.relative_path;
                }

                analytics_service_->logSearch(log_entry);
            }

            // Build response
            boost::json::array results_array;
            for (const auto &result: results) {
                boost::json::object result_obj{
                    {"path", result.document.relative_path},
                    {"type", result.document.type},
                    {"language", result.document.language},
                    {"score", result.score},
                    {"vector_score", result.vector_score},
                    {"text_score", result.text_score},
                    {"fused_score", result.fused_score},
                    {"snippet", result.snippet},
                    {"lines", result.document.lines_count},
                    {"size", result.document.size_bytes}
                };
                results_array.emplace_back(result_obj);
            }

            boost::json::object response;
            response["success"] = true;
            response["query"] = query;
            response["results"] = results_array;
            response["count"] = results.size();

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));

        } else if (path == "/api/ask" || path.find("/api/ask") != std::string::npos) {
            // LLM-powered Q&A with RAG context
            boost::json::value json_req = boost::json::parse(req.body());
            std::string question;
            size_t top_k = 5;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("question")) {
                    question = obj.at("question").as_string().c_str();
                }
                if (obj.contains("top_k")) {
                    top_k = static_cast<size_t>(obj.at("top_k").as_int64());
                }
            }

            if (question.empty()) {
                buildErrorResponse(res, http::status::bad_request, "Question is required");
                return;
            }

            // Search for context
            auto results = rag_engine_->search(question, top_k);

            // Build context string
            std::string context;
            boost::json::array context_array;
            boost::json::array sources_array;

            for (const auto &result: results) {
                std::ostringstream ctx;
                ctx << result.document.relative_path << ": " << result.snippet;
                if (!context.empty()) {
                    context += "\n---\n";
                }
                context += ctx.str();

                boost::json::object ctx_obj{
                    {"path", result.document.relative_path},
                    {"score", result.fused_score},
                    {"snippet", result.snippet}
                };
                context_array.emplace_back(ctx_obj);
                sources_array.emplace_back(result.document.relative_path);
            }

            boost::json::object response;
            response["success"] = true;
            response["question"] = question;
            response["context"] = context_array;
            response["sources"] = sources_array;

            if (llm_client_ && llm_client_->is_enabled()) {
                // Check if streaming requested
                bool do_stream = false;
                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("stream")) {
                        do_stream = obj.at("stream").as_bool();
                    }
                }

                if (do_stream) {
                    // SSE Streaming response
                    res.set("Content-Type", "text/event-stream; charset=utf-8");
                    res.set("Cache-Control", "no-cache");
                    res.set("Connection", "keep-alive");
                    res.set("Access-Control-Allow-Origin", "*");

                    std::string response_body;

                    bool success = llm_client_->ask_stream_sse(
                        question, context,
                        [&response_body](const std::string& chunk, bool is_done) {
                            if (!chunk.empty()) {
                                response_body += "data: {"
                                    "\"chunk\": \"" + LLMClient::json_escape_chunk(chunk) + "\""
                                    ", \"done\": " + (is_done ? "true" : "false")
                                    + "}\n\n";
                            } else if (is_done) {
                                response_body += "data: {\"done\": true}\n\n";
                            }
                        }
                    );

                    res.body() = success ? response_body : "data: {\"error\": \"Streaming failed\"}\n\n";
                    res.result(http::status::ok);
                    return;
                }

                // Non-streaming response
                auto start_time = std::chrono::steady_clock::now();
                std::string answer;

                // Extract client IP from request headers (X-Forwarded-For, X-Real-IP, or remote address)
                std::string client_ip;
                auto it_ip = req.find("X-Forwarded-For");
                if (it_ip != req.end()) {
                    client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                } else {
                    it_ip = req.find("X-Real-IP");
                    if (it_ip != req.end()) {
                        client_ip = std::string(it_ip->value().data(), it_ip->value().size());
                    }
                }

                answer = llm_client_->ask(question, context, client_ip);
                auto end_time = std::chrono::steady_clock::now();

                long long response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time).count();

                response["answer"] = answer;
                response["llm_status"] = llm_client_->is_available() ? "ok" : "unavailable";
                response["response_time_ms"] = static_cast<std::int64_t>(response_time);

                // Phase 3: Add cache stats
                if (cache_) {
                    auto stats = llm_client_->get_cache_stats();
                    boost::json::object cache_obj;
                    cache_obj["enabled"] = true;
                    cache_obj["hits"] = static_cast<std::int64_t>(stats.hits);
                    cache_obj["misses"] = static_cast<std::int64_t>(stats.misses);
                    cache_obj["size"] = static_cast<std::int64_t>(stats.size);
                    cache_obj["max_size"] = static_cast<std::int64_t>(stats.max_size);
                    cache_obj["hit_rate_percent"] = static_cast<double>(stats.hit_rate());
                    response["cache"] = cache_obj;
                }

                // Phase 3: Add rate limiter stats
                if (rate_limiter_) {
                    auto rl_stats = llm_client_->get_rate_limiter_stats();
                    boost::json::object rl_obj;
                    rl_obj["enabled"] = true;
                    rl_obj["allowed"] = static_cast<std::int64_t>(rl_stats.allowed);
                    rl_obj["rejected"] = static_cast<std::int64_t>(rl_stats.rejected);
                    rl_obj["rejection_rate_percent"] = static_cast<double>(rl_stats.rejection_rate());
                    response["rate_limiter"] = rl_obj;
                }
            } else {
                // LLM not available - return context only
                response["answer"] = "LLM недоступен. Вот релевантные фрагменты:\n" + context;
                response["llm_status"] = "unavailable";
                response["response_time_ms"] = 0;
            }

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        } else if (path == "/api/batch" || path.find("/api/batch") != std::string::npos) {
            // Batch processing of multiple questions
            boost::json::value json_req = boost::json::parse(req.body());
            std::vector<BatchQuestion> questions;

            if (json_req.if_object()) {
                auto obj = json_req.as_object();
                if (obj.contains("questions")) {
                    const auto& q_array = obj.at("questions").as_array();
                    for (const auto& q_item : q_array) {
                        BatchQuestion q;
                        if (q_item.if_object()) {
                            auto q_obj = q_item.as_object();
                            if (q_obj.contains("question")) {
                                q.question = q_obj.at("question").as_string().c_str();
                            }
                            if (q_obj.contains("context")) {
                                q.context = q_obj.at("context").as_string().c_str();
                            }
                            if (q_obj.contains("top_k")) {
                                q.top_k = static_cast<size_t>(q_obj.at("top_k").as_int64());
                            }
                            if (q_obj.contains("client_ip")) {
                                q.client_ip = q_obj.at("client_ip").as_string().c_str();
                            }
                            if (!q.question.empty()) {
                                questions.push_back(std::move(q));
                            }
                        }
                    }
                }
            }

            if (questions.empty()) {
                buildErrorResponse(res, http::status::bad_request, "Questions array is required");
                return;
            }

            // Update metrics
            if (metrics_) {
                metrics_->inc_batch_questions(questions.size());
            }

            // Process batch
            auto start_time = std::chrono::steady_clock::now();

            std::vector<BatchResult> results;
            if (llm_client_ && llm_client_->is_enabled()) {
                results = batch_processor_->process(questions,
                    [this](const std::string& question, const std::string& context, const std::string& client_ip) -> std::string {
                        return llm_client_->ask(question, context, client_ip);
                    });
            } else {
                // LLM not available - return context only for each question
                results.resize(questions.size());
                for (size_t i = 0; i < questions.size(); i++) {
                    results[i].question = questions[i].question;
                    results[i].answer = "LLM недоступен. Вот релевантные фрагменты:\n" + questions[i].context;
                    results[i].success = false;
                    results[i].error = "LLM not available";
                }
            }

            auto end_time = std::chrono::steady_clock::now();
            long long batch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();

            // Update metrics
            if (metrics_) {
                size_t completed = 0;
                for (const auto& r : results) {
                    if (r.success) completed++;
                }
                metrics_->inc_batch_completed(completed);
                metrics_->observe_llm_request_duration_seconds(batch_duration / 1000.0);
            }

            // Build response
            boost::json::array results_array;
            for (const auto& r : results) {
                boost::json::object r_obj{
                    {"question", r.question},
                    {"success", r.success},
                    {"answer", r.answer},
                    {"response_time_ms", static_cast<std::int64_t>(r.response_time_ms)}
                };
                if (!r.error.empty()) {
                    r_obj["error"] = r.error;
                }
                results_array.emplace_back(r_obj);
            }

            auto stats = batch_processor_->get_stats();
            boost::json::object response;
            response["success"] = true;
            response["questions_count"] = static_cast<std::int64_t>(questions.size());
            response["results"] = results_array;
            response["batch_duration_ms"] = static_cast<std::int64_t>(batch_duration);
            response["stats"] = {
                {"total", static_cast<std::int64_t>(stats.total)},
                {"completed", static_cast<std::int64_t>(stats.completed)},
                {"failed", static_cast<std::int64_t>(stats.failed)},
                {"completion_rate_percent", static_cast<double>(stats.completion_rate())}
            };

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        } else if (path == "/api/metrics" || path.find("/api/metrics") != std::string::npos) {
            // Prometheus metrics endpoint
            if (metrics_) {
                std::string metrics_text = metrics_->render_all();
                res.set("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
                res.body() = metrics_text;
                res.result(http::status::ok);
            } else {
                buildErrorResponse(res, http::status::service_unavailable, "Metrics not enabled");
            }
        } else if (path == "/api/sources" || path.find("/api/sources") != std::string::npos) {
            // GET /api/sources - list data sources
            {
                auto sources = rag_engine_->getDataSources();
                boost::json::array sources_array;

                for (const auto& source : sources) {
                    boost::json::object source_obj{
                        {"id", source->getId()},
                        {"type", data_source_type_to_string(source->getType())},
                        {"name", source->getName()},
                        {"document_count", static_cast<std::int64_t>(source->count())}
                    };
                    sources_array.emplace_back(source_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["sources"] = sources_array;
                response["count"] = static_cast<std::int64_t>(sources.size());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/sources/add" || path.find("/api/sources/add") != std::string::npos) {
            // POST /api/sources/add - add a data source
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_type;
                std::string name;
                std::string source_id;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_type")) {
                        source_type = obj.at("source_type").as_string().c_str();
                    }
                    if (obj.contains("name")) {
                        name = obj.at("name").as_string().c_str();
                    }
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                }

                if (source_type.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "source_type is required");
                    return;
                }

                std::shared_ptr<DataSource> new_source;

                if (source_type == "qa_kb") {
                    // Create QASource
                    QASource::Config qa_config;
                    qa_config.name = name.empty() ? "API QA Source" : name;
                    qa_config.source_id = source_id.empty() ? ("qa_" + std::to_string(std::time(nullptr))) : source_id;

                    // Load initial pairs from JSON if provided
                    if (json_req.if_object()) {
                        auto obj = json_req.as_object();
                        if (obj.contains("pairs") && obj.at("pairs").is_array()) {
                            const auto& pairs_array = obj.at("pairs").as_array();
                            for (const auto& pair_item : pairs_array) {
                                if (pair_item.if_object()) {
                                    auto p = pair_item.as_object();
                                    if (p.contains("question") && p.contains("answer")) {
                                        QASource::QAPair qa_pair;
                                        qa_pair.question = p.at("question").as_string().c_str();
                                        qa_pair.answer = p.at("answer").as_string().c_str();
                                        if (p.contains("category")) {
                                            qa_pair.category = p.at("category").as_string().c_str();
                                        }
                                        if (p.contains("id")) {
                                            qa_pair.id = p.at("id").as_string().c_str();
                                        }
                                        qa_config.pairs.push_back(qa_pair);
                                    }
                                }
                            }
                        }
                    }

                    new_source = std::make_shared<QASource>(qa_config);
                } else if (source_type == "memory") {
                    // Create MemorySource
                    MemorySource::Config mem_config;
                    mem_config.name = name.empty() ? "API Memory Source" : name;
                    mem_config.allow_duplicates = false;

                    new_source = std::make_shared<MemorySource>(mem_config);
                } else {
                    buildErrorResponse(res, http::status::bad_request,
                                      "Unsupported source_type: " + source_type + ". Use 'qa_kb' or 'memory'");
                    return;
                }

                rag_engine_->addDataSource(new_source);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "Data source added";
                response["source_id"] = new_source->getId();
                response["source_type"] = data_source_type_to_string(new_source->getType());
                response["document_count"] = static_cast<std::int64_t>(new_source->count());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/sources/remove" || path.find("/api/sources/remove") != std::string::npos) {
            // POST /api/sources/remove - remove a data source by ID
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_id;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                }

                if (source_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "source_id is required");
                    return;
                }

                rag_engine_->removeDataSource(source_id);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "Data source removed";
                response["source_id"] = source_id;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/qa/add" || path.find("/api/qa/add") != std::string::npos) {
            // POST /api/qa/add - add a QA pair
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string source_id;
                std::string question;
                std::string answer;
                std::string category = "general";

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("source_id")) {
                        source_id = obj.at("source_id").as_string().c_str();
                    }
                    if (obj.contains("question")) {
                        question = obj.at("question").as_string().c_str();
                    }
                    if (obj.contains("answer")) {
                        answer = obj.at("answer").as_string().c_str();
                    }
                    if (obj.contains("category")) {
                        category = obj.at("category").as_string().c_str();
                    }
                }

                if (question.empty() || answer.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "question and answer are required");
                    return;
                }

                // Find QASource by ID
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        if (source_id.empty() || qa->getId() == source_id) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found,
                                      "QASource not found: " + (source_id.empty() ? "no QA source" : source_id));
                    return;
                }

                // Generate ID if not provided
                std::string pair_id = source_id + "_" + std::to_string(std::time(nullptr));

                QASource::QAPair new_pair;
                new_pair.id = pair_id;
                new_pair.question = question;
                new_pair.answer = answer;
                new_pair.category = category;

                qa_source->addQAPair(new_pair);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "QA pair added";
                response["pair_id"] = pair_id;
                response["source_id"] = qa_source->getId();

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/qa/update" || path.find("/api/qa/update") != std::string::npos) {
            // POST /api/qa/update - update a QA pair
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string pair_id;
                std::string question;
                std::string answer;
                std::string category;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("pair_id")) {
                        pair_id = obj.at("pair_id").as_string().c_str();
                    }
                    if (obj.contains("question")) {
                        question = obj.at("question").as_string().c_str();
                    }
                    if (obj.contains("answer")) {
                        answer = obj.at("answer").as_string().c_str();
                    }
                    if (obj.contains("category")) {
                        category = obj.at("category").as_string().c_str();
                    }
                }

                if (pair_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "pair_id is required");
                    return;
                }

                // Find QASource containing this pair
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        auto opt_pair = qa->findQAPair(pair_id);
                        if (opt_pair) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
                    return;
                }

                auto opt_pair = qa_source->findQAPair(pair_id);
                if (!opt_pair) {
                    buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
                    return;
                }

                QASource::QAPair updated_pair = opt_pair.value();
                if (!question.empty()) updated_pair.question = question;
                if (!answer.empty()) updated_pair.answer = answer;
                if (!category.empty()) updated_pair.category = category;

                qa_source->updateQAPair(updated_pair);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "QA pair updated";
                response["pair_id"] = pair_id;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/qa/delete" || path.find("/api/qa/delete") != std::string::npos) {
            // POST /api/qa/delete - delete a QA pair
            {
                boost::json::value json_req = boost::json::parse(req.body());
                std::string pair_id;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("pair_id")) {
                        pair_id = obj.at("pair_id").as_string().c_str();
                    }
                }

                if (pair_id.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "pair_id is required");
                    return;
                }

                // Find QASource containing this pair
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        auto opt_pair = qa->findQAPair(pair_id);
                        if (opt_pair) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found, "QA pair not found: " + pair_id);
                    return;
                }

                qa_source->removeQAPair(pair_id);

                boost::json::object response;
                response["success"] = true;
                response["message"] = "QA pair deleted";
                response["pair_id"] = pair_id;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/qa/list" || path.find("/api/qa/list") != std::string::npos) {
            // GET /api/qa/list - list QA pairs with pagination
            {
                std::string source_id;
                size_t page = 1;
                size_t per_page = 20;

                // Parse query parameters manually
                std::string query = url_view.query();
                if (!query.empty() && query[0] == '?') {
                    query = query.substr(1);
                }
                if (!query.empty()) {
                    // Simple query param parsing
                    std::stringstream ss(query);
                    std::string param;
                    while (std::getline(ss, param, '&')) {
                        auto eq_pos = param.find('=');
                        if (eq_pos != std::string::npos) {
                            std::string key = param.substr(0, eq_pos);
                            std::string value = param.substr(eq_pos + 1);
                            if (key == "source_id") {
                                source_id = value;
                            } else if (key == "page") {
                                page = static_cast<size_t>(std::stoi(value));
                            } else if (key == "per_page") {
                                per_page = static_cast<size_t>(std::stoi(value));
                            }
                        }
                    }
                }

                // Find QASource
                auto sources = rag_engine_->getDataSources();
                std::shared_ptr<QASource> qa_source = nullptr;

                for (const auto& source : sources) {
                    if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
                        if (source_id.empty() || qa->getId() == source_id) {
                            qa_source = qa;
                            break;
                        }
                    }
                }

                if (!qa_source) {
                    buildErrorResponse(res, http::status::not_found, "No QA source found");
                    return;
                }

                auto all_pairs = qa_source->getAllPairs();
                size_t total = all_pairs.size();
                size_t start = (page - 1) * per_page;
                size_t end = std::min(start + per_page, total);

                boost::json::array pairs_array;
                for (size_t i = start; i < end && i < total; i++) {
                    const auto& pair = all_pairs[i];
                    boost::json::object pair_obj;
                    pair_obj["id"] = pair.id;
                    pair_obj["question"] = pair.question;
                    pair_obj["category"] = pair.category;

                    boost::json::array aliases_array;
                    for (const auto& alias : pair.aliases) {
                        aliases_array.emplace_back(alias);
                    }
                    pair_obj["aliases"] = std::move(aliases_array);

                    pairs_array.emplace_back(pair_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["source_id"] = qa_source->getId();
                response["total"] = static_cast<std::int64_t>(total);
                response["page"] = static_cast<std::int64_t>(page);
                response["per_page"] = static_cast<std::int64_t>(per_page);
                response["pairs"] = pairs_array;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }

        // ============================================
        // Phase 5: New API Endpoints
        // ============================================

        } else if (path == "/api/analytics" || path.find("/api/analytics") != std::string::npos) {
            // GET /api/analytics - Get analytics report
            {
                if (!analytics_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                    return;
                }

                auto report = analytics_service_->getRecentReport();
                auto json_report = analytics_service_->exportToJson(report);

                boost::json::object response;
                response["success"] = true;
                response["report"] = boost::json::parse(json_report);

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/analytics/gaps" || path.find("/api/analytics/gaps") != std::string::npos) {
            // GET /api/analytics/gaps - Get knowledge gaps
            {
                if (!analytics_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                    return;
                }

                auto gaps = analytics_service_->getKnowledgeGaps();
                auto missing = analytics_service_->getMissingAnswers();

                boost::json::array gaps_array;
                for (const auto& gap : gaps) {
                    boost::json::object gap_obj;
                    gap_obj["query"] = gap.query;
                    gap_obj["search_count"] = static_cast<std::int64_t>(gap.search_count);
                    gaps_array.emplace_back(gap_obj);
                }

                boost::json::array missing_array;
                for (const auto& miss : missing) {
                    boost::json::object miss_obj;
                    miss_obj["query"] = miss.query;
                    miss_obj["search_count"] = static_cast<std::int64_t>(miss.search_count);
                    missing_array.emplace_back(miss_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["knowledge_gaps"] = gaps_array;
                response["missing_answers"] = missing_array;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/analytics/export" || path.find("/api/analytics/export") != std::string::npos) {
            // POST /api/analytics/export - Export report to JSON
            {
                if (!analytics_service_) {
                    buildErrorResponse(res, http::status::service_unavailable, "Analytics not enabled");
                    return;
                }

                auto report = analytics_service_->getRecentReport();
                auto json_report = analytics_service_->exportToJson(report);

                res.set("Content-Type", "application/json; charset=utf-8");
                res.body() = json_report;
                res.result(http::status::ok);
            }
        } else if (path == "/api/qa/dedup" || path.find("/api/qa/dedup") != std::string::npos) {
            // POST /api/qa/dedup - Find duplicate QA pairs (hash-based)
            {
                // For now, return info about dedup capability
                boost::json::object response;
                response["success"] = true;
                response["message"] = "Semantic deduplication not yet implemented. Hash-based dedup is active on insert.";
                response["hash_dedup_enabled"] = true;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/qa/dedup/remove" || path.find("/api/qa/dedup/remove") != std::string::npos) {
            // POST /api/qa/dedup/remove - Remove duplicates
            {
                boost::json::object response;
                response["success"] = true;
                response["message"] = "Semantic deduplication removal not yet implemented.";

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/import/markdown" || path.find("/api/import/markdown") != std::string::npos) {
            // POST /api/import/markdown - Import Markdown files
            {
                if (!markdown_source_) {
                    buildErrorResponse(res, http::status::service_unavailable, "MarkdownSource not configured");
                    return;
                }

                boost::json::value json_req = boost::json::parse(req.body());
                std::string directory;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("directory")) {
                        directory = obj.at("directory").as_string().c_str();
                    }
                }

                if (!directory.empty()) {
                    markdown_source_->setDirectoryPath(directory);
                }

                auto result = markdown_source_->importFiles();

                boost::json::object response;
                response["success"] = true;
                response["result"] = {
                    {"files_imported", static_cast<std::int64_t>(result.files_imported)},
                    {"documents_created", static_cast<std::int64_t>(result.documents_created)},
                    {"duplicates_skipped", static_cast<std::int64_t>(result.duplicates_skipped)}
                };

                boost::json::array errors_array;
                for (const auto& err : result.errors) {
                    errors_array.emplace_back(err);
                }
                response["errors"] = errors_array;

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else if (path == "/api/import/history" || path.find("/api/import/history") != std::string::npos) {
            // GET /api/import/history - Get import history
            {
                if (!markdown_source_) {
                    buildErrorResponse(res, http::status::service_unavailable, "MarkdownSource not configured");
                    return;
                }

                auto imported_files = markdown_source_->getImportedFiles();

                boost::json::array files_array;
                for (const auto& file : imported_files) {
                    files_array.emplace_back(file);
                }

                boost::json::object response;
                response["success"] = true;
                response["files"] = files_array;
                response["count"] = static_cast<std::int64_t>(imported_files.size());

                buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
            }
        } else {
            buildErrorResponse(res, http::status::not_found, "Unknown endpoint");
        }
    } catch (const std::exception &e) {
        buildErrorResponse(res, http::status::internal_server_error, e.what());
    }
}

// ============================================================================
// RagApiHandler::handleGet
// ============================================================================

void RagApiHandler::handleGet(
    const http::request<http::string_body> &req,
    http::response<http::string_body> &res,
    const urls::url_view &url_view,
    const std::map<std::string, std::string> &) {
    try {
        std::string path = url_view.path();

        if (path == "/api/health" || path.find("/api/health") != std::string::npos) {
            // Health check endpoint
            auto stats = rag_engine_->get_statistics();

            boost::json::object rag_obj;
            rag_obj["indexed"] = rag_engine_->is_indexed();
            rag_obj["files"] = stats.total_files;
            rag_obj["lines"] = stats.total_lines;
            rag_obj["embedding_backend"] = rag_engine_->get_embedding_backend();
            rag_obj["hybrid_search"] = true; // Default to true

            boost::json::object llm_obj;
            if (llm_client_ && llm_client_->is_enabled()) {
                int response_time = llm_client_->health_check();
                llm_obj["available"] = response_time >= 0;
                llm_obj["model"] = llm_client_->get_model();
                llm_obj["api_url"] = llm_client_->get_api_url();
                llm_obj["status"] = response_time >= 0 ? "ok" : "error";
                llm_obj["response_time_ms"] = response_time >= 0
                                              ? static_cast<std::int64_t>(response_time)
                                              : static_cast<std::int64_t>(-1);
            } else {
                llm_obj["available"] = false;
                llm_obj["model"] = "not_configured";
                llm_obj["api_url"] = "N/A";
                llm_obj["status"] = "not_configured";
            }

            boost::json::object response;
            response["status"] = "ok";
            response["rag"] = rag_obj;
            response["llm"] = llm_obj;

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));

        } else {
            // Original /api/stats handling
            auto stats = rag_engine_->get_statistics();

            boost::json::object response;
            response["success"] = true;
            response["indexed"] = rag_engine_->is_indexed();
            response["embedding_backend"] = rag_engine_->get_embedding_backend();
            response["embedding_dim"] = static_cast<std::int64_t>(rag_engine_->get_embedding_dim());
            response["onnx_ready"] = rag_engine_->is_onnx_ready();
            response["onnx_status"] = rag_engine_->get_onnx_status_message();
            response["project_root"] = rag_engine_->get_indexed_project_root();

            boost::json::object stats_obj;
            stats_obj["total_files"] = stats.total_files;
            stats_obj["total_lines"] = stats.total_lines;
            stats_obj["total_size_kb"] = stats.total_size_bytes / 1024;
            stats_obj["index_duration_ms"] = static_cast<std::int64_t>(stats.index_duration_ms);

            boost::json::object files_by_type;
            for (const auto &[type, count]: stats.files_by_type) {
                files_by_type[type] = count;
            }
            stats_obj["files_by_type"] = files_by_type;

            boost::json::object files_by_dir;
            for (const auto &[dir, count]: stats.files_by_directory) {
                files_by_dir[dir] = count;
            }
            stats_obj["files_by_directory"] = files_by_dir;

            response["stats"] = stats_obj;

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        }
    } catch (const std::exception &e) {
        buildErrorResponse(res, http::status::internal_server_error, e.what());
    }
}

// ============================================================================
// RagWebHandler::handleGet
// ============================================================================

void RagWebHandler::handleGet(
    const http::request<http::string_body> &,
    http::response<http::string_body> &res,
    const urls::url_view &,
    const std::map<std::string, std::string> &) {
    try {
        std::string html = TemplateLoader::loadFile("rag_interface.html", templates_dir_);
        buildHtmlResponse(res, http::status::ok, html);
    } catch (const std::exception &e) {
        std::string error_html = TemplateLoader::load500Template(templates_dir_);
        buildHtmlResponse(res, http::status::internal_server_error, error_html);
    }
}

// ============================================================================
// setupRagRoutes
// ============================================================================

void setupRagRoutes(HttpServer& server,
                    std::shared_ptr<RagEngine> rag_engine,
                    std::shared_ptr<LLMClient> llm_client,
                    std::shared_ptr<ICache> cache,
                    std::shared_ptr<RateLimiter> limiter,
                    std::shared_ptr<BatchProcessor> batch,
                    std::shared_ptr<IPromptCache> pcache,
                    std::shared_ptr<LLMRAGMetrics> metrics,
                    std::shared_ptr<AnalyticsService> analytics,
                    std::shared_ptr<MarkdownSource> markdown) {
    std::cout << "\U0001f527\U0001f4dd\u0414\u043e\u0431\u0430\u0432\u043b\u0435\u043d\u0438\u0435 RAG \u043c\u0430\u0440\u0448\u0440\u0443\u0442\u043e\u0432..." << std::endl;

    // Main page
    server.add_route("/", std::make_shared<RagWebHandler>("templates"));
    std::cout << "  \u2713 GET  / - Web \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441" << std::endl;

    // API endpoints
    server.add_route("/api/index", std::make_shared<RagApiHandler>(rag_engine, llm_client, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
    std::cout << "  \u2713 POST /api/index - \u0418\u043d\u0434\u0435\u043a\u0441\u0430\u0446\u0438\u044f \u043f\u0440\u043e\u0435\u043a\u0442\u0430" << std::endl;

    server.add_route("/api/search", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics, nullptr));
    std::cout << "  \u2713 POST /api/search - \u041f\u043e\u0438\u0441\u043a" << std::endl;

    server.add_route("/api/stats", std::make_shared<RagApiHandler>(rag_engine, llm_client, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
    std::cout << "  \u2713 GET  /api/stats - \u0421\u0442\u0430\u0442\u0438\u0441\u0442\u0438\u043a\u0430" << std::endl;

    server.add_route("/api/ask", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, nullptr, nullptr));
    std::cout << "  \u2713 POST /api/ask - \u0412\u043e\u043f\u0440\u043e\u0441 \u0441 LLM" << std::endl;

    server.add_route("/api/batch", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, nullptr, nullptr));
    std::cout << "  \u2713 POST /api/batch - Batch \u0432\u043e\u043f\u0440\u043e\u0441\u043e\u0432" << std::endl;

    server.add_route("/api/health", std::make_shared<RagApiHandler>(rag_engine, llm_client, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
    std::cout << "  \u2713 GET  /api/health - \u041f\u0440\u043e\u0432\u0435\u0440\u043a\u0430 \u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u044f" << std::endl;

    server.add_route("/api/metrics", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, nullptr, nullptr));
    std::cout << "  \u2713 GET  /api/metrics - Prometheus metrics" << std::endl;

    // Phase 4: Data sources management endpoints
    server.add_route("/api/sources", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 GET  /api/sources - \u0421\u043f\u0438\u0441\u043e\u043a \u0438\u0441\u0442\u043e\u0447\u043d\u0438\u043a\u043e\u0432 \u0434\u0430\u043d\u043d\u044b\u0445" << std::endl;

    server.add_route("/api/sources/add", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 POST /api/sources/add - \u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u0438\u0441\u0442\u043e\u0447\u043d\u0438\u043a \u0434\u0430\u043d\u043d\u044b\u0445" << std::endl;

    server.add_route("/api/sources/remove", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 POST /api/sources/remove - \u0423\u0434\u0430\u043b\u0438\u0442\u044c \u0438\u0441\u0442\u043e\u0447\u043d\u0438\u043a \u0434\u0430\u043d\u043d\u044b\u0445" << std::endl;

    // Phase 4: QA knowledge base endpoints
    server.add_route("/api/qa/add", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 POST /api/qa/add - \u0414\u043e\u0431\u0430\u0432\u0438\u0442\u044c QA-\u043f\u0430\u0440\u0443" << std::endl;

    server.add_route("/api/qa/update", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 POST /api/qa/update - \u041e\u0431\u043d\u043e\u0432\u0438\u0442\u044c QA-\u043f\u0430\u0440\u0443" << std::endl;

    server.add_route("/api/qa/delete", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 POST /api/qa/delete - \u0423\u0434\u0430\u043b\u0438\u0442\u044c QA-\u043f\u0430\u0440\u0443" << std::endl;

    server.add_route("/api/qa/list", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  \u2713 GET  /api/qa/list - \u0421\u043f\u0438\u0441\u043e\u043a QA-\u043f\u0430\u0440" << std::endl;

    if (llm_client && llm_client->is_enabled()) {
        std::cout << "  \u2139\ufe0f  LLM: " << llm_client->get_model()
                  << " @ " << llm_client->get_api_url() << std::endl;
    } else {
        std::cout << "  \u2139\ufe0f  LLM: \u043d\u0435 \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043d (\u0442\u043e\u043b\u044c\u043a\u043e \u043f\u043e\u0438\u0441\u043a)" << std::endl;
    }

    if (cache) {
        std::cout << "  \u2705 Cache: enabled" << std::endl;
    }
    if (limiter && limiter->is_available()) {
        std::cout << "  \U0001f6e1\ufe0f  Rate limiter: enabled" << std::endl;
    }
    if (batch) {
        std::cout << "  \U0001f4e6 Batch processor: enabled" << std::endl;
    }
    if (pcache) {
        std::cout << "  \U0001f4dd Prompt cache: enabled" << std::endl;
    }
    if (metrics) {
        std::cout << "  \U0001f4ca Prometheus metrics: enabled" << std::endl;
    }

    // Phase 5: Analytics endpoints
    if (analytics) {
        server.add_route("/api/analytics", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics, nullptr));
        std::cout << "  \U0001f4c8 GET  /api/analytics - Analytics report" << std::endl;

        server.add_route("/api/analytics/gaps", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics, nullptr));
        std::cout << "  \U0001f4c8 GET  /api/analytics/gaps - Knowledge gaps" << std::endl;

        server.add_route("/api/analytics/export", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics, nullptr));
        std::cout << "  \U0001f4c8 POST /api/analytics/export - Export report" << std::endl;
    }

    // Phase 5: QA dedup endpoints (stub)
    {
        server.add_route("/api/qa/dedup", std::make_shared<RagApiHandler>(rag_engine));
        std::cout << "  \U0001f504 POST /api/qa/dedup - Find duplicates (stub)" << std::endl;

        server.add_route("/api/qa/dedup/remove", std::make_shared<RagApiHandler>(rag_engine));
        std::cout << "  \U0001f504 POST /api/qa/dedup/remove - Remove duplicates (stub)" << std::endl;
    }

    // Phase 5: Markdown import endpoints
    if (markdown) {
        server.add_route("/api/import/markdown", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics, markdown));
        std::cout << "  \U0001f4dd POST /api/import/markdown - Import Markdown" << std::endl;

        server.add_route("/api/import/history", std::make_shared<RagApiHandler>(rag_engine, llm_client, cache, limiter, batch, pcache, metrics, analytics, markdown));
        std::cout << "  \U0001f4dd GET  /api/import/history - Import history" << std::endl;
    }

    std::cout << "\u2705 \u0412\u0441\u0435 \u043c\u0430\u0440\u0448\u0440\u0443\u0442\u044b \u0434\u043e\u0431\u0430\u0432\u043b\u0435\u043d\u044b" << std::endl;
}
