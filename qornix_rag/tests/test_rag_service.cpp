/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_service.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

static fs::path make_fixture() {
    auto dir = fs::temp_directory_path() / ("qornix_rag_service_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "routing.md")
        << "# Routing\n"
        << "RagExtension registers UI routes and API routes with a configurable prefix.\n"
        << "Standalone mode uses / and /api endpoints.\n";
    return dir;
}

int main() {
    auto fixture = make_fixture();

    RagEngineConfig config;
    config.embedding.backend = "tfidf";
    config.embedding.enable_fallback = true;

    auto engine = std::make_shared<RagEngine>(config);
    auto service = std::make_shared<RagService>(engine);

    auto index = service->indexProject(fixture.string());
    assert(index.success);
    assert(index.stats.total_files >= 1);

    auto search = service->search("configurable API prefix", 5);
    assert(search.success);
    assert(!search.results.empty());
    assert(search.results.front().path.find("routing.md") != std::string::npos);

    auto ask = service->ask("How are RAG routes registered?", 5);
    assert(ask.success);
    assert(ask.llm_status == "unavailable");
    assert(ask.answer.find("LLM") != std::string::npos);
    assert(!ask.context.empty());

    auto health = service->health();
    assert(health.status == "ok");
    assert(health.indexed);
    assert(health.files >= 1);

    auto sources = service->sources();
    assert(sources.empty());

    fs::remove_all(fixture);
    std::cout << "RagService reusable core tests passed\n";
    return 0;
}
