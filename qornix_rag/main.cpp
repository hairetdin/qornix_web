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

        // Configure RAG routes
        setupRagRoutes(*g_server, g_rag_engine);

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
        g_rag_engine.reset();

    } catch (const std::exception& e) {
        std::cerr << "❌ Критическая ошибка: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
