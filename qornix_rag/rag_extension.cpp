/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_extension.h"
#include "web.h"
#include "analytics_service.h"
#include <iostream>
#include <algorithm>

RagExtension::RagExtension()
    : runtime_config_(makeRagConfigFromIntegratedFlatMap({}, "default integrated RAG config")) {
    rag_config_ = runtime_config_.engine;
    llm_config_ = runtime_config_.llm;
}

void RagExtension::initialize(DIContainer& container) {
    (void)container;
    if (initialized_) return;

    std::cout << "🚀 Initializing RAG module..." << std::endl;
    std::cout << "  Mode: " << ragRuntimeModeToString(runtime_config_.mode) << std::endl;
    std::cout << "  Config source: " << runtime_config_.config_source << std::endl;
    std::cout << "  Route UI: " << runtime_config_.routes.ui_path << std::endl;
    std::cout << "  Route API prefix: " << runtime_config_.routes.api_prefix << std::endl;
    std::cout << "  Scan path: "
              << (runtime_config_.scan_path ? *runtime_config_.scan_path : std::string("<not configured>"))
              << std::endl;
    std::cout << "  LLM: "
              << (runtime_config_.llm.enabled ? runtime_config_.llm.model : std::string("disabled"))
              << " @ " << runtime_config_.llm.api_url << std::endl;

    // Initialize RagEngine
    rag_engine_ = std::make_shared<RagEngine>(runtime_config_.engine);

    // Initialize LLMClient from the parsed RagConfig contract.
    llm_client_ = std::make_shared<LLMClient>(runtime_config_.llm);

    // Initialize cache
    cache_ = create_cache(runtime_config_.cache);

    // Initialize rate limiter
    rate_limiter_ = std::make_shared<RateLimiter>(runtime_config_.rate_limit);
    if (cache_) {
        llm_client_->set_cache(cache_);
    }
    if (rate_limiter_ && rate_limiter_->is_available()) {
        llm_client_->set_rate_limiter(rate_limiter_);
    }

    // Initialize batch processor
    batch_processor_ = std::make_shared<BatchProcessor>(runtime_config_.batch);

    // Initialize prompt cache
    prompt_cache_ = create_prompt_cache(runtime_config_.prompt_cache);

    // Initialize metrics
    metrics_ = std::make_shared<LLMRAGMetrics>();

    // Phase 5: Initialize SQLiteSource if configured
#if QORNIX_HAS_SQLITE
    if (runtime_config_.sqlite_enabled) {
        sqlite_source_ = std::make_shared<SQLiteSource>(runtime_config_.sqlite);
        if (sqlite_source_->initialize()) {
            data_sources_.push_back(sqlite_source_);
            std::cout << "📊 SQLiteSource initialized: " << runtime_config_.sqlite.db_path << std::endl;
        } else {
            std::cerr << "⚠️ SQLiteSource was configured but failed to initialize" << std::endl;
            sqlite_source_.reset();
        }
    }
#endif

    // Phase 5: Initialize MarkdownSource if configured
    {
        if (runtime_config_.markdown_enabled) {
            markdown_source_ = std::make_shared<MarkdownSource>(runtime_config_.markdown);
            if (markdown_source_->initialize()) {
                data_sources_.push_back(markdown_source_);
                std::cout << "📝 MarkdownSource initialized: " << runtime_config_.markdown.directory_path << std::endl;
            } else {
                std::cerr << "⚠️ MarkdownSource was configured but failed to initialize" << std::endl;
                markdown_source_.reset();
            }
        }
    }

    // Phase 5: Initialize AnalyticsService
    {
        analytics_service_ = std::make_shared<AnalyticsService>(runtime_config_.analytics);
        std::cout << "📈 AnalyticsService initialized" << std::endl;
    }

    // Phase 5: Initialize DeduplicationService
    {
        dedup_service_ = std::make_shared<DeduplicationService>(runtime_config_.dedup);
        std::cout << "🔍 DeduplicationService initialized (threshold="
                  << runtime_config_.dedup.similarity_threshold << ")" << std::endl;
    }

    // Register data sources
    for (const auto& source : data_sources_) {
        rag_engine_->addDataSource(source);
    }

    if (runtime_config_.auto_index_on_startup && runtime_config_.scan_path) {
        std::cout << "📚 Indexing scan path: " << *runtime_config_.scan_path << std::endl;
        rag_engine_->index_project(*runtime_config_.scan_path);
    } else {
        std::cout << "⏭️ Directory scan disabled; RAG starts in upload/API/QA mode" << std::endl;
    }

    initialized_ = true;
    std::cout << "✅ RAG module initialized" << std::endl;
}

void RagExtension::registerRoutes(HttpServer& server, DIContainer& container) {
    (void)container;
    if (!initialized_) {
        std::cerr << "RagExtension: Not initialized, routes not registered" << std::endl;
        return;
    }

    setupRagRoutes(
        server,
        rag_engine_,
        llm_client_,
        cache_,
        rate_limiter_,
        batch_processor_,
        prompt_cache_,
        metrics_,
        analytics_service_,
        markdown_source_,
        dedup_service_
#if QORNIX_HAS_SQLITE
        , sqlite_source_
#endif
        , RagRouteOptions{
            runtime_config_.routes.expose_root_ui,
            runtime_config_.routes.ui_path,
            runtime_config_.routes.api_prefix,
            runtime_config_.security,
            runtime_config_.upload
        }
    );

    std::cout << "✅ RAG routes registered successfully" << std::endl;
}

void RagExtension::cleanup() {
    if (!initialized_) return;

    // Cleanup resources
    data_sources_.clear();

    initialized_ = false;
    std::cout << "RAG module cleaned up" << std::endl;
}

bool RagExtension::configure(const std::map<std::string, std::string>& config) {
    config_ = config;
    return configure(makeRagConfigFromIntegratedFlatMap(config));
}

bool RagExtension::configure(const RagConfig& config) {
    runtime_config_ = config;
    rag_config_ = config.engine;
    llm_config_ = config.llm;
    std::cout << "RAG module configured"
              << " (mode=" << ragRuntimeModeToString(runtime_config_.mode)
              << ", source=" << runtime_config_.config_source
              << ", ui=" << runtime_config_.routes.ui_path
              << ", api_prefix=" << runtime_config_.routes.api_prefix
              << ")" << std::endl;
    return true;
}

bool RagExtension::addDataSource(std::shared_ptr<DataSource> source) {
    data_sources_.push_back(source);
    return true;
}

bool RagExtension::reindex() {
    if (!rag_engine_ || !initialized_) return false;
    rag_engine_->indexSources();
    return true;
}

// ============================================
// Dynamic library exports (for .so)
// ============================================
extern "C" {
    ExtensionInterface* createExtension() {
        return new RagExtension();
    }

    void destroyExtension(ExtensionInterface* ext) {
        delete ext;
    }
}
