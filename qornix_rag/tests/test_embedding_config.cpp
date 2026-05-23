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

    std::cout << "Embedding config tests passed\n";
    return 0;
}
