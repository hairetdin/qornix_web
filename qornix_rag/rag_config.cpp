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
#include <iostream>
#include <set>

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

void validateEmbeddingModel(RagConfig& config, const EmbeddingModelDefinition& model) {
    if (!isSupportedEmbeddingBackend(model.backend)) {
        addWarning(config, "embedding.registry." + model.id + ".backend unsupported: " + model.backend);
    }
    if (model.backend == "onnx") {
        if (model.model_path.empty()) {
            addWarning(config, "embedding.registry." + model.id + ".model_path is required for onnx backend");
        }
        if (model.tokenizer_path.empty()) {
            addWarning(config, "embedding.registry." + model.id + ".tokenizer_path is required for onnx backend");
        }
    }
    if (!isSupportedPooling(model.pooling)) {
        addWarning(config, "embedding.registry." + model.id + ".pooling unsupported: " + model.pooling);
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
    config.engine.search.use_reranking = getBool(values, {"search.use_reranking", "rag.search.use_reranking"}, config.engine.search.use_reranking);
    config.engine.search.rerank_input_multiplier = getSize(values, {"search.rerank_input_multiplier", "rag.search.rerank_input_multiplier"}, config.engine.search.rerank_input_multiplier);
    config.engine.search.rerank_path_boost = getFloat(values, {"search.rerank_path_boost", "rag.search.rerank_path_boost"}, config.engine.search.rerank_path_boost);
    config.engine.search.rerank_metadata_boost = getFloat(values, {"search.rerank_metadata_boost", "rag.search.rerank_metadata_boost"}, config.engine.search.rerank_metadata_boost);
    config.engine.search.rerank_exact_content_boost = getFloat(values, {"search.rerank_exact_content_boost", "rag.search.rerank_exact_content_boost"}, config.engine.search.rerank_exact_content_boost);
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
    config.llm.stream = getBool(values, {"llm.stream", "rag.llm.stream"}, config.llm.stream);

    config.cache.enabled = getBool(values, {"cache.enabled", "rag.cache.enabled"}, config.cache.enabled);
    config.cache.backend = getString(values, {"cache.backend", "rag.cache.backend"}, config.cache.backend);
    config.cache.max_size = getSize(values, {"cache.max_size", "rag.cache.max_size"}, config.cache.max_size);
    config.cache.ttl = std::chrono::seconds(getInt(values, {"cache.ttl_seconds", "rag.cache.ttl_seconds"}, static_cast<int>(config.cache.ttl.count())));

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
    config.project_path = getString(flat, {"server.project_path"}, config.project_path);
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
    config.routes.expose_root_ui = false;
    config.routes.ui_path = normalizePathPrefix(getString(flat, {"rag.route.ui_path", "route.ui_path"}, "/rag"), "/rag");
    config.routes.api_prefix = normalizePathPrefix(getString(flat, {"rag.route.api_prefix", "route.api_prefix"}, "/api/rag"), "/api/rag");

    applyCommonRagConfig(flat, config);
    return config;
}
