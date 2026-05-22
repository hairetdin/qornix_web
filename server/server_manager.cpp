/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "extension_loader.h"
#include "server_manager.h"
#include "logging_middleware.h"
#include "../include/config_parser.h"
#include <iostream>
#include <thread>
#include <fstream>
#include <filesystem>
#include <functional>

#include "log_initializer.h"

static std::string resolveConfigPath(const std::string &config_path, const std::string &argv0) {
    // Always try the requested path first. ConfigParser::load() returns defaults
    // when a file is missing, so config discovery must check file validity
    // explicitly rather than relying on default settings values.
    if (ConfigParser::canLoad(config_path)) {
        return config_path;
    }

    // Fallback: search near the executable (up to 4 parent dirs).
    try {
        std::filesystem::path exe_path = std::filesystem::absolute(argv0);
        std::filesystem::path exe_dir = exe_path.parent_path();

        std::vector<std::filesystem::path> candidates;
        std::filesystem::path cur = exe_dir;
        for (int i = 0; i < 4; ++i) {
            candidates.push_back(cur / "config.yaml");
            cur = cur.parent_path();
        }

        for (const auto &candidate : candidates) {
            if (ConfigParser::canLoad(candidate.string())) {
                return candidate.string();
            }
        }
    } catch (...) {
        // Ignore fallback resolution errors.
    }

    return {};
}

static std::map<std::string, std::string> loadFlatConfig(const std::string& path) {
    std::map<std::string, std::string> values;
    if (path.empty()) {
        return values;
    }

    try {
        YAML::Node node = YAML::LoadFile(path);
        std::function<void(const YAML::Node&, const std::string&)> flatten;
        flatten = [&](const YAML::Node& parent, const std::string& prefix) {
            for (const auto& it : parent) {
                const std::string key = it.first.as<std::string>();
                const auto& val = it.second;
                const std::string full_key = prefix.empty() ? key : prefix + "." + key;
                if (val.IsMap()) {
                    flatten(val, full_key);
                } else if (val.IsScalar()) {
                    values[full_key] = val.as<std::string>();
                }
            }
        };
        flatten(node, "");
    } catch (const std::exception& e) {
        std::cerr << "Config flatten error in " << path << ": " << e.what() << std::endl;
    }

    return values;
}

ServerSettings settings_;
std::string g_resolved_config_path;
net::io_context ioc_;
std::unique_ptr<HttpServer> server_;
DIContainer di_container_;

ServerManager::ServerManager(int argc, char* argv[]) {
    // Load config from config.yaml with fallback — MUST happen before setup_logging()
    std::string config_path = "config.yaml";
    g_resolved_config_path = resolveConfigPath(config_path, argv[0]);
    if (!g_resolved_config_path.empty()) {
        settings_ = ConfigParser::load(g_resolved_config_path);
        flat_config_ = loadFlatConfig(g_resolved_config_path);
    }

    parse_arguments(argc, argv);
    setup_logging();      // Initialize logging (uses settings_ from config)
    setup_di_container();  // Initialize DI container
    setup_server();
}

ServerManager::~ServerManager() = default;

const std::map<std::string, std::string>& ServerManager::getConfig() const {
    return flat_config_;
}

DIContainer& ServerManager::getDIContainer() {
    return di_container_;
}

std::unique_ptr<ServerManager> create_server_manager(int argc, char* argv[]) {
    return std::make_unique<ServerManager>(argc, argv);
}

void ServerManager::setup_server() {
    // Fallback to default address if no config was found
    std::string addr = settings_.address.empty() ? "127.0.0.1" : settings_.address;
    auto const address = net::ip::make_address(addr);
    tcp::endpoint tcp_endpoint{address, settings_.port};

    server_ = std::make_unique<HttpServer>(ioc_, tcp_endpoint);
    setup_middleware();
    // setup_routes();  // routes are added when the server starts: ServerManager::run()
}

void ServerManager::setup_logging() {
    if (!settings_.enable_logging) {
        LogInitializer::disableLogging();
        return;
    }

    // Always enable common attributes
    LogInitializer::enableTimestamps();

    // Configure logging level
    boost::log::trivial::severity_level level;
    switch (settings_.log_level) {
        case LogLevel::trace:
            level = boost::log::trivial::trace;
            break;
        case LogLevel::debug:
            level = boost::log::trivial::debug;
            break;
        case LogLevel::info:
            level = boost::log::trivial::info;
            break;
        case LogLevel::warning:
            level = boost::log::trivial::warning;
            break;
        case LogLevel::error:
            level = boost::log::trivial::error;
            break;
        case LogLevel::fatal:
            level = boost::log::trivial::fatal;
            break;
        default:
            level = boost::log::trivial::info;
    }

    // Initialize console output
    LogInitializer::initConsoleLogging();

    // If file logging is needed, initialize a file sink
    if (settings_.log_to_file) {
        LogInitializer::initFileLogging(
            settings_.log_file_path,
            settings_.log_rotation_size,
            settings_.log_max_files
        );
    }

    // Set filter level
    LogInitializer::setLogLevel(level);

    BOOST_LOG_TRIVIAL(info) << "=== Server logging initialized ===";
    BOOST_LOG_TRIVIAL(info) << "Log level: " << ConfigParser::logLevelToString(settings_.log_level);
    BOOST_LOG_TRIVIAL(info) << "Console logging: enabled";
    BOOST_LOG_TRIVIAL(info) << "File logging: " << (settings_.log_to_file ? "enabled" : "disabled");
}

void ServerManager::parse_arguments(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log" || arg == "-l") {
            settings_.enable_logging = true;
        } else if (arg == "--log-level" && i + 1 < argc) {
            std::string level = argv[++i];
            if(level == "trace") settings_.log_level = LogLevel::trace;
            else if (level == "debug") settings_.log_level = LogLevel::debug;
            else if(level == "info") settings_.log_level = LogLevel::info;
            else if (level == "warning") settings_.log_level = LogLevel::warning;
            else if (level == "error") settings_.log_level = LogLevel::error;
            else if (level == "fatal") settings_.log_level = LogLevel::fatal;
        } else if (arg == "--log-file" && i + 1 < argc) {
            settings_.log_to_file = true;
            settings_.log_file_path = argv[++i];
        } else if (arg == "--log-rotation" && i + 1 < argc) {
            settings_.log_rotation_size = std::stoul(argv[++i]) * 1024 * 1024; // MB
        } else if (arg == "--log-max-files" && i + 1 < argc) {
            settings_.log_max_files = std::stoul(argv[++i]);
        }
    }
}

void ServerManager::setup_di_container() {
    // Register logger as singleton: one instance for the whole application
    di_container_.registerType<LoggingMiddleware>(
            "logging_middleware",
            []() {
                return std::make_shared<LoggingMiddleware>(settings_.enable_logging);
            },
            Lifetime::SINGLETON
    );

    // Register database as singleton
//    di_container_.registerType<Database>(
//            "database",
//            []() {
//                return std::make_shared<Database>();
//            },
//            Lifetime::SINGLETON  // One database connection for the whole application
//    );

    // Register user service as transient
    // New instance on each request (can be useful for tracking state)
//    di_container_.registerType<UserService>(
//            "user_service",
//            [this]() {
//                auto db = di_container_.resolve<Database>("database");
//                return std::make_shared<UserService>(db);
//            },
//            Lifetime::TRANSIENT
//    );

    // Register handler as singleton
//    di_container_.registerType<UserHandler>(
//            "user_handler",
//            [this]() {
//                return std::make_shared<UserHandler>();
//            },
//            Lifetime::SINGLETON  // One handler for all requests
//    );
}

void ServerManager::setup_middleware() {
    // Get LoggingMiddleware from the DI container (created with the correct enabled_ from config)
    auto logging_middleware = di_container_.resolve<LoggingMiddleware>("logging_middleware");
    server_->add_middleware(logging_middleware);

    // Authentication middleware (optional, commented out by default)
    // qornix_auth::AuthConfig auth_config;
    // auth_config.mode = qornix_auth::AuthMode::BOTH;
    // auth_config.jwtSecret = "your-secret-key-here";
    // auto authManager = std::make_shared<qornix_auth::AuthManager>(auth_config);
    // auto auth_middleware = create_auth_middleware(
    //     authManager,
    //     true,  // authentication required
    //     {"/api/login", "/api/register", "/health"}  // excluded paths
    //     );
    // server_->add_middleware(auth_middleware);
}

void ServerManager::setup_routes() {
    // Core library must not depend on application-level routes.h.
    // Applications register their routes through addRouteFunction().
    std::cout << "DEBUG: Setting up routes..." << std::endl;

    for (const auto& route_func : custom_routes_) {
        if (route_func) {
            route_func(*server_);
        }
    }

    // Load dynamic route extensions, if present.
    std::string routeExtensionsPath = "./route_extensions";
    extension_loader_ = std::make_unique<ExtensionLoader>(routeExtensionsPath, di_container_);
    if (extension_loader_->loadExtensions()) {
        extension_loader_->registerRoutes(*server_);
    }
}

void ServerManager::run() {
    setup_routes();
    std::cout << "🦅 Qornix Web Server is running on " << settings_.address << ":" << settings_.port << std::endl;
    // std::cout << "🦅 Qornix Web Server started!" << std::endl;
    std::cout << "🌐 http://localhost:" << settings_.port << std::endl;
    std::cout << "📖 Документация: смотрите README.md" << std::endl;
    std::cout << "🔧 Добавляйте свои обработчики в handlers/ и настраивайте маршруты в routes.h" << std::endl;
    std::cout << "🔧 Для динамических расширений используйте папку route_extensions/" << std::endl;
    if (!g_resolved_config_path.empty()) {
        std::cout << "⚙️ Конфиг: " << g_resolved_config_path << std::endl;
    }
    std::cout << std::endl;

    BOOST_LOG_TRIVIAL(info) << "=== Qornix Web Server starting ===";
    BOOST_LOG_TRIVIAL(info) << "Address: " << settings_.address << ":" << settings_.port;
    BOOST_LOG_TRIVIAL(info) << "URL: http://localhost:" << settings_.port;

    server_->run();
    start_io_threads();
}

// For handling asynchronous operations
void ServerManager::start_io_threads() {
    const int num_threads = std::thread::hardware_concurrency() > 0 ?  //Number of CPU cores
                           std::thread::hardware_concurrency() : 4;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this]() {
            ioc_.run();
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}
