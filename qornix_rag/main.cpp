/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Qornix RAG - entry point
 *
 * Start the web server with the RAG system
 */

#include "core.h"
#include "web.h"
#include "llm_client.h"
#include "llm_cache.h"
#include "rate_limiter.h"
#include "batch_processor.h"
#include "prompt_cache.h"
#include "prometheus_metrics.h"
#include "analytics_service.h"
#include "deduplication_service.h"
#include "markdown_source.h"
#if QORNIX_HAS_SQLITE
#include "sqlite_source.h"
#endif
#include "../include/handler_base.h"
#include "../include/http_server.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <thread>
#include <csignal>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <functional>


// Global variables
std::shared_ptr<RagEngine> g_rag_engine;
std::shared_ptr<LLMClient> g_llm_client;
std::shared_ptr<AnalyticsService> g_analytics_service;
std::shared_ptr<MarkdownSource> g_markdown_source;
std::shared_ptr<DeduplicationService> g_dedup_service;
#if QORNIX_HAS_SQLITE
std::shared_ptr<SQLiteSource> g_sqlite_source;
#endif
std::unique_ptr<HttpServer> g_server;
std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    (void)signum;
    g_running.store(false);
}

void print_banner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║           🔍  QORNIX RAG SEARCH ENGINE  🔍                ║
║                                                           ║
║      Retrieval-Augmented Generation для C++ проектов      ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
    )" << std::endl;
}

void print_usage(const char* program) {
    std::cout << "Использование:" << std::endl;
    std::cout << "  " << program << " [опции]" << std::endl;
    std::cout << "\nОпции:" << std::endl;
    std::cout << "  --port, -p <port>     Порт сервера (по умолчанию: 8081)" << std::endl;
    std::cout << "  --address, -a <addr>  Адрес (по умолчанию: 0.0.0.0)" << std::endl;
    std::cout << "  --project, -P <path>  Путь к проекту для индексации" << std::endl;
    std::cout << "  --config, -c <path>   Путь к config.yaml (по умолчанию: ./config.yaml)" << std::endl;
    std::cout << "  --help, -h            Показать эту справку" << std::endl;
}

static std::unordered_map<std::string, std::string> loadIniWithFallback(
    const std::string &config_path,
    const std::string &argv0,
    std::string &resolved_path
) {
    auto try_load = [config_path](const std::string &path) -> std::unordered_map<std::string, std::string> {
        std::ifstream file(path);
        if (!file.is_open()) {
            return {};
        }
        file.close();

        try {
            YAML::Node node = YAML::LoadFile(path);
            std::unordered_map<std::string, std::string> values;

            std::function<void(const YAML::Node&, const std::string&)> flatten;
            flatten = [&](const YAML::Node &parent, const std::string &prefix) {
                for (const auto &it : parent) {
                    std::string key = it.first.as<std::string>();
                    const auto &val = it.second;
                    std::string full_key = prefix.empty() ? key : prefix + "." + key;

                    if (val.IsMap()) {
                        flatten(val, full_key);
                    } else if (val.IsSequence()) {
                        // Skip sequences — they're not used in current config parsing.
                    } else if (val.IsScalar() || val.IsDefined()) {
                        values[full_key] = val.as<std::string>();
                    }
                }
            };

            flatten(node, "");
            return values;
        } catch (const std::exception &e) {
            std::cerr << "⚠️ YAML parse error in " << path << ": " << e.what() << "\n";
            return {};
        }
    };

    auto values = try_load(config_path);
    if (!values.empty()) {
        resolved_path = config_path;
        return values;
    }

    try {
        std::filesystem::path exe_path = std::filesystem::absolute(argv0);
        std::filesystem::path exe_dir = exe_path.parent_path();

        std::vector<std::filesystem::path> candidates = {
            exe_dir / "config.yaml",
            exe_dir.parent_path() / "config.yaml"
        };

        for (const auto &candidate : candidates) {
            values = try_load(candidate.string());
            if (!values.empty()) {
                resolved_path = candidate.string();
                return values;
            }
        }
    } catch (...) {
        // Ignore fallback resolution errors.
    }

    resolved_path.clear();
    return {};
}

static bool toBool(const std::string &value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes") return true;
    if (value == "false" || value == "0" || value == "no") return false;
    return fallback;
}

int main(int argc, char* argv[]) {
    // Default settings
    std::string address = "0.0.0.0";
    int port = 8081;
    std::string project_path = ".";
    std::string config_path = "config.yaml";
    RagEngineConfig engine_config;
    bool cli_port_set = false;
    bool cli_address_set = false;
    bool cli_project_set = false;
    bool cli_config_set = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--port" || arg == "-p") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
                cli_port_set = true;
            }
        }
        else if (arg == "--address" || arg == "-a") {
            if (i + 1 < argc) {
                address = argv[++i];
                cli_address_set = true;
            }
        }
        else if (arg == "--project" || arg == "-P") {
            if (i + 1 < argc) {
                project_path = argv[++i];
                cli_project_set = true;
            }
        }
        else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) {
                config_path = argv[++i];
                cli_config_set = true;
            }
        }
    }

    std::string resolved_config_path;
    auto ini = loadIniWithFallback(config_path, argv[0], resolved_config_path);
    if (!ini.empty()) {
        if (!cli_address_set && ini.count("server.address")) address = ini["server.address"];
        if (!cli_port_set && ini.count("server.port")) port = std::stoi(ini["server.port"]);
        if (!cli_project_set && ini.count("server.project_path")) project_path = ini["server.project_path"];

        if (ini.count("search.use_hybrid")) engine_config.search.use_hybrid = toBool(ini["search.use_hybrid"], true);
        if (ini.count("search.vector_weight")) engine_config.search.vector_weight = std::stof(ini["search.vector_weight"]);
        if (ini.count("search.text_weight")) engine_config.search.text_weight = std::stof(ini["search.text_weight"]);
        if (ini.count("search.top_k")) engine_config.search.top_k = static_cast<size_t>(std::stoul(ini["search.top_k"]));
        if (ini.count("search.min_score_threshold")) engine_config.search.min_score_threshold = std::stof(ini["search.min_score_threshold"]);
        if (ini.count("indexing.max_file_size_kb")) engine_config.max_file_size_kb = static_cast<size_t>(std::stoul(ini["indexing.max_file_size_kb"]));

        if (ini.count("embedding.backend")) engine_config.embedding.backend = ini["embedding.backend"];
        if (ini.count("embedding.model_path")) engine_config.embedding.model_path = ini["embedding.model_path"];
        if (ini.count("embedding.tokenizer_path")) engine_config.embedding.tokenizer_path = ini["embedding.tokenizer_path"];
        if (ini.count("embedding.max_seq_len")) engine_config.embedding.max_seq_len = static_cast<size_t>(std::stoul(ini["embedding.max_seq_len"]));
        if (ini.count("embedding.onnx_threads")) engine_config.embedding.onnx_threads = static_cast<size_t>(std::stoul(ini["embedding.onnx_threads"]));
        if (ini.count("embedding.normalize_embeddings")) engine_config.embedding.normalize_embeddings = toBool(ini["embedding.normalize_embeddings"], true);
        if (ini.count("embedding.enable_fallback")) engine_config.embedding.enable_fallback = toBool(ini["embedding.enable_fallback"], true);
    }

    // Set signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    print_banner();

    std::cout << "🚀 Запуск Qornix RAG..." << std::endl;
    std::cout << "📡 Адрес: " << address << ":" << port << std::endl;
    std::cout << "📂 Проект: " << project_path << std::endl;
    if (!resolved_config_path.empty()) {
        std::cout << "⚙️ Конфиг: " << resolved_config_path << std::endl;
    } else if (cli_config_set) {
        std::cout << "⚠️ Конфиг не найден по пути: " << config_path << std::endl;
    } else {
        std::cout << "⚠️ Конфиг не найден, используются значения по умолчанию" << std::endl;
    }
    std::cout << std::endl;

    try {
        // Initialize the RAG engine
        std::cout << "🔍 Инициализация RAG движка..." << std::endl;
        g_rag_engine = std::make_shared<RagEngine>(engine_config);
        g_rag_engine->set_stop_flag(&g_running);

        // Automatic indexing on startup
        std::cout << "📚 Индексация проекта..." << std::endl;
        g_rag_engine->index_project(project_path);

        if (!g_running.load()) {
            std::cout << "\n🛑 Получен сигнал остановки" << std::endl;
            std::cout << "👋 Остановка во время индексации" << std::endl;
            return 0;
        }

        auto stats = g_rag_engine->get_statistics();
        std::cout << "✅ Проиндексировано: " << stats.total_files << " файлов" << std::endl;
        std::cout << "   Строк кода: " << stats.total_lines << std::endl;
        std::cout << "   Размер: " << (stats.total_size_bytes / 1024) << " KB" << std::endl;
        std::cout << std::endl;

        // Create and configure the server
        std::cout << "🌐 Запуск веб-сервера..." << std::endl;

        net::io_context ioc;
        auto endpoint = net::ip::tcp::endpoint(
            net::ip::make_address(address),
            port
        );

        g_server = std::make_unique<HttpServer>(ioc, endpoint);

        // Initialize LLM client (if config has llm section)
        std::cout << "🤖 Инициализация LLM клиента..." << std::endl;
        g_llm_client = std::make_shared<LLMClient>(resolved_config_path);
        if (g_llm_client->is_enabled()) {
            std::cout << "  ℹ️  LLM: " << g_llm_client->get_model()
                      << " @ " << g_llm_client->get_api_url() << std::endl;
            if (g_llm_client->is_available()) {
                std::cout << "  ✅ LLM доступен" << std::endl;
            } else {
                std::cout << "  ⚠️  LLM недоступен (будет использован только поиск)" << std::endl;
            }
        } else {
            std::cout << "  ℹ️  LLM не настроен (только поиск)" << std::endl;
        }

        // Phase 3: Initialize cache and rate limiter
        std::shared_ptr<ICache> cache;
        std::shared_ptr<RateLimiter> rate_limiter;
        std::shared_ptr<BatchProcessor> batch_processor;
        std::shared_ptr<IPromptCache> prompt_cache;
        std::shared_ptr<LLMRAGMetrics> metrics;

        if (!resolved_config_path.empty()) {
            try {
#ifdef QORNIX_HAS_YAML
                YAML::Node node = YAML::LoadFile(resolved_config_path);

                // Cache config
                CacheConfig cache_config;
                if (node["cache"]) {
                    const auto& cache_node = node["cache"];
                    cache_config.enabled = cache_node["enabled"] && cache_node["enabled"].as<bool>();
                    if (cache_node["backend"]) {
                        cache_config.backend = cache_node["backend"].as<std::string>();
                    }
                    if (cache_node["max_size"]) {
                        cache_config.max_size = cache_node["max_size"].as<size_t>();
                    }
                    if (cache_node["ttl_seconds"]) {
                        cache_config.ttl = std::chrono::seconds(cache_node["ttl_seconds"].as<int>());
                    }
                    if (cache_node["redis"]) {
                        const auto& redis = cache_node["redis"];
                        if (redis["host"]) cache_config.redis_host = redis["host"].as<std::string>();
                        if (redis["port"]) cache_config.redis_port = redis["port"].as<int>();
                        if (redis["db"]) cache_config.redis_db = redis["db"].as<int>();
                        if (redis["password"]) cache_config.redis_password = redis["password"].as<std::string>();
                        if (redis["ttl_seconds"]) cache_config.redis_ttl = std::chrono::seconds(redis["ttl_seconds"].as<int>());
                    }
                }
                cache = create_cache(cache_config);

                // Rate limiter config
                RateLimiterConfig rl_config;
                if (node["rate_limit"]) {
                    const auto& rl_node = node["rate_limit"];
                    rl_config.enabled = rl_node["enabled"] && rl_node["enabled"].as<bool>();
                    if (rl_node["max_requests_per_second"]) {
                        rl_config.max_requests_per_second = rl_node["max_requests_per_second"].as<size_t>();
                    }
                    if (rl_node["max_requests_per_minute"]) {
                        rl_config.max_requests_per_minute = rl_node["max_requests_per_minute"].as<size_t>();
                    }
                    if (rl_node["per_ip_limit"]) {
                        rl_config.per_ip_limit = rl_node["per_ip_limit"].as<bool>();
                    }
                    if (rl_node["max_requests_per_second_per_ip"]) {
                        rl_config.max_requests_per_second_per_ip = rl_node["max_requests_per_second_per_ip"].as<size_t>();
                    }
                    if (rl_node["max_requests_per_minute_per_ip"]) {
                        rl_config.max_requests_per_minute_per_ip = rl_node["max_requests_per_minute_per_ip"].as<size_t>();
                    }
                }
                rate_limiter = std::make_shared<RateLimiter>(rl_config);

                // Connect cache and rate limiter to LLM client
                if (cache) {
                    g_llm_client->set_cache(cache);
                }
                if (rate_limiter->is_available()) {
                    g_llm_client->set_rate_limiter(rate_limiter);
                }

                // Phase 3: Batch processor config
                BatchConfig batch_config;
                if (node["batch"]) {
                    const auto& batch_node = node["batch"];
                    if (batch_node["max_concurrent"]) {
                        batch_config.max_concurrent = batch_node["max_concurrent"].as<size_t>();
                    }
                    if (batch_node["question_timeout_ms"]) {
                        batch_config.question_timeout_ms = batch_node["question_timeout_ms"].as<int>();
                    }
                    if (batch_node["batch_timeout_ms"]) {
                        batch_config.batch_timeout_ms = batch_node["batch_timeout_ms"].as<int>();
                    }
                }
                batch_processor = std::make_shared<BatchProcessor>(batch_config);

                // Phase 3: Prompt cache config
                PromptCacheConfig prompt_cache_config;
                if (node["prompt_cache"]) {
                    const auto& pc_node = node["prompt_cache"];
                    prompt_cache_config.enabled = pc_node["enabled"] && pc_node["enabled"].as<bool>();
                    if (pc_node["max_size"]) {
                        prompt_cache_config.max_size = pc_node["max_size"].as<size_t>();
                    }
                    if (pc_node["ttl_seconds"]) {
                        prompt_cache_config.ttl = std::chrono::seconds(pc_node["ttl_seconds"].as<int>());
                    }
                    prompt_cache = create_prompt_cache(prompt_cache_config);
                }

                // Phase 3: Prometheus metrics
                metrics = std::make_shared<LLMRAGMetrics>();

                // Update metrics with current stats
                auto rag_stats = g_rag_engine->get_statistics();
                metrics->set_indexed_files(rag_stats.total_files);
                metrics->set_indexed_lines(rag_stats.total_lines);

                // Phase 5: Analytics service
                AnalyticsService::Config analytics_config;
                if (node["rag"] && node["rag"]["analytics"]) {
                    const auto& analytics_node = node["rag"]["analytics"];
                    if (analytics_node["max_log_entries"]) {
                        analytics_config.max_log_entries = analytics_node["max_log_entries"].as<size_t>();
                    }
                    if (analytics_node["top_n"]) {
                        analytics_config.top_n = analytics_node["top_n"].as<size_t>();
                    }
                    if (analytics_node["gap_min_search_count"]) {
                        analytics_config.gap_min_search_count = analytics_node["gap_min_search_count"].as<size_t>();
                    }
                }
                g_analytics_service = std::make_shared<AnalyticsService>(analytics_config);

                // Phase 5: Deduplication service
                DeduplicationService::Config dedup_config;
                if (node["rag"] && node["rag"]["dedup"]) {
                    const auto& dedup_node = node["rag"]["dedup"];
                    if (dedup_node["similarity_threshold"]) {
                        dedup_config.similarity_threshold = dedup_node["similarity_threshold"].as<float>();
                    }
                    if (dedup_node["auto_remove"]) {
                        dedup_config.auto_remove = dedup_node["auto_remove"].as<bool>();
                    }
                }
                g_dedup_service = std::make_shared<DeduplicationService>(dedup_config);

#if QORNIX_HAS_SQLITE
                // Phase 5: SQLite-backed QA storage
                if (node["rag"] && node["rag"]["sqlite"] &&
                    node["rag"]["sqlite"]["enabled"] &&
                    node["rag"]["sqlite"]["enabled"].as<bool>()) {
                    const auto& sqlite_node = node["rag"]["sqlite"];
                    SQLiteSource::Config sqlite_config;
                    sqlite_config.db_path = sqlite_node["db_path"]
                        ? sqlite_node["db_path"].as<std::string>()
                        : "rag_kb.db";
                    sqlite_config.source_id = sqlite_node["source_id"]
                        ? sqlite_node["source_id"].as<std::string>()
                        : "sqlite_kb";
                    sqlite_config.name = sqlite_node["name"]
                        ? sqlite_node["name"].as<std::string>()
                        : "SQLite Knowledge Base";
                    sqlite_config.auto_migrate = !sqlite_node["auto_migrate"] ||
                        sqlite_node["auto_migrate"].as<bool>();

                    g_sqlite_source = std::make_shared<SQLiteSource>(sqlite_config);
                    if (g_sqlite_source->initialize()) {
                        g_rag_engine->addDataSource(g_sqlite_source);
                        std::cout << "  ✅ SQLiteSource: " << sqlite_config.db_path << std::endl;
                    } else {
                        std::cerr << "  ⚠️  SQLiteSource не инициализирован" << std::endl;
                        g_sqlite_source.reset();
                    }
                }
#endif

                // Phase 5: Markdown import source
                if (node["rag"] && node["rag"]["markdown"] &&
                    node["rag"]["markdown"]["enabled"] &&
                    node["rag"]["markdown"]["enabled"].as<bool>()) {
                    const auto& markdown_node = node["rag"]["markdown"];
                    MarkdownSource::Config markdown_config;
                    markdown_config.directory_path = markdown_node["directory_path"]
                        ? markdown_node["directory_path"].as<std::string>()
                        : "knowledge_base";
                    markdown_config.recursive = !markdown_node["recursive"] ||
                        markdown_node["recursive"].as<bool>();

                    g_markdown_source = std::make_shared<MarkdownSource>(markdown_config);
                    if (g_markdown_source->initialize()) {
                        g_rag_engine->addDataSource(g_markdown_source);
                        std::cout << "  ✅ MarkdownSource: " << markdown_config.directory_path
                                  << " (" << g_markdown_source->count() << " документов)" << std::endl;
                    } else {
                        std::cerr << "  ⚠️  MarkdownSource не инициализирован" << std::endl;
                        g_markdown_source.reset();
                    }
                }
#endif
            } catch (const std::exception& e) {
                std::cerr << "⚠️  Cache/rate_limiter/batch config error: " << e.what() << std::endl;
            }
        }
        std::cout << std::endl;

        // Configure RAG routes (with cache, rate limiter, batch, prompt cache, metrics)
        setupRagRoutes(*g_server, g_rag_engine, g_llm_client, cache, rate_limiter,
                      batch_processor, prompt_cache, metrics, g_analytics_service,
                      g_markdown_source, g_dedup_service
#if QORNIX_HAS_SQLITE
                      , g_sqlite_source
#endif
        );

        g_server->run();

        std::cout << "✅ Сервер запущен!" << std::endl;
        std::cout << std::endl;
        std::cout << "🌐 Откройте в браузере: http://localhost:" << port << std::endl;
        std::cout << "📊 API Stats: http://localhost:" << port << "/api/stats" << std::endl;
        std::cout << "🔍 API Search: http://localhost:" << port << "/api/search" << std::endl;
        std::cout << std::endl;
        std::cout << "Нажмите Ctrl+C для остановки" << std::endl;
        std::cout << "📊 Потоков: " << std::thread::hardware_concurrency() << std::endl;
        std::cout << std::string(60, '=') << std::endl;

        // Create a thread pool to handle connections
        const size_t num_threads = std::max(2u, std::thread::hardware_concurrency());
        std::vector<std::thread> threads;

        for (size_t i = 0; i < num_threads; ++i) {
            threads.emplace_back([&ioc]() {
                ioc.run();
            });
        }

        // Wait for the stop signal
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n🛑 Получен сигнал остановки" << std::endl;

        // Stop the server
        ioc.stop();
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        std::cout << "\n👋 Остановка сервера..." << std::endl;
        g_server.reset();
        g_dedup_service.reset();
        g_markdown_source.reset();
        g_analytics_service.reset();
#if QORNIX_HAS_SQLITE
        g_sqlite_source.reset();
#endif
        g_llm_client.reset();
        g_rag_engine.reset();

    } catch (const std::exception& e) {
        std::cerr << "❌ Критическая ошибка: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
