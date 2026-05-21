/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Qornix RAG - Extension for qornix_web
 *
 * Implements ExtensionInterface to be loaded dynamically or registered statically.
 * Enables optional integration of RAG module into any qornix_web application.
 */

#include "extension_interface.h"
#include "core.h"
#include "llm_client.h"
#include "data_source.h"
#include "file_source.h"
#include "qa_source.h"
#include "text_source.h"
#include "memory_source.h"
#if QORNIX_HAS_SQLITE
#include "sqlite_source.h"
#endif
#include "markdown_source.h"
#include "analytics_service.h"
#include "llm_cache.h"
#include "rate_limiter.h"
#include "batch_processor.h"
#include "prompt_cache.h"
#include "prometheus_metrics.h"

/**
 * RAG module extension for qornix_web.
 * Implements ExtensionInterface to be loaded dynamically or registered statically.
 *
 * Usage in main.cpp:
 *   auto ragExtension = std::make_shared<RagExtension>();
 *   ragExtension->configure(config_map);
 *   ragExtension->initialize(di_container);
 *   server_manager->addRouteFunction([ragExtension](HttpServer& srv) {
 *       ragExtension->registerRoutes(srv, di_container);
 *   });
 */
class RagExtension : public ExtensionInterface {
public:
    RagExtension() = default;
    ~RagExtension() override = default;

    // ExtensionInterface
    std::string getName() const override { return "qornix_rag"; }
    void registerRoutes(HttpServer& server, DIContainer& container) override;
    void initialize(DIContainer& container) override;
    void cleanup() override;

    // RAG-specific initialization
    /**
     * Configure from flattened config map (keys like "rag.enabled", "rag.llm.api_url", etc.)
     */
    bool configure(const std::map<std::string, std::string>& config);

    /**
     * Add a data source to the RAG engine.
     */
    bool addDataSource(std::shared_ptr<DataSource> source);

    /**
     * Re-index all data sources.
     */
    bool reindex();

    /**
     * Get the RagEngine pointer (for direct access if needed).
     */
    std::shared_ptr<RagEngine> getRagEngine() const { return rag_engine_; }

    /**
     * Get the LLMClient pointer (for direct access if needed).
     */
    std::shared_ptr<LLMClient> getLLMClient() const { return llm_client_; }

private:
    std::shared_ptr<RagEngine> rag_engine_;
    std::shared_ptr<LLMClient> llm_client_;
    std::vector<std::shared_ptr<DataSource>> data_sources_;
    std::shared_ptr<ICache> cache_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    std::shared_ptr<BatchProcessor> batch_processor_;
    std::shared_ptr<IPromptCache> prompt_cache_;
    std::shared_ptr<LLMRAGMetrics> metrics_;

    // Phase 5: Additional data sources
#if QORNIX_HAS_SQLITE
    std::shared_ptr<SQLiteSource> sqlite_source_;
#endif
    std::shared_ptr<MarkdownSource> markdown_source_;
    std::shared_ptr<AnalyticsService> analytics_service_;

    RagEngineConfig rag_config_;
    LLMConfig llm_config_;
    std::map<std::string, std::string> config_;  // Phase 5: Store full config
    bool initialized_ = false;
};
