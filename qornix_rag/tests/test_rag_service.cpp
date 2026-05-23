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
#if QORNIX_HAS_SQLITE
    auto db_path = fs::temp_directory_path() / ("qornix_rag_service_test_" + std::to_string(::getpid()) + ".db");
    fs::remove(db_path);
    SQLiteSource::Config sqlite_config;
    sqlite_config.db_path = db_path.string();
    sqlite_config.source_id = "service_sqlite";
    sqlite_config.auto_migrate = true;
    auto sqlite = std::make_shared<SQLiteSource>(sqlite_config);
    assert(sqlite->initialize());
    auto service = std::make_shared<RagService>(engine, nullptr, sqlite);
#else
    auto service = std::make_shared<RagService>(engine);
#endif

    auto index = service->indexProject(fixture.string());
    assert(index.success);
    assert(index.stats.total_files >= 1);
#if QORNIX_HAS_SQLITE
    assert(sqlite->countPersistedDocuments("project:" + fixture.string()) >= 1);
    assert(sqlite->countPersistedChunks("project:" + fixture.string()) >= 1);
    assert(sqlite->countPersistedEmbeddings("project:" + fixture.string()) >= 1);
#endif

    auto ingest = service->ingestProject(fixture.string());
    assert(ingest.success);
    assert(!ingest.job.id.empty());
    assert(ingest.job.status == "completed");
    assert(ingest.job.files_seen >= 1);
    assert(ingest.job.documents_imported >= 1);
#if QORNIX_HAS_SQLITE
    auto persisted_job = service->findIngestionJob(ingest.job.id);
    assert(persisted_job.has_value());
    assert(persisted_job->status == "completed");
    auto jobs = service->listIngestionJobs();
    assert(!jobs.empty());
#endif

    auto search = service->search("configurable API prefix", 5);
    assert(search.success);
    assert(!search.results.empty());
    assert(search.results.front().path.find("routing.md") != std::string::npos);
    assert(search.results.front().confidence >= 0.0);

    auto ask = service->ask("How are RAG routes registered?", 5);
    assert(ask.success);
    assert(ask.llm_status == "unavailable");
    assert(ask.answer.find("LLM") != std::string::npos);
    assert(!ask.context.empty());
    assert(!ask.citations.empty());
    assert(!ask.context.front().citation_id.empty());
    assert(!ask.context.front().source_path.empty());
    assert(ask.retrieval_confidence >= 0.0);
    assert(ask.grounding_status != "no_context");

    auto health = service->health();
    assert(health.status == "ok");
    assert(health.indexed);
    assert(health.files >= 1);

    auto sources = service->sources();
    assert(sources.empty());

    fs::remove_all(fixture);
#if QORNIX_HAS_SQLITE
    sqlite->cleanup();
    fs::remove(db_path);
#endif
    std::cout << "RagService reusable core tests passed\n";
    return 0;
}
