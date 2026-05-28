/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
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
        flat["embedding.models_dir"] = "models/custom";
        flat["embedding.auto_discover_models"] = "false";
        flat["embedding.validate_model_files"] = "false";
        flat["embedding.persistent_cache_enabled"] = "true";
        flat["embedding.chunk_token_margin"] = "3";
        flat["embedding.lowercase_tokens"] = "false";
        flat["vector_store.backend"] = "local_hnsw";
        flat["vector_store.index_path"] = "data/test_hnsw.bin";
        flat["vector_store.metadata_path"] = "data/test_hnsw.meta.json";
        flat["vector_store.endpoint"] = "http://127.0.0.1:6333";
        flat["vector_store.api_key"] = "test-key";
        flat["vector_store.collection"] = "test_collection";
        flat["vector_store.connection_string"] = "postgresql://localhost/test";
        flat["vector_store.table"] = "test_vectors";
        flat["vector_store.distance"] = "Cosine";
        flat["vector_store.upsert_batch_size"] = "128";
        flat["vector_store.recreate"] = "true";
        flat["vector_store.auto_load"] = "true";
        flat["vector_store.auto_save"] = "true";
        flat["search.use_query_expansion"] = "true";
        flat["search.xapian_enabled"] = "true";
        flat["search.xapian_language"] = "ru";
        flat["search.xapian_stemming"] = "true";
        flat["search.xapian_stemming_strategy"] = "all";
        flat["search.xapian_cjk_ngrams"] = "true";
        flat["search.xapian_word_breaks"] = "true";
        flat["search.xapian_spelling"] = "true";
        flat["search.xapian_metadata_prefixes"] = "false";
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
        flat["cache.enabled"] = "true";
        flat["cache.backend"] = "redis";
        flat["cache.max_size"] = "123";
        flat["cache.ttl_seconds"] = "456";
        flat["cache.key_prefix"] = "qornix_test:";
        flat["cache.redis.host"] = "redis.local";
        flat["cache.redis.port"] = "6380";
        flat["cache.redis.db"] = "3";
        flat["cache.redis.password"] = "secret";
        flat["cache.redis.ttl_seconds"] = "789";

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
        assert(config.engine.embedding.models_dir == "models/custom");
        assert(!config.engine.embedding.auto_discover_models);
        assert(!config.engine.embedding.validate_model_files);
        assert(config.engine.embedding.persistent_cache_enabled);
        assert(config.engine.embedding.chunk_token_margin == 3);
        assert(!config.engine.embedding.lowercase_tokens);
        assert(config.engine.vector_store.backend == "local_hnsw");
        assert(config.engine.vector_store.index_path == "data/test_hnsw.bin");
        assert(config.engine.vector_store.metadata_path == "data/test_hnsw.meta.json");
        assert(config.engine.vector_store.endpoint == "http://127.0.0.1:6333");
        assert(config.engine.vector_store.api_key == "test-key");
        assert(config.engine.vector_store.collection == "test_collection");
        assert(config.engine.vector_store.connection_string == "postgresql://localhost/test");
        assert(config.engine.vector_store.table == "test_vectors");
        assert(config.engine.vector_store.distance == "Cosine");
        assert(config.engine.vector_store.upsert_batch_size == 128);
        assert(config.engine.vector_store.recreate);
        assert(config.engine.vector_store.auto_load);
        assert(config.engine.vector_store.auto_save);
        assert(config.engine.search.use_query_expansion);
        assert(config.engine.search.xapian_enabled);
        assert(config.engine.search.xapian_language == "ru");
        assert(config.engine.search.xapian_stemming);
        assert(config.engine.search.xapian_stemming_strategy == "all");
        assert(config.engine.search.xapian_cjk_ngrams);
        assert(config.engine.search.xapian_word_breaks);
        assert(config.engine.search.xapian_spelling);
        assert(!config.engine.search.xapian_metadata_prefixes);
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
        assert(config.cache.enabled);
        assert(config.cache.backend == "redis");
        assert(config.cache.max_size == 123);
        assert(config.cache.ttl.count() == 456);
        assert(config.cache.key_prefix == "qornix_test:");
        assert(config.cache.redis_host == "redis.local");
        assert(config.cache.redis_port == 6380);
        assert(config.cache.redis_db == 3);
        assert(config.cache.redis_password == "secret");
        assert(config.cache.redis_ttl.count() == 789);
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
        const auto fixture = std::filesystem::temp_directory_path() / "qornix_embedding_discovery_fixture";
        std::filesystem::remove_all(fixture);
        std::filesystem::create_directories(fixture / "mini-encoder");
        {
            std::ofstream model(fixture / "mini-encoder" / "model.onnx");
            model << "fixture";
        }
        {
            std::ofstream tokenizer(fixture / "mini-encoder" / "tokenizer.json");
            tokenizer << R"({
                "model": {
                    "type": "BPE",
                    "vocab": {"<pad>":0,"<s>":1,"</s>":2,"<unk>":3,"hello":4,"Ġworld":5},
                    "unk_token": "<unk>"
                },
                "truncation": {"max_length": 128},
                "added_tokens": [{"id": 6, "content": "[CUSTOM]"}]
            })";
        }

        std::unordered_map<std::string, std::string> flat;
        flat["embedding.models_dir"] = fixture.string();
        flat["embedding.auto_discover_models"] = "true";
        flat["embedding.validate_model_files"] = "true";
        flat["embedding.max_seq_len"] = "256";

        auto config = makeRagConfigFromStandaloneFlatMap(flat, "discovery-unit");
        assert(config.engine.embedding_registry.models.size() == 1);
        const auto& model = config.engine.embedding_registry.models.begin()->second;
        assert(model.backend == "onnx");
        assert(model.discovered);
        assert(model.files_present);
        assert(model.tokenizer_type == "BPE");
        assert(model.tokenizer_vocab_size == 7);
        assert(model.max_seq_len == 128);
        assert(model.source.find("auto_discovered:") == 0);
        std::filesystem::remove_all(fixture);
    }

    {
        RagEngine engine;
        auto info = engine.get_embedding_model_info();
        assert(info.backend == "tfidf");
        assert(info.id == "tfidf:d256");
        assert(info.dimension == 256);
        assert(info.ready);
        assert(info.effective_chunk_token_limit == 254);
        assert(info.persistent_cache_enabled);
        assert(!info.model_signature.empty());
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

    {
        RagEngineConfig config;
        config.embedding.active_model_id = "tfidf-a";
        EmbeddingModelDefinition model_a;
        model_a.id = "tfidf-a";
        model_a.backend = "tfidf";
        model_a.name = "Local TF-IDF A";
        model_a.dimension = 256;
        EmbeddingModelDefinition model_b;
        model_b.id = "tfidf-b";
        model_b.backend = "tfidf";
        model_b.name = "Local TF-IDF B";
        model_b.dimension = 256;
        config.embedding_registry.models.emplace(model_a.id, model_a);
        config.embedding_registry.models.emplace(model_b.id, model_b);

        RagEngine engine(config);
        assert(engine.get_embedding_model_info().active_model_id == "tfidf-a");
        assert(engine.switch_active_embedding_model("tfidf-b"));
        auto info = engine.get_embedding_model_info();
        assert(info.backend == "tfidf");
        assert(info.active_model_id == "tfidf-b");
        assert(info.name == "Local TF-IDF B");
        assert(!engine.switch_active_embedding_model("missing-model"));
    }

    {
        std::unordered_map<std::string, std::string> flat;
        flat["search.xapian_language"] = "de";
        auto config = makeRagConfigFromStandaloneFlatMap(flat, "xapian-german-unit");
        assert(config.engine.search.xapian_language == "de");
        assert(config.diagnostics.warnings.empty());
    }

    {
        std::unordered_map<std::string, std::string> flat;
        flat["search.xapian_language"] = "porter";
        auto config = makeRagConfigFromStandaloneFlatMap(flat, "xapian-porter-unit");
        assert(config.engine.search.xapian_language == "porter");
        assert(config.diagnostics.warnings.empty());
    }

    {
        std::unordered_map<std::string, std::string> flat;
        flat["search.xapian_language"] = "klingon";
        flat["search.xapian_stemming_strategy"] = "sometimes";
        auto config = makeRagConfigFromStandaloneFlatMap(flat, "xapian-warning-unit");
        assert(config.engine.search.xapian_language == "auto");
        assert(config.engine.search.xapian_stemming_strategy == "some");
        assert(config.diagnostics.warnings.size() >= 2);
    }

    std::cout << "Embedding config tests passed\n";
    return 0;
}
