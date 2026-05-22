/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * HTTP request handlers for the RAG system
 * Integration with qornix_web
 */

#include "core.h"
#include "llm_client.h"
#include "rag_service.h"
#include "batch_processor.h"
#include "prompt_cache.h"
#include "prometheus_metrics.h"
#include "qa_source.h"
#include "data_source.h"
#include "analytics_service.h"
#include "markdown_source.h"
#include "deduplication_service.h"
#if QORNIX_HAS_SQLITE
#include "sqlite_source.h"
#endif
#include "../include/handler_base.h"
#include "../include/template_loader.h"
#include "../include/http_server.h"
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <boost/json.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace http = boost::beast::http;
namespace urls = boost::urls;

/**
 * Base handler for the RAG API
 */
class RagApiHandler : public HandlerBase {
protected:
    std::shared_ptr<RagService> rag_service_;
    std::shared_ptr<RagEngine> rag_engine_;
    std::shared_ptr<LLMClient> llm_client_;
    std::shared_ptr<ICache> cache_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    std::shared_ptr<BatchProcessor> batch_processor_;
    std::shared_ptr<IPromptCache> prompt_cache_;
    std::shared_ptr<LLMRAGMetrics> metrics_;
   std::shared_ptr<AnalyticsService> analytics_service_;
    std::shared_ptr<MarkdownSource> markdown_source_;
    std::shared_ptr<DeduplicationService> dedup_service_;
#if QORNIX_HAS_SQLITE
    std::shared_ptr<SQLiteSource> sqlite_source_;
#endif

public:
   explicit RagApiHandler(std::shared_ptr<RagService> service);

   RagApiHandler(std::shared_ptr<RagEngine> engine,
                 std::shared_ptr<LLMClient> llm,
                 std::shared_ptr<ICache> cache,
                 std::shared_ptr<RateLimiter> limiter,
                 std::shared_ptr<BatchProcessor> batch,
                 std::shared_ptr<IPromptCache> pcache,
                 std::shared_ptr<LLMRAGMetrics> metrics,
                 std::shared_ptr<AnalyticsService> analytics,
                 std::shared_ptr<MarkdownSource> markdown,
                 std::shared_ptr<DeduplicationService> dedup = nullptr
#if QORNIX_HAS_SQLITE
                 , std::shared_ptr<SQLiteSource> sqlite = nullptr
#endif
                 );

    /**
     * Simple constructor for routes that only need rag_engine.
     */
    explicit RagApiHandler(std::shared_ptr<RagEngine> engine);

    /**
     * POST /api/index and /api/search - handle POST requests
     */
    void handlePost(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &
    ) override;

    /**
     * GET /api/stats, /api/health - project statistics and health check
     */
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &
    ) override;

    void handlePut(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override;

    void handleDelete(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override;
};

struct RagRouteOptions {
    bool expose_root_ui = true;
    std::string ui_path = "/";
    std::string api_prefix = "/api";
};

/**
 * Handler for the main page
 */
class RagWebHandler : public HandlerBase {
private:
    std::string templates_dir_;
    std::string api_base_;

public:
    explicit RagWebHandler(const std::string &templates_dir = "templates",
                           const std::string &api_base = "");

    void handleGet(
        const http::request<http::string_body> &,
        http::response<http::string_body> &res,
        const urls::url_view &,
        const std::map<std::string, std::string> &
    ) override;
};

/**
 * Configure routes for the RAG system
 */
void setupRagRoutes(HttpServer& server,
                    std::shared_ptr<RagEngine> rag_engine,
                    std::shared_ptr<LLMClient> llm_client = nullptr,
                    std::shared_ptr<ICache> cache = nullptr,
                    std::shared_ptr<RateLimiter> limiter = nullptr,
                    std::shared_ptr<BatchProcessor> batch = nullptr,
                    std::shared_ptr<IPromptCache> pcache = nullptr,
                    std::shared_ptr<LLMRAGMetrics> metrics = nullptr,
                    std::shared_ptr<AnalyticsService> analytics = nullptr,
                    std::shared_ptr<MarkdownSource> markdown = nullptr,
                    std::shared_ptr<DeduplicationService> dedup = nullptr
#if QORNIX_HAS_SQLITE
                    , std::shared_ptr<SQLiteSource> sqlite = nullptr
#endif
                    , RagRouteOptions options = RagRouteOptions()
                    );

void setupRagRoutes(HttpServer& server,
                    std::shared_ptr<RagService> rag_service,
                    std::shared_ptr<ICache> cache = nullptr,
                    std::shared_ptr<RateLimiter> limiter = nullptr,
                    std::shared_ptr<BatchProcessor> batch = nullptr,
                    std::shared_ptr<IPromptCache> pcache = nullptr,
                    std::shared_ptr<LLMRAGMetrics> metrics = nullptr,
                    std::shared_ptr<AnalyticsService> analytics = nullptr,
                    std::shared_ptr<MarkdownSource> markdown = nullptr,
                    std::shared_ptr<DeduplicationService> dedup = nullptr
#if QORNIX_HAS_SQLITE
                    , std::shared_ptr<SQLiteSource> sqlite = nullptr
#endif
                    , RagRouteOptions options = RagRouteOptions()
                    );
