/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "app_paths.h"
#include "server_manager.h"
#include "routes.h"
#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
#include "rag_config.h"
#include "rag_extension.h"
#endif

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#endif
#include <iostream>

namespace {

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
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
    return (dynamic_api_app_paths::sourceDir() / candidate).lexically_normal().string();
}

RagConfig makeDynamicApiRagConfig(const std::map<std::string, std::string>& flatConfig) {
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
    if (config.scan_path) {
        config.scan_path = resolveAppPath(*config.scan_path);
    }
    config.markdown.directory_path = resolveAppPath(config.markdown.directory_path);
    config.upload.uploads_dir = resolveAppPath(config.upload.uploads_dir);
#if QORNIX_HAS_SQLITE
    config.sqlite.db_path = resolveAppPath(config.sqlite.db_path);
    std::filesystem::create_directories(std::filesystem::path(config.sqlite.db_path).parent_path());
#endif
    if (config.markdown_enabled) {
        std::filesystem::create_directories(config.markdown.directory_path);
    }
    if (config.upload.enabled) {
        std::filesystem::create_directories(config.upload.uploads_dir);
    }
    std::filesystem::create_directories(dynamic_api_app_paths::sourceDir() / "data");
    std::filesystem::create_directories(dynamic_api_app_paths::sourceDir() / "logs");
    return config;
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 0) {
            dynamic_api_app_paths::setExecutablePath(argv[0]);
        }

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
        const auto appRoot = dynamic_api_app_paths::sourceDir();
        std::filesystem::current_path(appRoot);
        setEnv("QORNIX_APP_ROOT", appRoot);
        setEnv("QORNIX_RAG_TEMPLATES_DIR", dynamic_api_app_paths::templatesDir());
#endif

        auto server = create_server_manager(argc, argv);
        server->addRouteFunction(setupRoutes);

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
        auto ragExtension = std::make_shared<RagExtension>();
        ragExtension->configure(makeDynamicApiRagConfig(server->getConfig()));
        ragExtension->initialize(server->getDIContainer());
        server->addRouteFunction([ragExtension, &server](HttpServer& httpServer) {
            ragExtension->registerRoutes(httpServer, server->getDIContainer());
        });
#endif

        server->run();

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
        ragExtension->cleanup();
#endif
    } catch (const std::exception& e) {
        std::cerr << "Qornix dynamic API application error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
