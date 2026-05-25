/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_config.h"

#include <cassert>
#include <iostream>
#include <unordered_map>

int main() {
    {
        std::unordered_map<std::string, std::string> flat;
        flat["embedding.backend"] = "onnx";
        flat["embedding.model_id"] = "bge-small-local-v1";
        flat["embedding.model_name"] = "BGE small local";
        flat["embedding.model_version"] = "2026-05";
        flat["embedding.model_path"] = "models/bge-small.onnx";
        flat["embedding.tokenizer_path"] = "models/tokenizer.json";
        flat["embedding.tokenizer_type"] = "WordPiece";
        flat["embedding.pooling"] = "cls";
        flat["embedding.dimension"] = "384";
        flat["embedding.max_seq_len"] = "512";
        flat["embedding.onnx_threads"] = "4";
        flat["embedding.lowercase_tokens"] = "false";
        flat["vector_store.backend"] = "local_hnsw";
        flat["vector_store.index_path"] = "data/test_hnsw.bin";
        flat["vector_store.metadata_path"] = "data/test_hnsw.meta.json";
        flat["vector_store.auto_load"] = "true";
        flat["vector_store.auto_save"] = "true";
        flat["search.use_query_expansion"] = "true";
        flat["search.use_reranking"] = "true";
        flat["search.rerank_input_multiplier"] = "4";
        flat["search.rerank_path_boost"] = "0.25";
        flat["search.rerank_metadata_boost"] = "0.15";
        flat["search.rerank_exact_content_boost"] = "0.07";
        flat["rag.security.enabled"] = "true";
        flat["rag.security.mode"] = "host_header";
        flat["rag.security.role_header"] = "X-App-Role";
        flat["rag.security.admin_role"] = "rag_admin";
        flat["rag.security.protect_admin_routes"] = "true";
        flat["rag.security.protect_write_routes"] = "false";

        auto config = makeRagConfigFromStandaloneFlatMap(flat, "unit");
        assert(config.engine.embedding.backend == "onnx");
        assert(config.engine.embedding.model_id == "bge-small-local-v1");
        assert(config.engine.embedding.model_name == "BGE small local");
        assert(config.engine.embedding.model_version == "2026-05");
        assert(config.engine.embedding.tokenizer_type == "WordPiece");
        assert(config.engine.embedding.pooling == "cls");
        assert(config.engine.embedding.dimension == 384);
        assert(config.engine.embedding.max_seq_len == 512);
        assert(config.engine.embedding.onnx_threads == 4);
        assert(!config.engine.embedding.lowercase_tokens);
        assert(config.engine.vector_store.backend == "local_hnsw");
        assert(config.engine.vector_store.index_path == "data/test_hnsw.bin");
        assert(config.engine.vector_store.metadata_path == "data/test_hnsw.meta.json");
        assert(config.engine.vector_store.auto_load);
        assert(config.engine.vector_store.auto_save);
        assert(config.engine.search.use_query_expansion);
        assert(config.engine.search.use_reranking);
        assert(config.engine.search.rerank_input_multiplier == 4);
        assert(config.engine.search.rerank_path_boost > 0.24f);
        assert(config.engine.search.rerank_metadata_boost > 0.14f);
        assert(config.engine.search.rerank_exact_content_boost > 0.06f);
        assert(config.security.enabled);
        assert(config.security.mode == "host_header");
        assert(config.security.role_header == "X-App-Role");
        assert(config.security.admin_role == "rag_admin");
        assert(config.security.protect_admin_routes);
        assert(!config.security.protect_write_routes);
    }

    {
        std::unordered_map<std::string, std::string> flat;
        flat["embedding.active_model_id"] = "bge-small";
        flat["embedding.registry.bge-small.backend"] = "onnx";
        flat["embedding.registry.bge-small.name"] = "BGE small registry";
        flat["embedding.registry.bge-small.version"] = "2026-05";
        flat["embedding.registry.bge-small.model_path"] = "models/bge-small.onnx";
        flat["embedding.registry.bge-small.tokenizer_path"] = "models/bge-tokenizer.json";
        flat["embedding.registry.bge-small.tokenizer_type"] = "WordPiece";
        flat["embedding.registry.bge-small.pooling"] = "mean";
        flat["embedding.registry.bge-small.dimension"] = "384";
        flat["embedding.registry.bge-small.max_seq_len"] = "512";
        flat["embedding.registry.bge-small.onnx_threads"] = "2";
        flat["embedding.registry.bge-small.license"] = "MIT";
        flat["embedding.registry.bge-small.source"] = "local";
        flat["embedding.registry.tfidf-local.backend"] = "tfidf";
        flat["embedding.registry.tfidf-local.name"] = "Local TF-IDF";
        flat["embedding.registry.tfidf-local.dimension"] = "256";

        auto config = makeRagConfigFromStandaloneFlatMap(flat, "registry-unit");
        assert(config.engine.embedding_registry.models.size() == 2);
        assert(config.engine.embedding.active_model_id == "bge-small");
        assert(config.engine.embedding.model_id == "bge-small");
        assert(config.engine.embedding.backend == "onnx");
        assert(config.engine.embedding.model_name == "BGE small registry");
        assert(config.engine.embedding.model_version == "2026-05");
        assert(config.engine.embedding.model_path == "models/bge-small.onnx");
        assert(config.engine.embedding.tokenizer_path == "models/bge-tokenizer.json");
        assert(config.engine.embedding.dimension == 384);
        assert(config.engine.embedding.max_seq_len == 512);
        assert(config.engine.embedding.onnx_threads == 2);
        assert(config.engine.embedding_registry.models.at("bge-small").license == "MIT");
        assert(config.engine.embedding_registry.models.at("bge-small").source == "local");
        assert(config.diagnostics.warnings.empty());
    }

    {
        std::unordered_map<std::string, std::string> flat;
        flat["embedding.active_model_id"] = "missing";
        flat["embedding.registry.present.backend"] = "tfidf";

        auto config = makeRagConfigFromStandaloneFlatMap(flat, "registry-warning-unit");
        assert(config.engine.embedding_registry.models.size() == 1);
        assert(!config.diagnostics.warnings.empty());
        assert(config.diagnostics.warnings.front().find("active_model_id") != std::string::npos);
        assert(!config.engine.embedding_registry.warnings.empty());
    }

    {
        RagEngine engine;
        auto info = engine.get_embedding_model_info();
        assert(info.backend == "tfidf");
        assert(info.id == "tfidf:d256");
        assert(info.dimension == 256);
        assert(info.ready);
        assert(engine.get_embedding_cache_namespace() == "tfidf:d256");
    }

    {
        RagEngineConfig config;
        config.embedding.backend = "onnx";
        config.embedding.model_id = "custom-onnx";
        config.embedding.model_path = "/missing/model.onnx";
        config.embedding.tokenizer_path = "/missing/tokenizer.json";
        config.embedding.dimension = 384;
        config.embedding.enable_fallback = true;

        RagEngine engine(config);
        auto info = engine.get_embedding_model_info();
        assert(info.backend == "tfidf");
        assert(info.id == "tfidf:d256");
        assert(info.dimension == 256);
        assert(!info.ready);
        assert(engine.get_embedding_dim() == 256);
    }

    {
        RagEngineConfig config;
        config.embedding.active_model_id = "tfidf-local";
        EmbeddingModelDefinition model;
        model.id = "tfidf-local";
        model.backend = "tfidf";
        model.name = "Local TF-IDF registry";
        model.dimension = 256;
        config.embedding_registry.models.emplace(model.id, model);

        RagEngine engine(config);
        auto info = engine.get_embedding_model_info();
        assert(info.backend == "tfidf");
        assert(info.active_model_id == "tfidf-local");
        assert(info.registry_size == 1);
        assert(info.registry_model_ids.size() == 1);
        assert(info.registry_model_ids.front() == "tfidf-local");
        assert(info.name == "Local TF-IDF registry");
    }

    std::cout << "Embedding config tests passed\n";
    return 0;
}
