#include "app_paths.h"
#include "rag_config.h"
#include "rag_extension.h"
#include "server_manager.h"

#if defined(@PROJECT_NAME_UPPER@_ENABLE_AUTH) && defined(@PROJECT_NAME_UPPER@_ENABLE_ORM)
#include "auth_manager.h"
#include "auth_middleware.h"
#include "auth_orm_store.h"
#include "auth_routes.h"
#include "database_interface.h"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

void setEnv(const std::string& name, const std::filesystem::path& value) {
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.string().c_str());
#else
    setenv(name.c_str(), value.string().c_str(), 1);
#endif
}

std::string resolveAppPath(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::filesystem::path candidate(path);
    if (candidate.is_absolute()) {
        return candidate.lexically_normal().string();
    }

    return (qornix_app_paths::appRoot() / candidate).lexically_normal().string();
}

RagConfig makeApplicationRagConfig(const std::map<std::string, std::string>& flatConfig) {
    RagConfig config = makeRagConfigFromIntegratedFlatMap(flatConfig, "application config.yaml");

    for (auto& [id, model] : config.engine.embedding_registry.models) {
        (void)id;
        model.model_path = resolveAppPath(model.model_path);
        model.tokenizer_path = resolveAppPath(model.tokenizer_path);
    }
    if (!config.engine.embedding.active_model_id.empty()) {
        auto active = config.engine.embedding_registry.models.find(config.engine.embedding.active_model_id);
        if (active != config.engine.embedding_registry.models.end()) {
            apply_embedding_model_definition(config.engine.embedding, active->second);
        }
    }
    config.engine.embedding.model_path = resolveAppPath(config.engine.embedding.model_path);
    config.engine.embedding.tokenizer_path = resolveAppPath(config.engine.embedding.tokenizer_path);
    config.markdown.directory_path = resolveAppPath(config.markdown.directory_path);

#if QORNIX_HAS_SQLITE
    config.sqlite.db_path = resolveAppPath(config.sqlite.db_path);
    std::filesystem::create_directories(std::filesystem::path(config.sqlite.db_path).parent_path());
#endif

    if (config.markdown_enabled) {
        std::filesystem::create_directories(config.markdown.directory_path);
    }
    std::filesystem::create_directories(qornix_app_paths::appRoot() / "data");
    std::filesystem::create_directories(qornix_app_paths::appRoot() / "logs");

    return config;
}

#if defined(@PROJECT_NAME_UPPER@_ENABLE_AUTH) && defined(@PROJECT_NAME_UPPER@_ENABLE_ORM)
std::string configValue(const std::map<std::string, std::string>& flatConfig,
                        const std::string& key,
                        const std::string& fallback = "") {
    auto it = flatConfig.find(key);
    return it == flatConfig.end() || it->second.empty() ? fallback : it->second;
}

bool configBool(const std::map<std::string, std::string>& flatConfig,
                const std::string& key,
                bool fallback = false) {
    std::string value = configValue(flatConfig, key, fallback ? "true" : "false");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::vector<std::string> configList(const std::map<std::string, std::string>& flatConfig,
                                    const std::string& key,
                                    std::vector<std::string> fallback = {}) {
    std::string value = configValue(flatConfig, key);
    if (value.empty()) {
        return fallback;
    }

    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char c) {
            return !std::isspace(c);
        }));
        item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), item.end());
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

std::chrono::minutes configMinutes(const std::map<std::string, std::string>& flatConfig,
                                   const std::string& key,
                                   int fallback) {
    try {
        return std::chrono::minutes(std::stoi(configValue(flatConfig, key, std::to_string(fallback))));
    } catch (...) {
        return std::chrono::minutes(fallback);
    }
}

qornix_auth::AuthMode authModeFromConfig(const std::map<std::string, std::string>& flatConfig) {
    const std::string mode = configValue(flatConfig, "auth.mode", "session");
    if (mode == "jwt") {
        return qornix_auth::AuthMode::JWT;
    }
    if (mode == "both") {
        return qornix_auth::AuthMode::BOTH;
    }
    return qornix_auth::AuthMode::SESSION;
}

DatabaseConfig makeAuthDatabaseConfig(const std::map<std::string, std::string>& flatConfig) {
    DatabaseConfig config;
    config.driver = configValue(flatConfig, "auth.database.driver", "sqlite");
    config.connectionString = configValue(flatConfig, "auth.database.connection_string");
    config.dbname = configValue(flatConfig, "auth.database.name");
    config.host = configValue(flatConfig, "auth.database.host");
    config.user = configValue(flatConfig, "auth.database.user");
    config.password = configValue(flatConfig, "auth.database.password");

    try {
        config.port = std::stoi(configValue(flatConfig, "auth.database.port", "0"));
    } catch (...) {
        config.port = 0;
    }

    if (config.driver == "sqlite" || config.driver == "sqlite3") {
        const std::string path = resolveAppPath(configValue(flatConfig, "auth.database.path", "data/auth.db"));
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        config.dbname = path;
        config.connectionString = path;
    }

    return config;
}

std::shared_ptr<qornix_auth::AuthManager> makeApplicationAuthManager(
    const std::map<std::string, std::string>& flatConfig) {
    if (!configBool(flatConfig, "auth.enabled", false)) {
        return nullptr;
    }

    qornix_auth::AuthConfig authConfig;
    authConfig.mode = authModeFromConfig(flatConfig);
    authConfig.sessionDuration = configMinutes(flatConfig, "auth.session_duration_minutes", 30);
    authConfig.jwtDuration = configMinutes(flatConfig, "auth.jwt_duration_minutes", 60);
    authConfig.jwtSecret = configValue(flatConfig, "auth.jwt_secret", "change-this-secret-key-in-production");
    authConfig.jwtIssuer = configValue(flatConfig, "auth.jwt_issuer", "qornix-auth");
    try {
        authConfig.minPasswordLength = static_cast<std::size_t>(
            std::stoul(configValue(flatConfig, "auth.min_password_length", "12")));
    } catch (...) {
        authConfig.minPasswordLength = 12;
    }
    authConfig.defaultRoles = configList(flatConfig, "auth.default_roles", {"user"});
    authConfig.defaultPermissions = configList(flatConfig, "auth.default_permissions", {"rag:read"});

    auto db = DatabaseInterface::init(makeAuthDatabaseConfig(flatConfig));
    auto sharedDb = std::shared_ptr<DatabaseInterface>(std::move(db));
    auto store = qornix_auth::QornixOrmAuthStore::create(sharedDb, configBool(flatConfig, "auth.auto_migrate", true));
    auto manager = std::make_shared<qornix_auth::AuthManager>(authConfig, store);

    if (configBool(flatConfig, "auth.bootstrap_admin.enabled", false)) {
        const std::string username = configValue(flatConfig, "auth.bootstrap_admin.username", "admin");
        std::string password = configValue(flatConfig, "auth.bootstrap_admin.password");
        if (password.empty()) {
            const std::string envName = configValue(flatConfig, "auth.bootstrap_admin.password_env", "QORNIX_ADMIN_PASSWORD");
            if (const char* envValue = std::getenv(envName.c_str())) {
                password = envValue;
            }
        }
        if (!password.empty() && !store->findUserByUsername(username).has_value()) {
            manager->registerUser(
                username,
                password,
                configValue(flatConfig, "auth.bootstrap_admin.email"),
                configList(flatConfig, "auth.bootstrap_admin.roles", {"admin"}),
                configList(flatConfig, "auth.bootstrap_admin.permissions", {"rag:read", "rag:write", "rag:admin", "auth:admin"}));
        }
    }

    return manager;
}

void setupApplicationAuth(HttpServer& httpServer,
                          const std::shared_ptr<qornix_auth::AuthManager>& authManager,
                          const std::map<std::string, std::string>& flatConfig) {
    if (!authManager) {
        return;
    }

    setupAuthRoutes(
        httpServer,
        authManager,
        configBool(flatConfig, "auth.registration_enabled", false),
        configBool(flatConfig, "auth.secure_cookies", false));

    auto middleware = create_auth_middleware(
        authManager,
        true,
        configList(flatConfig, "auth.exclude_paths", {
            "/auth/login", "/auth/register", "/health", "/static"
        }),
        configBool(flatConfig, "auth.logging", false));
    middleware->setCsrfProtectionEnabled(configBool(flatConfig, "auth.csrf.enabled", true));
    middleware->setCsrfHeaderName(configValue(flatConfig, "auth.csrf.header", "X-CSRF-Token"));
    middleware->requireAnyRole(configList(flatConfig, "auth.required_roles"));
    middleware->requireAnyPermission(configList(flatConfig, "auth.required_permissions"));

    const auto readPermission = configList(flatConfig, "auth.rag_read_permissions", {"rag:read"});
    const auto writePermission = configList(flatConfig, "auth.rag_write_permissions", {"rag:write"});
    const auto adminPermission = configList(flatConfig, "auth.rag_admin_permissions", {"rag:admin"});
    const auto authAdminPermission = configList(flatConfig, "auth.admin_permissions", {"auth:admin"});
    middleware->addRoutePolicy("*", "/auth/users", authAdminPermission);
    middleware->addRoutePolicy("GET", "/rag", readPermission, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/health", readPermission, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/sources", readPermission, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/stats", readPermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/search", readPermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/ask", readPermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/batch", readPermission, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/metrics", adminPermission, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/admin", adminPermission);
    middleware->addRoutePolicy("*", "/api/rag/analytics", adminPermission);
    middleware->addRoutePolicy("*", "/api/rag/import", writePermission);
    middleware->addRoutePolicy("*", "/api/rag/ingest", writePermission);
    middleware->addRoutePolicy("POST", "/api/rag/index", writePermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/documents/delete", writePermission, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/qa", readPermission);
    middleware->addRoutePolicy("POST", "/api/rag/qa/dedup", writePermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/qa/add", writePermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/qa/update", writePermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/qa/delete", writePermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/qa", writePermission, {}, true);
    middleware->addRoutePolicy("PUT", "/api/rag/qa", writePermission);
    middleware->addRoutePolicy("DELETE", "/api/rag/qa", writePermission);
    middleware->addRoutePolicy("POST", "/api/rag/sources/add", writePermission, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/sources/remove", writePermission, {}, true);

    httpServer.add_middleware(middleware);
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto appRoot = qornix_app_paths::appRoot();
        std::filesystem::current_path(appRoot);
        setEnv("QORNIX_APP_ROOT", appRoot);
        setEnv("QORNIX_RAG_TEMPLATES_DIR", qornix_app_paths::templatesDir());

        auto server = create_server_manager(argc, argv);

#if defined(@PROJECT_NAME_UPPER@_ENABLE_AUTH) && defined(@PROJECT_NAME_UPPER@_ENABLE_ORM)
        auto authManager = makeApplicationAuthManager(server->getConfig());
        server->addRouteFunction([authManager, &server](HttpServer& httpServer) {
            setupApplicationAuth(httpServer, authManager, server->getConfig());
        });
#endif

        auto ragExtension = std::make_shared<RagExtension>();

        ragExtension->configure(makeApplicationRagConfig(server->getConfig()));
        ragExtension->initialize(server->getDIContainer());

        server->addRouteFunction([ragExtension, &server](HttpServer& httpServer) {
            ragExtension->registerRoutes(httpServer, server->getDIContainer());
        });

        server->run();
        ragExtension->cleanup();
    } catch (const std::exception& e) {
        std::cerr << "RAG application error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
