/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>

#include <xapian.h>

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parseBool(const std::string& value, bool fallback) {
    const auto normalized = lower(value);
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") return true;
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") return false;
    return fallback;
}

int parseInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

size_t parseSize(const std::string& value, size_t fallback) {
    try {
        return static_cast<size_t>(std::stoull(value));
    } catch (...) {
        return fallback;
    }
}

float parseFloat(const std::string& value, float fallback) {
    try {
        return std::stof(value);
    } catch (...) {
        return fallback;
    }
}

bool isSupportedEmbeddingBackend(const std::string& backend) {
    return backend == "tfidf" || backend == "onnx";
}

bool isSupportedPooling(const std::string& pooling) {
    return pooling == "mean" || pooling == "cls";
}

bool isSupportedXapianLanguage(const std::string& language) {
    const auto value = lower(language);
    if (value.empty() || value == "default" || value == "auto" || value == "detect" ||
        value == "none" || value == "off" || value == "disabled" || value == "false") {
        return true;
    }

    // Delegate real stemmer-name validation to the installed Xapian package
    // instead of maintaining a partial qornix-side list. Xapian::Stem accepts
    // documented language names and aliases such as "english"/"en" and throws
    // Xapian::InvalidArgumentError when a stemmer is unavailable in this build.
    try {
        Xapian::Stem stemmer(value);
        (void)stemmer;
        return true;
    } catch (const Xapian::InvalidArgumentError&) {
        return false;
    }
}

bool isSupportedXapianStemmingStrategy(const std::string& strategy) {
    const auto value = lower(strategy);
    return value == "none" || value == "off" || value == "false" ||
           value == "some" || value == "all";
}

void addWarning(RagConfig& config, const std::string& warning) {
    config.diagnostics.warnings.push_back(warning);
}

std::string normalizePathPrefix(std::string prefix, const std::string& fallback) {
    if (prefix.empty()) {
        prefix = fallback;
    }
    if (prefix.front() != '/') {
        prefix.insert(prefix.begin(), '/');
    }
    while (prefix.size() > 1 && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix;
}

template <typename Map>
std::string getString(const Map& values,
                      const std::vector<std::string>& keys,
                      const std::string& fallback) {
    for (const auto& key : keys) {
        auto it = values.find(key);
        if (it != values.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return fallback;
}

template <typename Map>
bool getBool(const Map& values,
             const std::vector<std::string>& keys,
             bool fallback) {
    for (const auto& key : keys) {
        auto it = values.find(key);
        if (it != values.end()) {
            return parseBool(it->second, fallback);
        }
    }
    return fallback;
}

template <typename Map>
int getInt(const Map& values,
           const std::vector<std::string>& keys,
           int fallback) {
    for (const auto& key : keys) {
        auto it = values.find(key);
        if (it != values.end()) {
            return parseInt(it->second, fallback);
        }
    }
    return fallback;
}

template <typename Map>
size_t getSize(const Map& values,
               const std::vector<std::string>& keys,
               size_t fallback) {
    for (const auto& key : keys) {
        auto it = values.find(key);
        if (it != values.end()) {
            return parseSize(it->second, fallback);
        }
    }
    return fallback;
}

template <typename Map>
float getFloat(const Map& values,
               const std::vector<std::string>& keys,
               float fallback) {
    for (const auto& key : keys) {
        auto it = values.find(key);
        if (it != values.end()) {
            return parseFloat(it->second, fallback);
        }
    }
    return fallback;
}

template <typename Map>
std::vector<std::string> collectEmbeddingRegistryIds(const Map& values) {
    std::set<std::string> ids;
    const std::vector<std::string> prefixes = {
        "embedding.registry.",
        "rag.embedding.registry."
    };

    for (const auto& [key, value] : values) {
        (void)value;
        for (const auto& prefix : prefixes) {
            if (key.rfind(prefix, 0) != 0) {
                continue;
            }
            const auto rest = key.substr(prefix.size());
            const auto dot = rest.find('.');
            if (dot != std::string::npos && dot > 0) {
                ids.insert(rest.substr(0, dot));
            }
        }
    }

    return {ids.begin(), ids.end()};
}

template <typename Map>
std::string getRegistryString(const Map& values,
                              const std::string& id,
                              const std::vector<std::string>& fields,
                              const std::string& fallback) {
    std::vector<std::string> keys;
    keys.reserve(fields.size() * 2);
    for (const auto& field : fields) {
        keys.push_back("embedding.registry." + id + "." + field);
        keys.push_back("rag.embedding.registry." + id + "." + field);
    }
    return getString(values, keys, fallback);
}

template <typename Map>
bool getRegistryBool(const Map& values,
                     const std::string& id,
                     const std::string& field,
                     bool fallback) {
    return getBool(values, {
        "embedding.registry." + id + "." + field,
        "rag.embedding.registry." + id + "." + field
    }, fallback);
}

template <typename Map>
size_t getRegistrySize(const Map& values,
                       const std::string& id,
                       const std::string& field,
                       size_t fallback) {
    return getSize(values, {
        "embedding.registry." + id + "." + field,
        "rag.embedding.registry." + id + "." + field
    }, fallback);
}

std::string sanitizeModelId(std::string value) {
    if (value.empty()) {
        value = "local-model";
    }
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? "local-model" : out;
}

struct TokenizerProbeResult {
    bool ok = false;
    std::string type;
    size_t vocab_size = 0;
    size_t max_seq_len = 0;
    std::string status;
};

size_t jsonVocabSize(const boost::json::value& value) {
    if (value.is_object()) {
        return value.as_object().size();
    }
    if (value.is_array()) {
        return value.as_array().size();
    }
    return 0;
}

TokenizerProbeResult probeTokenizerJson(const std::string& tokenizer_path) {
    TokenizerProbeResult result;
    if (tokenizer_path.empty()) {
        result.status = "tokenizer_path is empty";
        return result;
    }
    std::ifstream in(tokenizer_path);
    if (!in.is_open()) {
        result.status = "tokenizer.json is missing: " + tokenizer_path;
        return result;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
        auto parsed = boost::json::parse(buffer.str());
        if (!parsed.is_object()) {
            result.status = "tokenizer root is not an object";
            return result;
        }
        const auto& root = parsed.as_object();
        auto model_it = root.find("model");
        if (model_it == root.end() || !model_it->value().is_object()) {
            result.status = "tokenizer model object is missing";
            return result;
        }
        const auto& model = model_it->value().as_object();
        auto type_it = model.find("type");
        if (type_it != model.end() && type_it->value().is_string()) {
            result.type = std::string(type_it->value().as_string().c_str());
        }
        auto vocab_it = model.find("vocab");
        if (vocab_it != model.end()) {
            result.vocab_size = jsonVocabSize(vocab_it->value());
        }
        auto root_vocab = root.find("vocab");
        if (result.vocab_size == 0 && root_vocab != root.end()) {
            result.vocab_size = jsonVocabSize(root_vocab->value());
        }
        auto added = root.find("added_tokens");
        if (added != root.end() && added->value().is_array()) {
            result.vocab_size += added->value().as_array().size();
        }
        auto trunc = root.find("truncation");
        if (trunc != root.end() && trunc->value().is_object()) {
            const auto& truncation = trunc->value().as_object();
            auto max_it = truncation.find("max_length");
            if (max_it != truncation.end() && max_it->value().is_int64() && max_it->value().as_int64() > 0) {
                result.max_seq_len = static_cast<size_t>(max_it->value().as_int64());
            }
        }
        result.ok = result.vocab_size > 0;
        result.status = result.ok
            ? "tokenizer compatible: type=" + (result.type.empty() ? "unknown" : result.type) +
              ", vocab=" + std::to_string(result.vocab_size)
            : "tokenizer has no supported vocab entries";
    } catch (const std::exception& e) {
        result.status = std::string("tokenizer parse failed: ") + e.what();
    }
    return result;
}

void validateEmbeddingModel(RagConfig& config, EmbeddingModelDefinition& model) {
    if (!isSupportedEmbeddingBackend(model.backend)) {
        addWarning(config, "embedding.registry." + model.id + ".backend unsupported: " + model.backend);
    }
    if (!isSupportedPooling(model.pooling)) {
        addWarning(config, "embedding.registry." + model.id + ".pooling unsupported: " + model.pooling);
    }
    if (model.backend == "onnx") {
        if (model.model_path.empty()) {
            addWarning(config, "embedding.registry." + model.id + ".model_path is required for onnx backend");
        } else if (config.engine.embedding.validate_model_files && !std::filesystem::exists(model.model_path)) {
            addWarning(config, "embedding.registry." + model.id + ".model_path not found: " + model.model_path);
            model.model_status = "missing model file";
        } else {
            model.model_status = "model file present";
        }
        if (model.tokenizer_path.empty()) {
            addWarning(config, "embedding.registry." + model.id + ".tokenizer_path is required for onnx backend");
        } else if (config.engine.embedding.validate_model_files) {
            auto probe = probeTokenizerJson(model.tokenizer_path);
            model.tokenizer_status = probe.status;
            model.tokenizer_vocab_size = probe.vocab_size;
            if (!probe.type.empty() && (model.tokenizer_type.empty() || model.tokenizer_type == "basic_wordpiece")) {
                model.tokenizer_type = probe.type;
            }
            if (probe.max_seq_len > 0 && model.max_seq_len > probe.max_seq_len) {
                model.max_seq_len = probe.max_seq_len;
            }
            if (!probe.ok) {
                addWarning(config, "embedding.registry." + model.id + ".tokenizer incompatible: " + probe.status);
            }
        }
        model.files_present = !model.model_path.empty() && !model.tokenizer_path.empty()
            && std::filesystem::exists(model.model_path)
            && std::filesystem::exists(model.tokenizer_path);
    } else {
        model.files_present = true;
        model.model_status = "tfidf backend does not require model files";
        model.tokenizer_status = "tfidf backend does not require tokenizer.json";
    }
}

void discoverLocalEmbeddingModels(RagConfig& config, EmbeddingModelRegistry& registry) {
    if (!config.engine.embedding.auto_discover_models) {
        return;
    }
    const std::filesystem::path models_dir(config.engine.embedding.models_dir);
    if (config.engine.embedding.models_dir.empty() || !std::filesystem::exists(models_dir)) {
        return;
    }

    size_t discovered = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(models_dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) {
            continue;
        }
        const auto path = it->path();
        if (path.extension() != ".onnx") {
            continue;
        }
        const auto dir = path.parent_path();
        std::filesystem::path tokenizer = dir / "tokenizer.json";
        if (!std::filesystem::exists(tokenizer)) {
            continue;
        }
        std::string id = "local-" + sanitizeModelId(dir.filename().string().empty() ? path.stem().string() : dir.filename().string());
        if (registry.models.find(id) != registry.models.end()) {
            id += "-" + sanitizeModelId(path.stem().string());
        }
        if (registry.models.find(id) != registry.models.end()) {
            addWarning(config, "embedding auto discovery skipped duplicate model id: " + id);
            continue;
        }

        EmbeddingModelDefinition model;
        model.id = id;
        model.backend = "onnx";
        model.name = dir.filename().string().empty() ? path.stem().string() : dir.filename().string();
        model.version = "local";
        model.model_path = path.string();
        model.tokenizer_path = tokenizer.string();
        model.pooling = config.engine.embedding.pooling;
        model.dimension = config.engine.embedding.dimension;
        model.max_seq_len = config.engine.embedding.max_seq_len;
        model.onnx_threads = config.engine.embedding.onnx_threads;
        model.normalize_embeddings = config.engine.embedding.normalize_embeddings;
        model.lowercase_tokens = config.engine.embedding.lowercase_tokens;
        model.enable_fallback = config.engine.embedding.enable_fallback;
        model.discovered = true;
        model.source = "auto_discovered:" + models_dir.string();
        validateEmbeddingModel(config, model);
        registry.models.emplace(model.id, std::move(model));
        ++discovered;
    }
    if (discovered > 0) {
        addWarning(config, "embedding auto discovery registered " + std::to_string(discovered) + " local model(s) from " + models_dir.string());
    }
}

template <typename Map>
void applyCommonRagConfig(const Map& values, RagConfig& config) {
    config.engine.search.use_hybrid = getBool(values, {"search.use_hybrid", "rag.search.use_hybrid"}, config.engine.search.use_hybrid);
    config.engine.search.vector_weight = getFloat(values, {"search.vector_weight", "rag.search.vector_weight"}, config.engine.search.vector_weight);
    config.engine.search.text_weight = getFloat(values, {"search.text_weight", "rag.search.text_weight"}, config.engine.search.text_weight);
    config.engine.search.top_k = getSize(values, {"search.top_k", "rag.search.top_k"}, config.engine.search.top_k);
    config.engine.search.min_score_threshold = getFloat(values, {"search.min_score_threshold", "rag.search.min_score_threshold"}, config.engine.search.min_score_threshold);
    config.engine.search.use_query_expansion = getBool(values, {"search.use_query_expansion", "rag.search.use_query_expansion"}, config.engine.search.use_query_expansion);
    config.engine.search.xapian_enabled = getBool(values, {"search.xapian_enabled", "rag.search.xapian_enabled", "search.xapian.enabled", "rag.search.xapian.enabled"}, config.engine.search.xapian_enabled);
    config.engine.search.xapian_language = lower(getString(values, {"search.xapian_language", "rag.search.xapian_language", "search.xapian.language", "rag.search.xapian.language"}, config.engine.search.xapian_language));
    config.engine.search.xapian_stemming = getBool(values, {"search.xapian_stemming", "rag.search.xapian_stemming", "search.xapian.stemming", "rag.search.xapian.stemming"}, config.engine.search.xapian_stemming);
    config.engine.search.xapian_stemming_strategy = lower(getString(values, {"search.xapian_stemming_strategy", "rag.search.xapian_stemming_strategy", "search.xapian.stemming_strategy", "rag.search.xapian.stemming_strategy"}, config.engine.search.xapian_stemming_strategy));
    config.engine.search.xapian_cjk_ngrams = getBool(values, {"search.xapian_cjk_ngrams", "rag.search.xapian_cjk_ngrams", "search.xapian.cjk_ngrams", "rag.search.xapian.cjk_ngrams"}, config.engine.search.xapian_cjk_ngrams);
    config.engine.search.xapian_word_breaks = getBool(values, {"search.xapian_word_breaks", "rag.search.xapian_word_breaks", "search.xapian.word_breaks", "rag.search.xapian.word_breaks"}, config.engine.search.xapian_word_breaks);
    config.engine.search.xapian_spelling = getBool(values, {"search.xapian_spelling", "rag.search.xapian_spelling", "search.xapian.spelling", "rag.search.xapian.spelling"}, config.engine.search.xapian_spelling);
    config.engine.search.xapian_metadata_prefixes = getBool(values, {"search.xapian_metadata_prefixes", "rag.search.xapian_metadata_prefixes", "search.xapian.metadata_prefixes", "rag.search.xapian.metadata_prefixes"}, config.engine.search.xapian_metadata_prefixes);
    if (!isSupportedXapianLanguage(config.engine.search.xapian_language)) {
        addWarning(config, "unsupported Xapian language '" + config.engine.search.xapian_language + "'; using auto");
        config.engine.search.xapian_language = "auto";
    }
    if (!isSupportedXapianStemmingStrategy(config.engine.search.xapian_stemming_strategy)) {
        addWarning(config, "unsupported Xapian stemming strategy '" + config.engine.search.xapian_stemming_strategy + "'; using some");
        config.engine.search.xapian_stemming_strategy = "some";
    }
    config.engine.search.use_reranking = getBool(values, {"search.use_reranking", "rag.search.use_reranking"}, config.engine.search.use_reranking);
    config.engine.search.rerank_input_multiplier = getSize(values, {"search.rerank_input_multiplier", "rag.search.rerank_input_multiplier"}, config.engine.search.rerank_input_multiplier);
    config.engine.search.rerank_path_boost = getFloat(values, {"search.rerank_path_boost", "rag.search.rerank_path_boost"}, config.engine.search.rerank_path_boost);
    config.engine.search.rerank_metadata_boost = getFloat(values, {"search.rerank_metadata_boost", "rag.search.rerank_metadata_boost"}, config.engine.search.rerank_metadata_boost);
    config.engine.search.rerank_exact_content_boost = getFloat(values, {"search.rerank_exact_content_boost", "rag.search.rerank_exact_content_boost"}, config.engine.search.rerank_exact_content_boost);
    config.engine.search.use_multi_query_retrieval = getBool(values, {"search.use_multi_query_retrieval", "rag.search.use_multi_query_retrieval"}, config.engine.search.use_multi_query_retrieval);
    config.engine.search.multi_query_max_variants = getSize(values, {"search.multi_query_max_variants", "rag.search.multi_query_max_variants"}, config.engine.search.multi_query_max_variants);
    config.engine.search.use_embedding_reranker = getBool(values, {"search.use_embedding_reranker", "rag.search.use_embedding_reranker"}, config.engine.search.use_embedding_reranker);
    config.engine.search.rerank_embedding_boost = getFloat(values, {"search.rerank_embedding_boost", "rag.search.rerank_embedding_boost"}, config.engine.search.rerank_embedding_boost);
    config.engine.search.rerank_coverage_boost = getFloat(values, {"search.rerank_coverage_boost", "rag.search.rerank_coverage_boost"}, config.engine.search.rerank_coverage_boost);
    config.engine.max_file_size_kb = getSize(values, {"indexing.max_file_size_kb", "rag.indexing.max_file_size_kb"}, config.engine.max_file_size_kb);

    config.engine.embedding.backend = getString(values, {"embedding.backend", "rag.embedding.backend"}, config.engine.embedding.backend);
    config.engine.embedding.active_model_id = getString(values, {"embedding.active_model_id", "rag.embedding.active_model_id"}, config.engine.embedding.active_model_id);
    config.engine.embedding.model_id = getString(values, {"embedding.model_id", "rag.embedding.model_id"}, config.engine.embedding.model_id);
    config.engine.embedding.model_name = getString(values, {"embedding.model_name", "rag.embedding.model_name"}, config.engine.embedding.model_name);
    config.engine.embedding.model_version = getString(values, {"embedding.model_version", "rag.embedding.model_version"}, config.engine.embedding.model_version);
    config.engine.embedding.model_path = getString(values, {"embedding.model_path", "rag.embedding.model_path"}, config.engine.embedding.model_path);
    config.engine.embedding.tokenizer_path = getString(values, {"embedding.tokenizer_path", "rag.embedding.tokenizer_path"}, config.engine.embedding.tokenizer_path);
    config.engine.embedding.tokenizer_type = getString(values, {"embedding.tokenizer_type", "rag.embedding.tokenizer_type"}, config.engine.embedding.tokenizer_type);
    config.engine.embedding.pooling = getString(values, {"embedding.pooling", "rag.embedding.pooling"}, config.engine.embedding.pooling);
    config.engine.embedding.dimension = getSize(values, {"embedding.dimension", "rag.embedding.dimension"}, config.engine.embedding.dimension);
    config.engine.embedding.max_seq_len = getSize(values, {"embedding.max_seq_len", "rag.embedding.max_seq_len"}, config.engine.embedding.max_seq_len);
    config.engine.embedding.onnx_threads = getSize(values, {"embedding.onnx_threads", "rag.embedding.onnx_threads"}, config.engine.embedding.onnx_threads);
    config.engine.embedding.models_dir = getString(values, {"embedding.models_dir", "rag.embedding.models_dir"}, config.engine.embedding.models_dir);
    config.engine.embedding.auto_discover_models = getBool(values, {"embedding.auto_discover_models", "rag.embedding.auto_discover_models"}, config.engine.embedding.auto_discover_models);
    config.engine.embedding.validate_model_files = getBool(values, {"embedding.validate_model_files", "rag.embedding.validate_model_files"}, config.engine.embedding.validate_model_files);
    config.engine.embedding.persistent_cache_enabled = getBool(values, {"embedding.persistent_cache_enabled", "rag.embedding.persistent_cache_enabled"}, config.engine.embedding.persistent_cache_enabled);
    config.engine.embedding.chunk_token_margin = getSize(values, {"embedding.chunk_token_margin", "rag.embedding.chunk_token_margin"}, config.engine.embedding.chunk_token_margin);
    config.engine.embedding.normalize_embeddings = getBool(values, {"embedding.normalize_embeddings", "rag.embedding.normalize_embeddings"}, config.engine.embedding.normalize_embeddings);
    config.engine.embedding.enable_fallback = getBool(values, {"embedding.enable_fallback", "rag.embedding.enable_fallback"}, config.engine.embedding.enable_fallback);
    config.engine.embedding.lowercase_tokens = getBool(values, {"embedding.lowercase_tokens", "rag.embedding.lowercase_tokens"}, config.engine.embedding.lowercase_tokens);

    EmbeddingModelRegistry registry;
    for (const auto& id : collectEmbeddingRegistryIds(values)) {
        EmbeddingModelDefinition model;
        model.id = id;
        model.backend = getRegistryString(values, id, {"backend"}, model.backend);
        model.name = getRegistryString(values, id, {"name", "model_name"}, model.name);
        model.version = getRegistryString(values, id, {"version", "model_version"}, model.version);
        model.model_path = getRegistryString(values, id, {"model_path"}, model.model_path);
        model.tokenizer_path = getRegistryString(values, id, {"tokenizer_path"}, model.tokenizer_path);
        model.tokenizer_type = getRegistryString(values, id, {"tokenizer_type"}, model.tokenizer_type);
        model.pooling = getRegistryString(values, id, {"pooling"}, model.pooling);
        model.dimension = getRegistrySize(values, id, "dimension", model.dimension);
        model.max_seq_len = getRegistrySize(values, id, "max_seq_len", model.max_seq_len);
        model.onnx_threads = getRegistrySize(values, id, "onnx_threads", model.onnx_threads);
        model.normalize_embeddings = getRegistryBool(values, id, "normalize_embeddings", model.normalize_embeddings);
        model.lowercase_tokens = getRegistryBool(values, id, "lowercase_tokens", model.lowercase_tokens);
        model.enable_fallback = getRegistryBool(values, id, "enable_fallback", model.enable_fallback);
        model.license = getRegistryString(values, id, {"license"}, model.license);
        model.source = getRegistryString(values, id, {"source"}, model.source);
        validateEmbeddingModel(config, model);
        registry.models.emplace(id, std::move(model));
    }

    discoverLocalEmbeddingModels(config, registry);

    if (!config.engine.embedding.active_model_id.empty()) {
        auto active = registry.models.find(config.engine.embedding.active_model_id);
        if (active != registry.models.end()) {
            apply_embedding_model_definition(config.engine.embedding, active->second);
        } else {
            addWarning(config, "embedding.active_model_id not found in registry: " + config.engine.embedding.active_model_id);
        }
    }
    registry.warnings = config.diagnostics.warnings;
    config.engine.embedding_registry = std::move(registry);

    config.engine.vector_store.backend = getString(values, {"vector_store.backend", "rag.vector_store.backend"}, config.engine.vector_store.backend);
    config.engine.vector_store.index_path = getString(values, {"vector_store.index_path", "rag.vector_store.index_path"}, config.engine.vector_store.index_path);
    config.engine.vector_store.metadata_path = getString(values, {"vector_store.metadata_path", "rag.vector_store.metadata_path"}, config.engine.vector_store.metadata_path);
    config.engine.vector_store.endpoint = getString(values, {"vector_store.endpoint", "rag.vector_store.endpoint"}, config.engine.vector_store.endpoint);
    config.engine.vector_store.api_key = getString(values, {"vector_store.api_key", "rag.vector_store.api_key"}, config.engine.vector_store.api_key);
    config.engine.vector_store.collection = getString(values, {"vector_store.collection", "rag.vector_store.collection"}, config.engine.vector_store.collection);
    config.engine.vector_store.connection_string = getString(values, {"vector_store.connection_string", "rag.vector_store.connection_string"}, config.engine.vector_store.connection_string);
    config.engine.vector_store.table = getString(values, {"vector_store.table", "rag.vector_store.table"}, config.engine.vector_store.table);
    config.engine.vector_store.distance = getString(values, {"vector_store.distance", "rag.vector_store.distance"}, config.engine.vector_store.distance);
    config.engine.vector_store.upsert_batch_size = getSize(values, {"vector_store.upsert_batch_size", "rag.vector_store.upsert_batch_size"}, config.engine.vector_store.upsert_batch_size);
    config.engine.vector_store.recreate = getBool(values, {"vector_store.recreate", "rag.vector_store.recreate"}, config.engine.vector_store.recreate);
    config.engine.vector_store.auto_load = getBool(values, {"vector_store.auto_load", "rag.vector_store.auto_load"}, config.engine.vector_store.auto_load);
    config.engine.vector_store.auto_save = getBool(values, {"vector_store.auto_save", "rag.vector_store.auto_save"}, config.engine.vector_store.auto_save);

    config.security.enabled = getBool(values, {"security.enabled", "rag.security.enabled"}, config.security.enabled);
    config.security.protect_admin_routes = getBool(values, {"security.protect_admin_routes", "rag.security.protect_admin_routes"}, config.security.protect_admin_routes);
    config.security.protect_write_routes = getBool(values, {"security.protect_write_routes", "rag.security.protect_write_routes"}, config.security.protect_write_routes);
    config.security.mode = getString(values, {"security.mode", "rag.security.mode"}, config.security.mode);
    config.security.admin_token = getString(values, {"security.admin_token", "rag.security.admin_token"}, config.security.admin_token);
    config.security.admin_token_env = getString(values, {"security.admin_token_env", "rag.security.admin_token_env"}, config.security.admin_token_env);
    config.security.token_header = getString(values, {"security.token_header", "rag.security.token_header"}, config.security.token_header);
    config.security.role_header = getString(values, {"security.role_header", "rag.security.role_header"}, config.security.role_header);
    config.security.admin_role = getString(values, {"security.admin_role", "rag.security.admin_role"}, config.security.admin_role);

    config.upload.enabled = getBool(values, {"upload.enabled", "rag.upload.enabled"}, config.upload.enabled);
    config.upload.uploads_dir = getString(values, {"upload.uploads_dir", "rag.upload.uploads_dir"}, config.upload.uploads_dir);
    config.upload.max_file_size_kb = getSize(values, {"upload.max_file_size_kb", "rag.upload.max_file_size_kb"}, config.upload.max_file_size_kb);
    config.upload.max_files_per_request = getSize(values, {"upload.max_files_per_request", "rag.upload.max_files_per_request"}, config.upload.max_files_per_request);
    config.upload.auto_ingest = getBool(values, {"upload.auto_ingest", "rag.upload.auto_ingest"}, config.upload.auto_ingest);
    config.upload.async_ingest = getBool(values, {"upload.async_ingest", "rag.upload.async_ingest"}, config.upload.async_ingest);
    config.upload.overwrite_existing = getBool(values, {"upload.overwrite_existing", "rag.upload.overwrite_existing"}, config.upload.overwrite_existing);
    const auto upload_extensions_csv = getString(values, {"upload.allowed_extensions", "rag.upload.allowed_extensions"}, "");
    if (!upload_extensions_csv.empty()) {
        config.upload.allowed_extensions = qornix::rag::splitUploadCsvList(upload_extensions_csv);
    }
    const auto upload_mime_csv = getString(values, {"upload.allowed_mime_types", "rag.upload.allowed_mime_types"}, "");
    if (!upload_mime_csv.empty()) {
        config.upload.allowed_mime_types = qornix::rag::splitUploadCsvList(upload_mime_csv);
    }

    const bool has_llm = values.find("llm.api_url") != values.end()
        || values.find("llm.model") != values.end()
        || values.find("rag.llm.api_url") != values.end()
        || values.find("rag.llm.model") != values.end()
        || values.find("llm.enabled") != values.end()
        || values.find("rag.llm.enabled") != values.end();
    config.llm.enabled = getBool(values, {"llm.enabled", "rag.llm.enabled"}, has_llm ? true : config.llm.enabled);
    config.llm.api_url = getString(values, {"llm.api_url", "rag.llm.api_url"}, config.llm.api_url);
    config.llm.api_key = getString(values, {"llm.api_key", "rag.llm.api_key"}, config.llm.api_key);
    config.llm.model = getString(values, {"llm.model", "rag.llm.model"}, config.llm.model);
    config.llm.max_tokens = getInt(values, {"llm.max_tokens", "rag.llm.max_tokens"}, config.llm.max_tokens);
    config.llm.temperature = getFloat(values, {"llm.temperature", "rag.llm.temperature"}, config.llm.temperature);
    config.llm.top_p = getFloat(values, {"llm.top_p", "rag.llm.top_p"}, config.llm.top_p);
    config.llm.request_timeout_ms = getInt(values, {"llm.request_timeout_ms", "rag.llm.request_timeout_ms"}, config.llm.request_timeout_ms);
    config.llm.system_prompt = getString(values, {"llm.system_prompt", "rag.llm.system_prompt"}, config.llm.system_prompt);
    config.llm.prompt_template = getString(values, {"llm.prompt_template", "rag.llm.prompt_template"}, config.llm.prompt_template);
    config.llm.stream = getBool(values, {"llm.stream", "rag.llm.stream"}, config.llm.stream);

    config.cache.enabled = getBool(values, {"cache.enabled", "rag.cache.enabled"}, config.cache.enabled);
    config.cache.backend = getString(values, {"cache.backend", "rag.cache.backend"}, config.cache.backend);
    config.cache.max_size = getSize(values, {"cache.max_size", "rag.cache.max_size"}, config.cache.max_size);
    config.cache.ttl = std::chrono::seconds(getInt(values, {"cache.ttl_seconds", "rag.cache.ttl_seconds"}, static_cast<int>(config.cache.ttl.count())));
    config.cache.key_prefix = getString(values, {"cache.key_prefix", "rag.cache.key_prefix"}, config.cache.key_prefix);
    config.cache.redis_host = getString(values, {"cache.redis.host", "rag.cache.redis.host"}, config.cache.redis_host);
    config.cache.redis_port = getInt(values, {"cache.redis.port", "rag.cache.redis.port"}, config.cache.redis_port);
    config.cache.redis_db = getInt(values, {"cache.redis.db", "rag.cache.redis.db"}, config.cache.redis_db);
    config.cache.redis_password = getString(values, {"cache.redis.password", "rag.cache.redis.password"}, config.cache.redis_password);
    const bool has_redis_ttl = values.find("cache.redis.ttl_seconds") != values.end()
        || values.find("rag.cache.redis.ttl_seconds") != values.end();
    config.cache.redis_ttl = std::chrono::seconds(getInt(
        values,
        {"cache.redis.ttl_seconds", "rag.cache.redis.ttl_seconds"},
        static_cast<int>((has_redis_ttl ? config.cache.redis_ttl : config.cache.ttl).count())));

    config.rate_limit.enabled = getBool(values, {"rate_limit.enabled", "rag.rate_limit.enabled"}, config.rate_limit.enabled);
    config.rate_limit.max_requests_per_second = getSize(values, {"rate_limit.max_requests_per_second", "rag.rate_limit.max_requests_per_second"}, config.rate_limit.max_requests_per_second);
    config.rate_limit.max_requests_per_minute = getSize(values, {"rate_limit.max_requests_per_minute", "rag.rate_limit.max_requests_per_minute"}, config.rate_limit.max_requests_per_minute);
    config.rate_limit.per_ip_limit = getBool(values, {"rate_limit.per_ip_limit", "rag.rate_limit.per_ip_limit"}, config.rate_limit.per_ip_limit);
    config.rate_limit.max_requests_per_second_per_ip = getSize(values, {"rate_limit.max_requests_per_second_per_ip", "rag.rate_limit.max_requests_per_second_per_ip"}, config.rate_limit.max_requests_per_second_per_ip);
    config.rate_limit.max_requests_per_minute_per_ip = getSize(values, {"rate_limit.max_requests_per_minute_per_ip", "rag.rate_limit.max_requests_per_minute_per_ip"}, config.rate_limit.max_requests_per_minute_per_ip);

    config.batch.max_concurrent = getSize(values, {"batch.max_concurrent", "rag.batch.max_concurrent"}, config.batch.max_concurrent);
    config.batch.question_timeout_ms = getInt(values, {"batch.question_timeout_ms", "rag.batch.question_timeout_ms"}, config.batch.question_timeout_ms);
    config.batch.batch_timeout_ms = getInt(values, {"batch.batch_timeout_ms", "rag.batch.batch_timeout_ms"}, config.batch.batch_timeout_ms);

    config.prompt_cache.enabled = getBool(values, {"prompt_cache.enabled", "rag.prompt_cache.enabled"}, config.prompt_cache.enabled);
    config.prompt_cache.max_size = getSize(values, {"prompt_cache.max_size", "rag.prompt_cache.max_size"}, config.prompt_cache.max_size);
    config.prompt_cache.ttl = std::chrono::seconds(getInt(values, {"prompt_cache.ttl_seconds", "rag.prompt_cache.ttl_seconds"}, static_cast<int>(config.prompt_cache.ttl.count())));

    config.analytics.max_log_entries = getSize(values, {"rag.analytics.max_log_entries", "analytics.max_log_entries"}, config.analytics.max_log_entries);
    config.analytics.top_n = getSize(values, {"rag.analytics.top_n", "analytics.top_n"}, config.analytics.top_n);
    config.analytics.gap_min_search_count = getSize(values, {"rag.analytics.gap_min_search_count", "analytics.gap_min_search_count"}, config.analytics.gap_min_search_count);

    config.dedup.similarity_threshold = getFloat(values, {"rag.dedup.similarity_threshold", "dedup.similarity_threshold"}, config.dedup.similarity_threshold);
    config.dedup.auto_remove = getBool(values, {"rag.dedup.auto_remove", "dedup.auto_remove"}, config.dedup.auto_remove);

    config.markdown_enabled = getBool(values, {"rag.markdown.enabled", "markdown.enabled"}, config.markdown_enabled);
    config.markdown.directory_path = getString(values, {"rag.markdown.directory_path", "markdown.directory_path"}, config.markdown.directory_path.empty() ? "knowledge_base" : config.markdown.directory_path);
    config.markdown.recursive = getBool(values, {"rag.markdown.recursive", "markdown.recursive"}, config.markdown.recursive);
    if (config.markdown.file_patterns.empty()) {
        config.markdown.file_patterns = {"*.md"};
    }

#if QORNIX_HAS_SQLITE
    config.sqlite_enabled = getBool(values, {"rag.sqlite.enabled", "sqlite.enabled"}, config.sqlite_enabled);
    config.sqlite.db_path = getString(values, {"rag.sqlite.db_path", "sqlite.db_path"}, config.sqlite.db_path.empty() ? "rag_kb.db" : config.sqlite.db_path);
    config.sqlite.source_id = getString(values, {"rag.sqlite.source_id", "sqlite.source_id"}, config.sqlite.source_id.empty() ? "sqlite_kb" : config.sqlite.source_id);
    config.sqlite.name = getString(values, {"rag.sqlite.name", "sqlite.name"}, config.sqlite.name.empty() ? "SQLite Knowledge Base" : config.sqlite.name);
    config.sqlite.auto_migrate = getBool(values, {"rag.sqlite.auto_migrate", "sqlite.auto_migrate"}, config.sqlite.auto_migrate);
#endif
}

} // namespace

std::string ragRuntimeModeToString(RagRuntimeMode mode) {
    switch (mode) {
        case RagRuntimeMode::Standalone: return "standalone";
        case RagRuntimeMode::Integrated: return "integrated";
        case RagRuntimeMode::Programmatic: return "programmatic";
    }
    return "programmatic";
}

RagConfig makeRagConfigFromStandaloneFlatMap(
    const std::unordered_map<std::string, std::string>& flat,
    const std::string& config_source) {
    RagConfig config;
    config.mode = RagRuntimeMode::Standalone;
    config.config_source = config_source.empty() ? "standalone defaults" : config_source;
    config.routes.expose_root_ui = true;
    config.routes.ui_path = "/";
    config.routes.api_prefix = "/api";

    config.address = getString(flat, {"server.address", "server.host"}, config.address);
    config.port = getInt(flat, {"server.port"}, config.port);
    {
        const std::string scan_path = getString(flat, {"indexing.scan_path"}, "");
        if (!scan_path.empty()) {
            config.scan_path = scan_path;
        }
    }
    config.auto_index_on_startup = getBool(flat, {"indexing.auto_index_on_startup"}, config.auto_index_on_startup);

    applyCommonRagConfig(flat, config);
    return config;
}

RagConfig makeRagConfigFromIntegratedFlatMap(
    const std::map<std::string, std::string>& flat,
    const std::string& config_source) {
    RagConfig config;
    config.mode = RagRuntimeMode::Integrated;
    config.config_source = config_source.empty() ? "host application config" : config_source;
    config.routes.expose_root_ui = getBool(flat, {"rag.route.expose_root_ui", "route.expose_root_ui"}, false);
    config.routes.ui_path = normalizePathPrefix(getString(flat, {"rag.route.ui_path", "route.ui_path"}, "/rag"), "/rag");
    config.routes.api_prefix = normalizePathPrefix(getString(flat, {"rag.route.api_prefix", "route.api_prefix"}, "/api/rag"), "/api/rag");
    {
        const std::string scan_path = getString(flat, {"rag.indexing.scan_path", "indexing.scan_path"}, "");
        if (!scan_path.empty()) {
            config.scan_path = scan_path;
        }
    }
    config.auto_index_on_startup = getBool(flat, {"rag.indexing.auto_index_on_startup", "indexing.auto_index_on_startup"}, config.auto_index_on_startup);

    applyCommonRagConfig(flat, config);
    return config;
}
