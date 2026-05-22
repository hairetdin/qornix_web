/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "core.h"
#include "llm_client.h"
#include "llm_cache.h"
#include "rate_limiter.h"
#include "batch_processor.h"
#include "prompt_cache.h"
#include "analytics_service.h"
#include "deduplication_service.h"
#if QORNIX_HAS_SQLITE
#include "sqlite_source.h"
#endif
#include "markdown_source.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

enum class RagRuntimeMode {
    Standalone,
    Integrated,
    Programmatic
};

struct RagRouteConfig {
    bool expose_root_ui = true;
    std::string ui_path = "/";
    std::string api_prefix = "/api";
};

struct RagConfigDiagnostics {
    std::vector<std::string> warnings;
};

struct RagConfig {
    RagRuntimeMode mode = RagRuntimeMode::Programmatic;
    std::string config_source = "programmatic";

    std::string address = "127.0.0.1";
    int port = 8081;
    std::string project_path = ".";
    bool auto_index_on_startup = true;

    RagEngineConfig engine;
    LLMConfig llm;
    CacheConfig cache;
    RateLimiterConfig rate_limit;
    BatchConfig batch;
    PromptCacheConfig prompt_cache;
    AnalyticsService::Config analytics;
    DeduplicationService::Config dedup;
    MarkdownSource::Config markdown;
    bool markdown_enabled = false;
#if QORNIX_HAS_SQLITE
    SQLiteSource::Config sqlite;
    bool sqlite_enabled = false;
#endif
    RagRouteConfig routes;
    RagConfigDiagnostics diagnostics;
};

std::string ragRuntimeModeToString(RagRuntimeMode mode);

RagConfig makeRagConfigFromStandaloneFlatMap(
    const std::unordered_map<std::string, std::string>& flat,
    const std::string& config_source);

RagConfig makeRagConfigFromIntegratedFlatMap(
    const std::map<std::string, std::string>& flat,
    const std::string& config_source = "host application config");

