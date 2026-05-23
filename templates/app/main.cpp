#include "server_manager.h"
#include "routes.h"

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
#include "app_paths.h"
#include "rag_config.h"
#include "rag_extension.h"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#endif

#include <iostream>

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
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

} // namespace
#endif

int main(int argc, char* argv[]) {
    try {
#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
        const auto appRoot = qornix_app_paths::appRoot();
        std::filesystem::current_path(appRoot);
        setEnv("QORNIX_APP_ROOT", appRoot);
        setEnv("QORNIX_RAG_TEMPLATES_DIR", qornix_app_paths::templatesDir());
#endif

        auto server = create_server_manager(argc, argv);
        server->addRouteFunction(setupRoutes);

#ifdef @PROJECT_NAME_UPPER@_ENABLE_RAG
        auto ragExtension = std::make_shared<RagExtension>();
        ragExtension->configure(makeApplicationRagConfig(server->getConfig()));
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
        std::cerr << "Application error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
