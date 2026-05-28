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
#include "rag_config.h"
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
#include <algorithm>
#include <cctype>
#include <optional>


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
    std::cout << "  --address, -a <addr>  Адрес (по умолчанию: 127.0.0.1)" << std::endl;
    std::cout << "  --scan-path, -S <path> Путь к директории для индексации (опционально)" << std::endl;
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

int main(int argc, char* argv[]) {
    // Default settings
    std::string address = "127.0.0.1";
    int port = 8081;
    std::optional<std::string> scan_path;
    std::string config_path = "config.yaml";
    RagConfig rag_runtime_config;
    bool auto_index_on_startup = true;
    bool cli_port_set = false;
    bool cli_address_set = false;
    bool cli_scan_path_set = false;
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
        else if (arg == "--scan-path" || arg == "-S") {
            if (i + 1 < argc) {
                scan_path = argv[++i];
                cli_scan_path_set = true;
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
    rag_runtime_config = makeRagConfigFromStandaloneFlatMap(
        ini,
        resolved_config_path.empty() ? "standalone defaults" : resolved_config_path);

    if (!cli_address_set) {
        address = rag_runtime_config.address;
    } else {
        rag_runtime_config.address = address;
    }
    if (!cli_port_set) {
        port = rag_runtime_config.port;
    } else {
        rag_runtime_config.port = port;
    }
    if (!cli_scan_path_set) {
        scan_path = rag_runtime_config.scan_path;
    } else {
        rag_runtime_config.scan_path = scan_path;
    }
    auto_index_on_startup = rag_runtime_config.auto_index_on_startup;
    RagEngineConfig engine_config = rag_runtime_config.engine;

    // Set signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    print_banner();

    std::cout << "🚀 Запуск Qornix RAG..." << std::endl;
    std::cout << "🧭 RAG mode: " << ragRuntimeModeToString(rag_runtime_config.mode) << std::endl;
    std::cout << "⚙️ Config source: " << rag_runtime_config.config_source << std::endl;
    std::cout << "📡 Bind address: " << address << ":" << port << std::endl;
    std::cout << "🌐 Local URL: http://localhost:" << port << std::endl;
    if (address == "0.0.0.0" || address == "::") {
        std::cout << "⚠️ Сервер слушает все сетевые интерфейсы. Для локального режима используйте 127.0.0.1." << std::endl;
    }
    std::cout << "📂 Scan path: " << (scan_path ? *scan_path : std::string("<not configured>")) << std::endl;
    std::cout << "🔎 Auto-index on startup: "
              << ((auto_index_on_startup && scan_path) ? "enabled" : "disabled")
              << std::endl;
    if (resolved_config_path.empty() && cli_config_set) {
        std::cout << "⚠️ Конфиг не найден по пути: " << config_path << std::endl;
    }
    std::cout << "🧠 Embedding backend: " << engine_config.embedding.backend << std::endl;
    std::cout << "🛟 Embedding fallback: " << (engine_config.embedding.enable_fallback ? "enabled" : "disabled") << std::endl;
    std::cout << std::endl;

    try {
        // Initialize the RAG engine
        std::cout << "🔍 Инициализация RAG движка..." << std::endl;
        g_rag_engine = std::make_shared<RagEngine>(engine_config);
        g_rag_engine->set_stop_flag(&g_running);
        auto embedding_info = g_rag_engine->get_embedding_model_info();
        std::cout << "🧬 Embedding model id: " << embedding_info.id << std::endl;
        std::cout << "📐 Embedding dimension: " << embedding_info.dimension << std::endl;
        std::cout << "🧾 Embedding status: " << embedding_info.status << std::endl;

        if (auto_index_on_startup && scan_path) {
            // Automatic indexing on startup is kept for the local standalone workflow.
            std::cout << "📚 Индексация директории..." << std::endl;
            g_rag_engine->index_project(*scan_path);

            if (!g_running.load()) {
                std::cout << "\n🛑 Получен сигнал остановки" << std::endl;
                std::cout << "👋 Остановка во время индексации" << std::endl;
                return 0;
            }

            auto stats = g_rag_engine->get_statistics();
            std::cout << "✅ Проиндексировано: " << stats.total_files << " файлов" << std::endl;
            std::cout << "   Строк кода: " << stats.total_lines << std::endl;
            std::cout << "   Размер: " << (stats.total_size_bytes / 1024) << " KB" << std::endl;
        } else {
            std::cout << "⏭️ Автоиндексация отключена; задайте indexing.scan_path/--scan-path или используйте upload/API/QA." << std::endl;
        }
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
        g_llm_client = std::make_shared<LLMClient>(rag_runtime_config.llm);
        if (g_llm_client->is_enabled()) {
            std::cout << "  ℹ️  LLM provider: " << g_llm_client->provider_name() << std::endl;
            std::cout << "  ℹ️  LLM model: " << g_llm_client->get_model() << std::endl;
            std::cout << "  ℹ️  LLM API: " << g_llm_client->get_api_url() << std::endl;

            const auto available_models = g_llm_client->list_available_models();
            if (!available_models.empty()) {
                std::cout << "  ℹ️  Available LLM models:";
                for (const auto& model : available_models) {
                    std::cout << " " << model;
                }
                std::cout << std::endl;
            } else {
                std::cout << "  ⚠️  Available LLM models: not reported" << std::endl;
            }

            if (g_llm_client->is_available()) {
                std::cout << "  ✅ LLM доступен" << std::endl;
            } else if (!available_models.empty() && !g_llm_client->configured_model_available()) {
                std::cout << "  ⚠️  LLM model not found in provider model list; "
                          << "search-only fallback enabled" << std::endl;
            } else {
                std::cout << "  ⚠️  LLM недоступен (search-only fallback enabled)" << std::endl;
            }
        } else {
            std::cout << "  ℹ️  LLM не настроен (search-only mode)" << std::endl;
        }

        // Phase 3: Initialize cache and rate limiter
        std::shared_ptr<ICache> cache;
        std::shared_ptr<RateLimiter> rate_limiter;
        std::shared_ptr<BatchProcessor> batch_processor;
        std::shared_ptr<IPromptCache> prompt_cache;
        std::shared_ptr<LLMRAGMetrics> metrics;

        try {
            cache = create_cache(rag_runtime_config.cache);
            rate_limiter = std::make_shared<RateLimiter>(rag_runtime_config.rate_limit);
            if (cache) {
                g_llm_client->set_cache(cache);
            }
            if (rate_limiter->is_available()) {
                g_llm_client->set_rate_limiter(rate_limiter);
            }

            batch_processor = std::make_shared<BatchProcessor>(rag_runtime_config.batch);
            prompt_cache = create_prompt_cache(rag_runtime_config.prompt_cache);
            metrics = std::make_shared<LLMRAGMetrics>();

            auto rag_stats = g_rag_engine->get_statistics();
            metrics->set_indexed_files(rag_stats.total_files);
            metrics->set_indexed_lines(rag_stats.total_lines);

            g_analytics_service = std::make_shared<AnalyticsService>(rag_runtime_config.analytics);
            g_dedup_service = std::make_shared<DeduplicationService>(rag_runtime_config.dedup);

#if QORNIX_HAS_SQLITE
            if (rag_runtime_config.sqlite_enabled) {
                g_sqlite_source = std::make_shared<SQLiteSource>(rag_runtime_config.sqlite);
                if (g_sqlite_source->initialize()) {
                    g_rag_engine->addDataSource(g_sqlite_source);
                    std::cout << "  ✅ SQLiteSource: " << rag_runtime_config.sqlite.db_path << std::endl;
                } else {
                    std::cerr << "  ⚠️  SQLiteSource не инициализирован" << std::endl;
                    g_sqlite_source.reset();
                }
            }
#endif

            if (rag_runtime_config.markdown_enabled) {
                g_markdown_source = std::make_shared<MarkdownSource>(rag_runtime_config.markdown);
                if (g_markdown_source->initialize()) {
                    g_rag_engine->addDataSource(g_markdown_source);
                    std::cout << "  ✅ MarkdownSource: " << rag_runtime_config.markdown.directory_path
                              << " (" << g_markdown_source->count() << " документов)" << std::endl;
                } else {
                    std::cerr << "  ⚠️  MarkdownSource не инициализирован" << std::endl;
                    g_markdown_source.reset();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "⚠️  RAG runtime config error: " << e.what() << std::endl;
        }
        std::cout << std::endl;

        // Configure RAG routes (with cache, rate limiter, batch, prompt cache, metrics)
        setupRagRoutes(*g_server, g_rag_engine, g_llm_client, cache, rate_limiter,
                      batch_processor, prompt_cache, metrics, g_analytics_service,
                      g_markdown_source, g_dedup_service
#if QORNIX_HAS_SQLITE
                      , g_sqlite_source
#endif
                      , RagRouteOptions{
                            rag_runtime_config.routes.expose_root_ui,
                            rag_runtime_config.routes.ui_path,
                            rag_runtime_config.routes.api_prefix,
                            rag_runtime_config.security,
                            rag_runtime_config.upload
                        }
        );

        g_server->run();

        std::cout << "✅ Сервер запущен!" << std::endl;
        std::cout << std::endl;
        std::cout << "🌐 Откройте в браузере: http://localhost:" << port << std::endl;
        std::cout << "💚 API Health: http://localhost:" << port << "/api/health" << std::endl;
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
