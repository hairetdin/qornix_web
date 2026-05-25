/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_service.h"

#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
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
    auto vector_index_path = fs::temp_directory_path() / ("qornix_rag_service_hnsw_" + std::to_string(::getpid()) + ".bin");
    auto vector_metadata_path = fs::temp_directory_path() / ("qornix_rag_service_hnsw_" + std::to_string(::getpid()) + ".meta.json");
    fs::remove(vector_index_path);
    fs::remove(vector_metadata_path);
    config.vector_store.index_path = vector_index_path.string();
    config.vector_store.metadata_path = vector_metadata_path.string();
    config.vector_store.auto_load = true;
    config.vector_store.auto_save = true;

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
    assert(index.stats.indexed_chunks >= 1);
    assert(index.stats.generated_embeddings >= 1);
    assert(index.stats.reused_embeddings == 0);
    assert(fs::exists(vector_index_path));
    assert(fs::exists(vector_metadata_path));

    auto unchanged = service->indexProject(fixture.string());
    assert(unchanged.success);
    assert(unchanged.stats.indexed_chunks == index.stats.indexed_chunks);
    assert(unchanged.stats.reused_embeddings == unchanged.stats.indexed_chunks);
    assert(unchanged.stats.generated_embeddings == 0);

    auto loaded_engine = std::make_shared<RagEngine>(config);
    loaded_engine->index_project(fixture.string());
    assert(loaded_engine->get_vector_store_status().find("loaded:") != std::string::npos);

    std::ofstream(fixture / "new_doc.md") << "# New doc\nThis changes the vector snapshot.\n";
    auto changed = service->indexProject(fixture.string());
    assert(changed.success);
    assert(changed.stats.indexed_chunks > unchanged.stats.indexed_chunks);
    assert(changed.stats.reused_embeddings >= unchanged.stats.reused_embeddings);
    assert(changed.stats.generated_embeddings >= 1);
    assert(changed.stats.stale_embeddings == 0);

    auto stale_engine = std::make_shared<RagEngine>(config);
    stale_engine->index_project(fixture.string());
    assert(stale_engine->get_vector_store_status().find("loaded:") == std::string::npos);
#if QORNIX_HAS_SQLITE
    assert(sqlite->countPersistedDocuments("project:" + fixture.string()) >= 1);
    assert(sqlite->countPersistedChunks("project:" + fixture.string()) >= 1);
    assert(sqlite->countPersistedEmbeddings("project:" + fixture.string()) >= 1);

    std::string qa1;
    std::string qa2;
    std::string qa3;
    assert(service->addQaPair("How do I configure Ollama?", "Set the Ollama model in config.", "llm", &qa1));
    assert(service->addQaPair("How do I reindex a project?", "Use the index or ingest endpoint.", "operations", &qa2));
    assert(service->addQaPair("How do I inspect ingestion jobs?", "Open the Admin tab or call ingest jobs.", "operations", &qa3));

    RagServiceQaListOptions qa_options;
    qa_options.query = "How";
    qa_options.category = "operations";
    qa_options.limit = 1;
    qa_options.offset = 0;
    auto qa_page1 = service->listQaPairs(qa_options);
    assert(qa_page1.success);
    assert(qa_page1.total == 2);
    assert(qa_page1.items.size() == 1);
    assert(qa_page1.has_more);

    qa_options.offset = 1;
    auto qa_page2 = service->listQaPairs(qa_options);
    assert(qa_page2.total == 2);
    assert(qa_page2.items.size() == 1);
    assert(!qa_page2.has_more);
    assert(qa_page2.items.front().id != qa_page1.items.front().id);

    auto qa_suggestions = service->suggestQaPairs("ingestion", 5);
    assert(!qa_suggestions.empty());
    assert(qa_suggestions.front().question.find("ingestion") != std::string::npos);

    auto qa_categories = service->listQaCategories("", 10);
    assert(std::find(qa_categories.begin(), qa_categories.end(), "llm") != qa_categories.end());
    assert(std::find(qa_categories.begin(), qa_categories.end(), "operations") != qa_categories.end());
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

    auto background = service->startBackgroundIngestProject(fixture.string());
    assert(background.success);
    assert(background.job.background);
    assert(background.job.status == "queued" || background.job.status == "running");
    bool saw_background_job = false;
    bool saw_terminal_status = false;
    for (int i = 0; i < 50; ++i) {
        auto found = service->findIngestionJob(background.job.id);
        if (found) {
            saw_background_job = true;
            assert(found->progress_percent >= 0);
            assert(found->progress_percent <= 100);
            if (found->status == "completed" || found->status == "failed") {
                saw_terminal_status = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(saw_background_job);
    assert(saw_terminal_status);

    auto search = service->search("configurable API prefix", 5);
    assert(search.success);
    assert(search.query_expansion_applied);
    assert(search.reranking_applied);
    assert(search.expanded_query.find("configurable") != std::string::npos);
    assert(!search.results.empty());
    assert(search.results.front().path.find("routing.md") != std::string::npos);
    assert(search.results.front().confidence >= 0.0);

    auto ask = service->ask("How are RAG routes registered?", 5);
    assert(ask.success);
    assert(ask.query_expansion_applied);
    assert(ask.reranking_applied);
    assert(ask.expanded_query.find("routes") != std::string::npos);
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
    fs::remove(vector_index_path);
    fs::remove(vector_metadata_path);
#if QORNIX_HAS_SQLITE
    sqlite->cleanup();
    fs::remove(db_path);
#endif
    std::cout << "RagService reusable core tests passed\n";
    return 0;
}
