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

void RagExtension::initialize(DIContainer& container) {
    if (initialized_) return;

    std::cout << "🚀 Initializing RAG module..." << std::endl;

    // Initialize RagEngine
    rag_engine_ = std::make_shared<RagEngine>(rag_config_);

    // Initialize LLMClient (config from YAML or defaults)
    llm_client_ = std::make_shared<LLMClient>("");

    // Initialize cache
    CacheConfig cache_config;
    cache_config.enabled = true;
    cache_config.backend = "memory";
    cache_config.ttl = std::chrono::seconds(3600);
    cache_config.max_size = 1000;
    cache_ = create_cache(cache_config);

    // Initialize rate limiter
    RateLimiterConfig rl_config;
    rl_config.enabled = true;
    rl_config.max_requests_per_second = 10;
    rl_config.max_requests_per_minute = 100;
    rl_config.per_ip_limit = true;
    rl_config.max_requests_per_second_per_ip = 2;
    rate_limiter_ = std::make_shared<RateLimiter>(rl_config);

    // Initialize batch processor
    BatchConfig batch_config;
    batch_config.max_concurrent = 4;
    batch_config.question_timeout_ms = 60000;
    batch_processor_ = std::make_shared<BatchProcessor>(batch_config);

    // Initialize prompt cache
    PromptCacheConfig pc_config;
    pc_config.enabled = true;
    pc_config.max_size = 500;
    pc_config.ttl = std::chrono::seconds(1800);
    prompt_cache_ = create_prompt_cache(pc_config);

    // Initialize metrics
    metrics_ = std::make_shared<LLMRAGMetrics>();

    // Phase 5: Initialize SQLiteSource if configured
#if QORNIX_HAS_SQLITE
    if (auto it = config_.find("rag.sqlite.enabled"); it != config_.end() && it->second == "true") {
        SQLiteSource::Config sqlite_config;
        sqlite_config.db_path = "rag_kb.db";
        if (auto db_it = config_.find("rag.sqlite.db_path"); db_it != config_.end()) {
            sqlite_config.db_path = db_it->second;
        }
        sqlite_config.source_id = "sqlite_kb";
        sqlite_config.name = "SQLite Knowledge Base";
        sqlite_config.auto_migrate = true;
        sqlite_source_ = std::make_shared<SQLiteSource>(sqlite_config);
        data_sources_.push_back(sqlite_source_);
        std::cout << "📊 SQLiteSource initialized: " << sqlite_config.db_path << std::endl;
    }
#endif

    // Phase 5: Initialize MarkdownSource if configured
    {
        if (auto it = config_.find("rag.markdown.enabled"); it != config_.end() && it->second == "true") {
            MarkdownSource::Config md_config;
            md_config.directory_path = "./knowledge_base";
            if (auto dir_it = config_.find("rag.markdown.directory_path"); dir_it != config_.end()) {
                md_config.directory_path = dir_it->second;
            }
            md_config.file_patterns = {"*.md"};
            md_config.recursive = true;
            markdown_source_ = std::make_shared<MarkdownSource>(md_config);
            data_sources_.push_back(markdown_source_);
            std::cout << "📝 MarkdownSource initialized: " << md_config.directory_path << std::endl;
        }
    }

    // Phase 5: Initialize AnalyticsService
    {
        AnalyticsService::Config analytics_config;
        if (auto max_it = config_.find("rag.analytics.max_log_entries"); max_it != config_.end()) {
            analytics_config.max_log_entries = std::stoull(max_it->second);
        }
        analytics_service_ = std::make_shared<AnalyticsService>(analytics_config);
        std::cout << "📈 AnalyticsService initialized" << std::endl;
    }

    // Register data sources
    for (const auto& source : data_sources_) {
        rag_engine_->addDataSource(source);
    }

    // Index all sources
    reindex();

    initialized_ = true;
    std::cout << "✅ RAG module initialized" << std::endl;
}

void RagExtension::registerRoutes(HttpServer& server, DIContainer& container) {
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
        analytics_service_
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
    // Store full config for later use (Phase 5)
    config_ = config;

    // Parse rag.enabled
    if (auto it = config.find("rag.enabled"); it != config.end()) {
        // Enabled flag - just note it
    }

    // Parse LLM config
    if (auto it = config.find("rag.llm.enabled"); it != config.end()) {
        llm_config_.enabled = (it->second == "true");
    }
    if (auto it = config.find("rag.llm.api_url"); it != config.end()) {
        llm_config_.api_url = it->second;
    }
    if (auto it = config.find("rag.llm.api_key"); it != config.end()) {
        llm_config_.api_key = it->second;
    }
    if (auto it = config.find("rag.llm.model"); it != config.end()) {
        llm_config_.model = it->second;
    }
    if (auto it = config.find("rag.llm.max_tokens"); it != config.end()) {
        llm_config_.max_tokens = std::stoi(it->second);
    }
    if (auto it = config.find("rag.llm.temperature"); it != config.end()) {
        llm_config_.temperature = std::stof(it->second);
    }
    if (auto it = config.find("rag.llm.request_timeout_ms"); it != config.end()) {
        llm_config_.request_timeout_ms = std::stoi(it->second);
    }

    // Parse search config
    if (auto it = config.find("rag.search.vector_weight"); it != config.end()) {
        rag_config_.search.vector_weight = std::stof(it->second);
    }
    if (auto it = config.find("rag.search.text_weight"); it != config.end()) {
        rag_config_.search.text_weight = std::stof(it->second);
    }
    if (auto it = config.find("rag.search.top_k"); it != config.end()) {
        rag_config_.search.top_k = std::stoi(it->second);
    }
    if (auto it = config.find("rag.search.min_score_threshold"); it != config.end()) {
        rag_config_.search.min_score_threshold = std::stof(it->second);
    }

    // Parse embedding config
    if (auto it = config.find("rag.embedding.backend"); it != config.end()) {
        rag_config_.embedding.backend = it->second;
    }

    // Parse cache config
    if (auto it = config.find("rag.cache.enabled"); it != config.end()) {
        // Cache will be configured in initialize()
    }

    // Parse rate limit config
    if (auto it = config.find("rag.rate_limit.max_requests_per_second"); it != config.end()) {
        // Rate limiter will be configured in initialize()
    }

    std::cout << "RAG module configured" << std::endl;
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
