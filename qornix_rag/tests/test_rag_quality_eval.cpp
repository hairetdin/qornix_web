/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_service.h"

#include <boost/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

#ifndef QORNIX_RAG_TEST_EVAL_DIR
#define QORNIX_RAG_TEST_EVAL_DIR "."
#endif

namespace {

std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    assert(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string json_string(const boost::json::object& object, const char* key) {
    auto it = object.find(key);
    assert(it != object.end());
    assert(it->value().is_string());
    return std::string(it->value().as_string().c_str());
}

size_t json_size(const boost::json::object& object, const char* key, size_t fallback) {
    auto it = object.find(key);
    if (it == object.end() || !it->value().is_int64()) {
        return fallback;
    }
    return static_cast<size_t>(it->value().as_int64());
}

bool contains_text(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

fs::path make_fixture(const boost::json::object& dataset) {
    auto dir = fs::temp_directory_path() / ("qornix_rag_quality_eval_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    const auto& documents = dataset.at("documents").as_array();
    for (const auto& value : documents) {
        const auto& doc = value.as_object();
        const auto relative_path = json_string(doc, "path");
        const auto content = json_string(doc, "content");
        const fs::path output_path = dir / relative_path;
        fs::create_directories(output_path.parent_path());
        std::ofstream out(output_path);
        out << content << "\n";
    }

    return dir;
}

RagEngineConfig eval_config() {
    RagEngineConfig config;
    config.embedding.backend = "tfidf";
    config.embedding.enable_fallback = true;
    config.search.use_hybrid = false;
    config.search.use_query_expansion = true;
    config.search.use_reranking = true;
    config.search.rerank_input_multiplier = 3;
    config.search.min_score_threshold = 0.05f;
    config.vector_store.auto_load = false;
    config.vector_store.auto_save = false;
    return config;
}

void assert_search_case(RagService& service, const boost::json::object& test_case) {
    const auto query = json_string(test_case, "query");
    const auto expected_source = json_string(test_case, "expected_top_source");
    const auto expected_snippet = json_string(test_case, "expected_snippet_contains");
    auto response = service.search(query, json_size(test_case, "top_k", 3));

    assert(response.success);
    assert(response.query_expansion_applied);
    assert(response.reranking_applied);
    assert(!response.expanded_query.empty());
    assert(!response.results.empty());
    assert(response.results.front().source_path == expected_source);
    assert(contains_text(response.results.front().snippet, expected_snippet));
    assert(response.results.front().confidence > 0.0);
}

void assert_ask_case(RagService& service, const boost::json::object& test_case) {
    const auto question = json_string(test_case, "question");
    const auto expected_source = json_string(test_case, "expected_context_source");
    const auto citation_prefix = json_string(test_case, "expected_citation_prefix");
    const auto expected_grounding = json_string(test_case, "expected_grounding_status");
    auto response = service.ask(question, json_size(test_case, "top_k", 3));

    assert(response.success);
    assert(response.query_expansion_applied);
    assert(response.reranking_applied);
    assert(!response.context.empty());
    assert(response.context.front().source_path == expected_source);
    assert(response.context.front().citation_id.rfind(citation_prefix, 0) == 0);
    assert(response.grounding_status == expected_grounding);
    assert(!response.answer_citations.empty());
    assert(response.missing_citations.empty());
    assert(response.llm_status == "unavailable");
    assert(contains_text(response.answer, "LLM"));
}

void assert_qa_case(RagService& service, const boost::json::object& test_case) {
    const auto question = json_string(test_case, "question");
    const auto expected_source_type = json_string(test_case, "expected_source_type");
    const auto citation_prefix = json_string(test_case, "expected_citation_prefix");
    auto response = service.ask(question, json_size(test_case, "top_k", 3));

    assert(response.success);
    assert(!response.context.empty());
    assert(response.context.front().source_type == expected_source_type);
    assert(response.context.front().citation_id.rfind(citation_prefix, 0) == 0);
    assert(!response.context.front().pair_id.empty());
    assert(!response.answer_citations.empty());
    assert(response.missing_citations.empty());
    assert(response.grounding_status == "grounded");
}

void assert_refusal_case(RagService& service, const boost::json::object& test_case) {
    const auto question = json_string(test_case, "question");
    const auto expected_grounding = json_string(test_case, "expected_grounding_status");
    const auto expected_llm_status = json_string(test_case, "expected_llm_status");
    auto response = service.ask(question, json_size(test_case, "top_k", 3));

    assert(response.success);
    assert(response.context.empty());
    assert(response.citations.empty());
    assert(response.answer_citations.empty());
    assert(response.grounding_status == expected_grounding);
    assert(response.llm_status == expected_llm_status);
    assert(contains_text(response.answer, "LLM"));
}

} // namespace

int main() {
    const fs::path dataset_path = fs::path(QORNIX_RAG_TEST_EVAL_DIR) / "retrieval_quality.json";
    const auto parsed = boost::json::parse(read_file(dataset_path));
    const auto& dataset = parsed.as_object();
    const auto fixture = make_fixture(dataset);

    auto engine = std::make_shared<RagEngine>(eval_config());
#if QORNIX_HAS_SQLITE
    auto db_path = fs::temp_directory_path() / ("qornix_rag_quality_eval_" + std::to_string(::getpid()) + ".db");
    fs::remove(db_path);
    SQLiteSource::Config sqlite_config;
    sqlite_config.db_path = db_path.string();
    sqlite_config.source_id = "quality_eval";
    sqlite_config.auto_migrate = true;
    auto sqlite = std::make_shared<SQLiteSource>(sqlite_config);
    assert(sqlite->initialize());
    RagService service(engine, nullptr, sqlite);
#else
    RagService service(engine);
#endif

    auto index = service.indexProject(fixture.string());
    assert(index.success);
    assert(index.stats.indexed_chunks >= dataset.at("documents").as_array().size());

#if QORNIX_HAS_SQLITE
    for (const auto& value : dataset.at("qa_pairs").as_array()) {
        const auto& pair = value.as_object();
        std::string pair_id;
        assert(service.addQaPair(json_string(pair, "question"),
                                 json_string(pair, "answer"),
                                 json_string(pair, "category"),
                                 &pair_id));
        assert(!pair_id.empty());
    }
#endif

    for (const auto& value : dataset.at("cases").as_array()) {
        const auto& test_case = value.as_object();
        const auto type = json_string(test_case, "type");
        if (type == "search") {
            assert_search_case(service, test_case);
        } else if (type == "ask") {
            assert_ask_case(service, test_case);
        } else if (type == "ask_qa") {
#if QORNIX_HAS_SQLITE
            assert_qa_case(service, test_case);
#endif
        } else if (type == "ask_refusal") {
            assert_refusal_case(service, test_case);
        } else {
            assert(false && "unknown evaluation case type");
        }
    }

    fs::remove_all(fixture);
#if QORNIX_HAS_SQLITE
    sqlite->cleanup();
    fs::remove(db_path);
#endif
    std::cout << "RAG quality evaluation tests passed\n";
    return 0;
}
