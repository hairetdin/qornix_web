/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "llm_client.h"
#include <boost/json.hpp>
#include <iostream>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <functional>
#include <utility>

#ifndef QORNIX_HAS_CURL
#define QORNIX_HAS_CURL 0
#endif

namespace {

namespace json = boost::json;

std::string json_string_value(const json::value& value);
std::vector<std::string> json_payload_lines(const std::string& text);

std::string apply_prompt_template(std::string template_str,
                                  const std::string& context,
                                  const std::string& question) {
    size_t pos = 0;
    while ((pos = template_str.find("{context}", pos)) != std::string::npos) {
        const std::string replacement = context.empty() ? "(Нет контекста)" : context;
        template_str.replace(pos, 9, replacement);
        pos += replacement.size();
    }

    pos = 0;
    while ((pos = template_str.find("{question}", pos)) != std::string::npos) {
        template_str.replace(pos, 10, question);
        pos += question.size();
    }

    return template_str;
}

bool response_has_error(const std::string& response) {
    if (response.empty()) {
        return true;
    }

    boost::system::error_code ec;
    const auto parsed = json::parse(response, ec);
    if (!ec && parsed.is_object()) {
        const auto& object = parsed.as_object();
        if (object.contains("error")) {
            return true;
        }
        const auto status_it = object.find("status");
        if (status_it != object.end() && status_it->value().is_string()) {
            const std::string status(status_it->value().as_string().c_str(),
                                     status_it->value().as_string().size());
            return status == "error" || status == "failed";
        }
        return false;
    }

    return response.rfind("error:", 0) == 0 ||
           response.rfind("Error:", 0) == 0 ||
           response.find("Ошибка") != std::string::npos ||
           response.find("недоступ") != std::string::npos ||
           response.find("CURL is not available") != std::string::npos;
}

bool response_is_provider_error_json(const std::string& response) {
    boost::system::error_code ec;
    const auto parsed = json::parse(response, ec);
    if (ec || !parsed.is_object()) {
        return false;
    }

    const auto& object = parsed.as_object();
    if (object.contains("error")) {
        return true;
    }

    const auto status_it = object.find("status");
    if (status_it != object.end() && status_it->value().is_string()) {
        const std::string status(status_it->value().as_string().c_str(),
                                 status_it->value().as_string().size());
        return status == "error" || status == "failed";
    }

    return false;
}

bool is_ollama_url(const std::string& url) {
    return url.find("11434") != std::string::npos ||
           url.find("/api/chat") != std::string::npos ||
           url.find("/api/generate") != std::string::npos ||
           url.find("/api/tags") != std::string::npos;
}

std::string ollama_base_url(std::string url) {
    const std::vector<std::string> suffixes = {
        "/api/chat",
        "/api/generate",
        "/api/tags"
    };

    for (const auto& suffix : suffixes) {
        const auto pos = url.find(suffix);
        if (pos != std::string::npos) {
            url = url.substr(0, pos);
            break;
        }
    }

    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    return url;
}

std::string openai_models_url(std::string url) {
    const std::vector<std::string> suffixes = {
        "/v1/chat/completions",
        "/chat/completions",
        "/v1/models"
    };

    for (const auto& suffix : suffixes) {
        const auto pos = url.find(suffix);
        if (pos != std::string::npos) {
            url = url.substr(0, pos);
            break;
        }
    }

    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    return url + "/v1/models";
}


void add_unique_string(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void collect_model_name_from_object(const json::object& object,
                                    std::vector<std::string>& models) {
    // Provider model-list shapes covered here:
    // - Ollama /api/tags: {"models":[{"name":"llama3:latest", "model":"..."}]}
    // - OpenAI/vLLM/LM Studio /v1/models: {"data":[{"id":"..."}]}
    // - Some OpenAI-compatible servers expose {"models":["..."]} or
    //   a single {"id"|"name"|"model":"..."} object.
    for (const char* key : {"id", "name", "model"}) {
        const auto it = object.find(key);
        if (it != object.end() && it->value().is_string()) {
            add_unique_string(models, json_string_value(it->value()));
            return;
        }
    }
}

void collect_model_names_from_value(const json::value& value,
                                    std::vector<std::string>& models) {
    if (value.is_string()) {
        add_unique_string(models, json_string_value(value));
        return;
    }

    if (value.is_object()) {
        collect_model_name_from_object(value.as_object(), models);
        return;
    }

    if (value.is_array()) {
        for (const auto& entry : value.as_array()) {
            collect_model_names_from_value(entry, models);
        }
    }
}

std::vector<std::string> parse_model_list_structured(const std::string& response) {
    std::vector<std::string> models;
    if (response.empty()) {
        return models;
    }

    auto collect_from_parsed = [&](const json::value& parsed) {
        if (parsed.is_array()) {
            collect_model_names_from_value(parsed, models);
            return;
        }

        if (!parsed.is_object()) {
            return;
        }

        const auto& object = parsed.as_object();
        for (const char* key : {"models", "data"}) {
            const auto it = object.find(key);
            if (it != object.end()) {
                collect_model_names_from_value(it->value(), models);
            }
        }

        // Also accept a single model object for small test servers.
        collect_model_name_from_object(object, models);
    };

    boost::system::error_code ec;
    json::value parsed = json::parse(response, ec);
    if (!ec) {
        collect_from_parsed(parsed);
        return models;
    }

    // Some debugging endpoints return NDJSON; parse each JSON payload line with
    // the same structured parser rather than falling back to string scanning.
    for (const auto& line : json_payload_lines(response)) {
        ec = {};
        parsed = json::parse(line, ec);
        if (!ec) {
            collect_from_parsed(parsed);
        }
    }

    return models;
}

size_t json_size_value(const json::value& value) {
    try {
        if (value.is_uint64()) {
            return static_cast<size_t>(value.as_uint64());
        }
        if (value.is_int64()) {
            const auto n = value.as_int64();
            return n > 0 ? static_cast<size_t>(n) : 0;
        }
        if (value.is_double()) {
            const auto n = value.as_double();
            return n > 0 ? static_cast<size_t>(n) : 0;
        }
        if (value.is_string()) {
            const auto text = json_string_value(value);
            if (!text.empty()) {
                return static_cast<size_t>(std::stoull(text));
            }
        }
    } catch (...) {
        return 0;
    }
    return 0;
}

size_t object_size_field(const json::object& object,
                         std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const auto it = object.find(key);
        if (it != object.end()) {
            const auto value = json_size_value(it->value());
            if (value > 0) {
                return value;
            }
        }
    }
    return 0;
}

void merge_token_stats(LLMTokenStats& stats, const LLMTokenStats& candidate) {
    // Streaming chunks often repeat or only report final usage in the last event.
    // Taking max keeps parsing idempotent across single JSON, SSE, and NDJSON.
    stats.prompt_tokens = std::max(stats.prompt_tokens, candidate.prompt_tokens);
    stats.completion_tokens = std::max(stats.completion_tokens, candidate.completion_tokens);
    stats.total_tokens = std::max(stats.total_tokens, candidate.total_tokens);
}

LLMTokenStats token_stats_from_object(const json::object& object) {
    LLMTokenStats stats;

    const auto usage_it = object.find("usage");
    if (usage_it != object.end() && usage_it->value().is_object()) {
        const auto& usage = usage_it->value().as_object();
        stats.prompt_tokens = object_size_field(usage, {
            "prompt_tokens", "input_tokens", "promptTokens", "inputTokens"
        });
        stats.completion_tokens = object_size_field(usage, {
            "completion_tokens", "output_tokens", "completionTokens", "outputTokens"
        });
        stats.total_tokens = object_size_field(usage, {
            "total_tokens", "totalTokens"
        });
    }

    // Ollama chat/generate final response fields.
    if (stats.prompt_tokens == 0) {
        stats.prompt_tokens = object_size_field(object, {"prompt_eval_count", "prompt_tokens"});
    }
    if (stats.completion_tokens == 0) {
        stats.completion_tokens = object_size_field(object, {"eval_count", "completion_tokens"});
    }
    if (stats.total_tokens == 0) {
        stats.total_tokens = object_size_field(object, {"total_tokens", "totalTokens"});
    }
    if (stats.total_tokens == 0 && (stats.prompt_tokens > 0 || stats.completion_tokens > 0)) {
        stats.total_tokens = stats.prompt_tokens + stats.completion_tokens;
    }

    return stats;
}

void collect_token_stats_from_value(const json::value& value, LLMTokenStats& stats) {
    if (value.is_object()) {
        const auto& object = value.as_object();
        merge_token_stats(stats, token_stats_from_object(object));

        const auto choices_it = object.find("choices");
        if (choices_it != object.end() && choices_it->value().is_array()) {
            for (const auto& choice : choices_it->value().as_array()) {
                collect_token_stats_from_value(choice, stats);
            }
        }
        return;
    }

    if (value.is_array()) {
        for (const auto& item : value.as_array()) {
            collect_token_stats_from_value(item, stats);
        }
    }
}

bool model_name_matches(const std::string& configured_model,
                        const std::string& available_model) {
    if (configured_model.empty()) {
        return true;
    }

    if (configured_model == available_model) {
        return true;
    }

    return available_model == configured_model + ":latest" ||
           available_model.rfind(configured_model + ":", 0) == 0;
}

bool model_list_contains(const std::vector<std::string>& models,
                         const std::string& configured_model) {
    if (models.empty()) {
        return false;
    }

    return std::any_of(models.begin(), models.end(), [&](const std::string& model) {
        return model_name_matches(configured_model, model);
    });
}


void append_with_spacing(std::string& target, const std::string& chunk) {
    if (chunk.empty()) {
        return;
    }
    if (!target.empty() && target.back() != '\n' && chunk.front() != '\n') {
        target += '\n';
    }
    target += chunk;
}

std::string json_string_value(const json::value& value) {
    if (!value.is_string()) {
        return {};
    }
    return std::string(value.as_string().c_str(), value.as_string().size());
}

std::string object_string(const json::object& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    return json_string_value(it->value());
}

void inspect_generation_metadata(const json::object& object,
                                 LLMGenerationResult& result) {
    const auto finish_reason = object_string(object, "finish_reason");
    if (!finish_reason.empty()) {
        result.finish_reason = finish_reason;
        if (finish_reason == "length" ||
            finish_reason == "content_filter" ||
            finish_reason == "max_tokens") {
            result.truncated = true;
        }
    }

    const auto done_reason = object_string(object, "done_reason");
    if (!done_reason.empty()) {
        result.finish_reason = done_reason;
        if (done_reason == "length" ||
            done_reason == "max_tokens" ||
            done_reason == "stop_limit") {
            result.truncated = true;
        }
    }

    const auto done_it = object.find("done");
    if (done_it != object.end() &&
        done_it->value().is_bool() &&
        !done_it->value().as_bool()) {
        result.truncated = true;
        if (result.finish_reason.empty()) {
            result.finish_reason = "incomplete";
        }
    }
}

void collect_llm_payload(const json::value& value,
                         LLMGenerationResult& result) {
    if (!value.is_object()) {
        return;
    }

    const auto& object = value.as_object();
    inspect_generation_metadata(object, result);

    const auto error_it = object.find("error");
    if (error_it != object.end()) {
        if (error_it->value().is_string()) {
            result.status = "provider_error";
            result.answer = "Ошибка LLM: " + json_string_value(error_it->value());
            return;
        }
        if (error_it->value().is_object()) {
            const auto message = object_string(error_it->value().as_object(), "message");
            if (!message.empty()) {
                result.status = "provider_error";
                result.answer = "Ошибка LLM: " + message;
                return;
            }
        }
    }

    const auto choices_it = object.find("choices");
    if (choices_it != object.end() && choices_it->value().is_array()) {
        for (const auto& choice : choices_it->value().as_array()) {
            if (!choice.is_object()) {
                continue;
            }
            const auto& choice_obj = choice.as_object();
            inspect_generation_metadata(choice_obj, result);

            const auto message_it = choice_obj.find("message");
            if (message_it != choice_obj.end() && message_it->value().is_object()) {
                append_with_spacing(result.answer, object_string(message_it->value().as_object(), "content"));
            }

            const auto delta_it = choice_obj.find("delta");
            if (delta_it != choice_obj.end() && delta_it->value().is_object()) {
                append_with_spacing(result.answer, object_string(delta_it->value().as_object(), "content"));
            }

            append_with_spacing(result.answer, object_string(choice_obj, "text"));
        }
    }

    const auto message_it = object.find("message");
    if (message_it != object.end() && message_it->value().is_object()) {
        append_with_spacing(result.answer, object_string(message_it->value().as_object(), "content"));
    }

    append_with_spacing(result.answer, object_string(object, "content"));
    append_with_spacing(result.answer, object_string(object, "response"));
}


void collect_stream_chunks_from_value(const json::value& value,
                                      std::vector<std::string>& chunks) {
    if (!value.is_object()) {
        return;
    }

    const auto& object = value.as_object();

    const auto error_it = object.find("error");
    if (error_it != object.end()) {
        if (error_it->value().is_string()) {
            add_unique_string(chunks, "Ошибка LLM: " + json_string_value(error_it->value()));
            return;
        }
        if (error_it->value().is_object()) {
            const auto message = object_string(error_it->value().as_object(), "message");
            if (!message.empty()) {
                add_unique_string(chunks, "Ошибка LLM: " + message);
                return;
            }
        }
    }

    const auto choices_it = object.find("choices");
    if (choices_it != object.end() && choices_it->value().is_array()) {
        for (const auto& choice : choices_it->value().as_array()) {
            if (!choice.is_object()) {
                continue;
            }
            const auto& choice_obj = choice.as_object();

            const auto delta_it = choice_obj.find("delta");
            if (delta_it != choice_obj.end() && delta_it->value().is_object()) {
                const auto content = object_string(delta_it->value().as_object(), "content");
                if (!content.empty()) {
                    chunks.push_back(content);
                }
            }

            const auto message_it = choice_obj.find("message");
            if (message_it != choice_obj.end() && message_it->value().is_object()) {
                const auto content = object_string(message_it->value().as_object(), "content");
                if (!content.empty()) {
                    chunks.push_back(content);
                }
            }

            const auto text = object_string(choice_obj, "text");
            if (!text.empty()) {
                chunks.push_back(text);
            }
        }
    }

    const auto message_it = object.find("message");
    if (message_it != object.end() && message_it->value().is_object()) {
        const auto content = object_string(message_it->value().as_object(), "content");
        if (!content.empty()) {
            chunks.push_back(content);
        }
    }

    const auto content = object_string(object, "content");
    if (!content.empty()) {
        chunks.push_back(content);
    }

    const auto response = object_string(object, "response");
    if (!response.empty()) {
        chunks.push_back(response);
    }
}

std::vector<std::string> parse_stream_chunks_structured(const std::string& stream_response) {
    std::vector<std::string> chunks;
    if (stream_response.empty()) {
        return chunks;
    }

    boost::system::error_code ec;
    json::value parsed = json::parse(stream_response, ec);
    if (!ec) {
        collect_stream_chunks_from_value(parsed, chunks);
        return chunks;
    }

    for (const auto& line : json_payload_lines(stream_response)) {
        ec = {};
        parsed = json::parse(line, ec);
        if (!ec) {
            collect_stream_chunks_from_value(parsed, chunks);
        }
    }

    return chunks;
}

std::vector<std::string> json_payload_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        line = line.substr(first);
        if (line.rfind("data:", 0) == 0) {
            line = line.substr(5);
            const auto data_first = line.find_first_not_of(" \t");
            line = data_first == std::string::npos ? std::string{} : line.substr(data_first);
            if (line == "[DONE]") {
                continue;
            }
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }

    return lines;
}

} // namespace

// ============================================================================
// JSON helpers
// ============================================================================

std::string LLMClient::json_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

std::string LLMClient::build_messages_json(const std::string& system_prompt,
                                           const std::string& user_message) {
    std::ostringstream json;
    json << "{\"messages\":[";
    // System message
    json << "{\"role\":\"system\",\"content\":\""
         << json_escape(system_prompt) << "\"}";
    // User message
    json << ",{\"role\":\"user\",\"content\":\""
         << json_escape(user_message) << "\"}";
    json << "]}";
    return json.str();
}

std::string LLMClient::json_escape_chunk(const std::string& s) {
    // SSE-friendly escaping (lighter than full JSON escaping)
    std::string result;
    result.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

// ============================================================================
// Curl callbacks
// ============================================================================

#if QORNIX_HAS_CURL

size_t LLMClient::write_callback(char* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t LLMClient::stream_write_callback(char* ptr, size_t size, size_t nmemb, std::vector<std::string>* out) {
    std::string chunk(ptr, size * nmemb);
    out->push_back(chunk);
    return size * nmemb;
}

#endif

// ============================================================================
// Config loading
// ============================================================================

void LLMClient::load_config(const std::string& config_path) {
    config_path_ = config_path;

#ifdef QORNIX_HAS_YAML
    if (config_path.empty()) {
        return; // Use defaults
    }

    std::ifstream file(config_path);
    if (!file.is_open()) {
        return; // Use defaults
    }
    file.close();

    try {
        YAML::Node node = YAML::LoadFile(config_path);
        if (!node["llm"]) {
            return; // No LLM section
        }

        const auto& llm = node["llm"];

        config_.enabled = true;

        if (llm["api_url"]) {
            config_.api_url = llm["api_url"].as<std::string>();
        }
        if (llm["api_key"]) {
            config_.api_key = llm["api_key"].as<std::string>();
        }
        if (llm["model"]) {
            config_.model = llm["model"].as<std::string>();
        }
        if (llm["max_tokens"]) {
            config_.max_tokens = llm["max_tokens"].as<int>();
        }
        if (llm["temperature"]) {
            config_.temperature = llm["temperature"].as<float>();
        }
        if (llm["top_p"]) {
            config_.top_p = llm["top_p"].as<float>();
        }
        if (llm["request_timeout_ms"]) {
            config_.request_timeout_ms = llm["request_timeout_ms"].as<int>();
        }
        if (llm["system_prompt"]) {
            config_.system_prompt = llm["system_prompt"].as<std::string>();
        }
        if (llm["stream"]) {
            config_.stream = llm["stream"].as<bool>();
        }

        // Failover
        if (llm["failover"]) {
            const auto& failover = llm["failover"];
            if (failover["enabled"] && failover["enabled"].as<bool>()) {
                config_.failover_enabled = true;
            }
            if (failover["endpoints"]) {
                const auto& endpoints = failover["endpoints"];
                for (const auto& ep : endpoints) {
                    LLMFailoverEndpoint endpoint;
                    if (ep["url"]) {
                        endpoint.url = ep["url"].as<std::string>();
                    }
                    if (ep["model"]) {
                        endpoint.model = ep["model"].as<std::string>();
                    }
                    config_.failover_endpoints.push_back(endpoint);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: LLM config parse error: " << e.what() << std::endl;
    }
#else
    (void)config_path;
    // yaml-cpp not available — LLM support disabled
    config_.enabled = false;
#endif
}

void LLMClient::parse_config_map(
    const std::unordered_map<std::string, std::string>& config) {

    auto get_str = [&](const std::string& key, const std::string& default_val) -> std::string {
        auto it = config.find(key);
        return it != config.end() ? it->second : default_val;
    };

    auto get_int = [&](const std::string& key, int default_val) -> int {
        auto it = config.find(key);
        if (it == config.end()) return default_val;
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_val;
        }
    };

    auto get_float = [&](const std::string& key, float default_val) -> float {
        auto it = config.find(key);
        if (it == config.end()) return default_val;
        try {
            return std::stof(it->second);
        } catch (...) {
            return default_val;
        }
    };

    auto get_bool = [&](const std::string& key, bool default_val) -> bool {
        auto it = config.find(key);
        if (it == config.end()) return default_val;
        const std::string& val = it->second;
        return (val == "true" || val == "1" || val == "yes");
    };

    // Check if llm section exists
    if (config.find("llm.api_url") == config.end() &&
        config.find("llm.model") == config.end()) {
        return; // No LLM config
    }

    config_.enabled = true;
    config_.api_url = get_str("llm.api_url", "http://localhost:11434");
    config_.api_key = get_str("llm.api_key", "");
    config_.model = get_str("llm.model", "llama3");
    config_.max_tokens = get_int("llm.max_tokens", 1024);
    config_.temperature = get_float("llm.temperature", 0.7f);
    config_.top_p = get_float("llm.top_p", 0.9f);
    config_.request_timeout_ms = get_int("llm.request_timeout_ms", 30000);

    std::string sys_prompt = get_str("llm.system_prompt", config_.system_prompt);
    if (!sys_prompt.empty()) {
        config_.system_prompt = sys_prompt;
    }

    config_.stream = get_bool("llm.stream", false);
}

// ============================================================================
// Constructor
// ============================================================================

LLMClient::LLMClient(const std::string& config_path) {
#if QORNIX_HAS_CURL
    // Initialize curl global
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

    if (!config_path.empty()) {
        // Try YAML config first
        load_config(config_path);

        // If YAML didn't provide LLM config, try flattened map format
        if (!config_.enabled) {
#ifdef QORNIX_HAS_YAML
            std::ifstream file(config_path);
            if (file.is_open()) {
                file.close();
                try {
                    YAML::Node node = YAML::LoadFile(config_path);
                    std::unordered_map<std::string, std::string> flat_config;

                    std::function<void(const YAML::Node&, const std::string&)> flatten;
                    flatten = [&](const YAML::Node& parent, const std::string& prefix) {
                        for (const auto& it : parent) {
                            std::string key = it.first.as<std::string>();
                            const auto& val = it.second;
                            std::string full_key = prefix.empty() ? key : prefix + "." + key;

                            if (val.IsMap()) {
                                flatten(val, full_key);
                            } else if (val.IsScalar()) {
                                flat_config[full_key] = val.as<std::string>();
                            }
                        }
                    };

                    flatten(node, "");
                    parse_config_map(flat_config);
                } catch (...) {
                    // Ignore
                }
            }
#endif
        }
    }
}

LLMClient::LLMClient(const LLMConfig& config)
    : config_(config) {
#if QORNIX_HAS_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

// ============================================================================
// Prompt building
// ============================================================================

std::string LLMClient::build_prompt(const std::string& context,
                                    const std::string& question) const {
    return apply_prompt_template(config_.prompt_template, context, question);
}

std::string LLMClient::build_request_json(const std::string& prompt) const {
    return build_request_json(prompt, false);
}

std::string LLMClient::build_request_json(const std::string& prompt, bool stream) const {
    return build_request_json(prompt, stream, std::nullopt);
}

std::string LLMClient::build_request_json(
    const std::string& prompt,
    bool stream,
    const std::optional<std::string>& system_prompt_override) const {
    std::ostringstream json;
    const std::string& system_prompt = system_prompt_override && !system_prompt_override->empty()
        ? *system_prompt_override
        : config_.system_prompt;

    if (is_ollama_url(config_.api_url)) {
        json << "{\"model\":\"" << json_escape(config_.model) << "\",";
        json << "\"stream\":" << (stream ? "true" : "false") << ",";
        json << "\"messages\":[";

        json << "{\"role\":\"system\",\"content\":\""
             << json_escape(system_prompt) << "\"},";

        json << "{\"role\":\"user\",\"content\":\""
             << json_escape(prompt) << "\"}],";

        json << "\"options\":{";
        json << "\"temperature\":" << std::fixed << std::setprecision(1)
             << config_.temperature << ",";
        json << "\"top_p\":" << std::fixed << std::setprecision(1)
             << config_.top_p << ",";
        json << "\"num_predict\":" << config_.max_tokens;
        json << "}}";
        return json.str();
    }

    json << "{\"model\":\"" << json_escape(config_.model) << "\",";
    json << "\"max_tokens\":" << config_.max_tokens << ",";
    json << "\"temperature\":" << std::fixed << std::setprecision(1)
         << config_.temperature << ",";
    if (stream) {
        json << "\"stream\":true,";
    }
    json << "\"messages\":[";

    // System message
    json << "{\"role\":\"system\",\"content\":\""
         << json_escape(system_prompt) << "\"},";

    // User message
    json << "{\"role\":\"user\",\"content\":\""
         << json_escape(prompt) << "\"}";

    json << "]}";
    return json.str();
}

// ============================================================================
// Response parsing
// ============================================================================

std::string LLMClient::parse_response(const std::string& json_response) const {
    return parse_response_result(json_response).answer;
}

LLMGenerationResult LLMClient::parse_response_result(const std::string& json_response) const {
    LLMGenerationResult result;

    if (json_response.empty()) {
        result.status = "parser_error";
        result.parser_error = "empty_response";
        result.answer = "Ошибка парсинга LLM: пустой ответ от провайдера";
        return result;
    }

    boost::system::error_code ec;
    json::value parsed = json::parse(json_response, ec);
    if (!ec) {
        collect_llm_payload(parsed, result);
    } else {
        for (const auto& line : json_payload_lines(json_response)) {
            ec = {};
            parsed = json::parse(line, ec);
            if (ec) {
                result.status = "parser_error";
                result.parser_error = ec.message();
                result.answer = "Ошибка парсинга LLM: " + ec.message();
                return result;
            }
            collect_llm_payload(parsed, result);
        }
    }

    if (!result.parser_error.empty() && result.answer.empty()) {
        result.answer = "Ошибка LLM: " + result.parser_error;
        return result;
    }

    if (result.answer.empty()) {
        result.status = "parser_error";
        result.parser_error = "missing_answer_field";
        result.answer = "Ошибка парсинга LLM: не удалось найти поле ответа";
        return result;
    }

    if (result.truncated && result.status == "ok") {
        result.status = "truncated";
    }

    return result;
}

// ============================================================================
// HTTP request
// ============================================================================

std::string LLMClient::make_request(const std::string& url,
                                    const std::string& request_body,
                                    int timeout_ms) const {
    (void)url;
    (void)request_body;
    (void)timeout_ms;

#if QORNIX_HAS_CURL
    std::string response_body;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "Ошибка: не удалось инициализировать CURL";
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (!config_.api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(static_cast<long>(timeout_ms), 5000L));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        return "Ошибка подключения к LLM: " + std::string(curl_easy_strerror(res));
    }

    return response_body;
#else
    (void)url;
    (void)request_body;
    return "error: CURL is not available (built without libcurl)";
#endif
}

std::string LLMClient::make_get_request(const std::string& url,
                                        int timeout_ms) const {
    (void)url;
    (void)timeout_ms;

#if QORNIX_HAS_CURL
    std::string response_body;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "Ошибка: не удалось инициализировать CURL";
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (!config_.api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(static_cast<long>(timeout_ms), 5000L));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        return "Ошибка подключения к LLM: " + std::string(curl_easy_strerror(res));
    }

    return response_body;
#else
    return "error: CURL is not available (built without libcurl)";
#endif
}

// ============================================================================
// Main ask method
// ============================================================================

std::string LLMClient::ask(const std::string& question,
                           const std::string& context) const {
    return ask_with_metadata(question, context).answer;
}

LLMGenerationResult LLMClient::ask_with_metadata(const std::string& question,
                                                 const std::string& context) const {
    LLMGenerationResult result;

    if (!config_.enabled) {
        result.status = "unavailable";
        result.answer = "LLM не настроен. Используйте /api/health для проверки.";
        return result;
    }

#if !QORNIX_HAS_CURL
    (void)question;
    result.status = "unavailable";
    result.answer = "LLM недоступен: qornix_rag собран без libcurl. "
                    "Установите libcurl development package и пересоберите проект.\n\n" + context;
    return result;
#endif

    std::string prompt = build_prompt(context, question);
    std::string request_body = build_request_json(prompt);

    // Try primary endpoint
    std::string url = config_.api_url;
    // Ensure URL ends with /chat/completions (OpenAI-compatible) or /api/chat
    if (url.find("/chat/completions") == std::string::npos &&
        url.find("/api/chat") == std::string::npos &&
        url.find("/v1/chat") == std::string::npos) {
        // For Ollama, use /api/chat; for OpenAI-compatible, use /v1/chat/completions
        // Try to detect Ollama by default port
        if (url.find("11434") != std::string::npos) {
            url += "/api/chat";
        } else {
            url += "/v1/chat/completions";
        }
    }

    std::string raw_response = make_request(url, request_body, config_.request_timeout_ms);

    if (response_has_error(raw_response)) {
        // Try failover endpoints
        if (config_.failover_enabled && !config_.failover_endpoints.empty()) {
            for (const auto& endpoint : config_.failover_endpoints) {
                std::string failover_url = endpoint.url;
                if (failover_url.find("/chat/completions") == std::string::npos &&
                    failover_url.find("/api/chat") == std::string::npos) {
                    if (failover_url.find("11434") != std::string::npos) {
                        failover_url += "/api/chat";
                    } else {
                        failover_url += "/v1/chat/completions";
                    }
                }

                // Build request with failover model
                std::string failover_prompt = build_prompt(context, question);
                std::ostringstream failover_json;
                failover_json << "{\"model\":\"" << json_escape(endpoint.model) << "\","
                              << "\"max_tokens\":" << config_.max_tokens << ","
                              << "\"temperature\":" << std::fixed << std::setprecision(1)
                              << config_.temperature << ","
                              << "\"messages\":["
                              << "{\"role\":\"system\",\"content\":\""
                              << json_escape(config_.system_prompt) << "\"},"
                              << "{\"role\":\"user\",\"content\":\""
                              << json_escape(failover_prompt) << "\"}]"
                              << "}";

                raw_response = make_request(failover_url, failover_json.str(),
                                            config_.request_timeout_ms);
                if (!response_has_error(raw_response)) {
                    break; // Success with failover
                }
            }
        }

        if (response_has_error(raw_response)) {
            if (response_is_provider_error_json(raw_response)) {
                return parse_response_result(raw_response);
            }
            result.status = "unavailable";
            result.answer = "LLM недоступен. Вот релевантные фрагменты из кода:\n" + context;
            return result;
        }
    }

    return parse_response_result(raw_response);
}

// ============================================================================
// Health check
// ============================================================================

bool LLMClient::is_available() const {
#if !QORNIX_HAS_CURL
    return false;
#else
    return health_check() >= 0;
#endif
}

int LLMClient::provider_health_check() const {
#if !QORNIX_HAS_CURL
    return -1;
#endif

    if (!config_.enabled) {
        return -1;
    }

    std::string url = config_.api_url;
    auto start = std::chrono::steady_clock::now();

    if (is_ollama_url(url)) {
        url = ollama_base_url(url) + "/api/tags";
    } else {
        url = openai_models_url(url);
    }

    const std::string response = make_get_request(url, 3000);

    auto end = std::chrono::steady_clock::now();
    const long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    if (response_has_error(response)) {
        return -1;
    }

    return static_cast<int>(duration);
}

int LLMClient::health_check() const {
#if !QORNIX_HAS_CURL
    return -1;
#endif

    const int provider_duration = provider_health_check();
    if (provider_duration < 0) {
        return -1;
    }

    const auto models = list_available_models();
    if (!models.empty() && !model_list_contains(models, config_.model)) {
        return -1;
    }

    if (is_ollama_url(config_.api_url) && models.empty()) {
        return -1;
    }

    return provider_duration;
}

std::string LLMClient::provider_name() const {
    if (is_ollama_url(config_.api_url)) {
        return "ollama";
    }

    return "openai-compatible";
}

std::vector<std::string> LLMClient::list_available_models() const {
#if !QORNIX_HAS_CURL
    return {};
#else
    if (!config_.enabled) {
        return {};
    }

    const std::string url = is_ollama_url(config_.api_url)
        ? ollama_base_url(config_.api_url) + "/api/tags"
        : openai_models_url(config_.api_url);

    const std::string response = make_get_request(url, 3000);
    if (response_has_error(response)) {
        return {};
    }

    return parse_available_models_response(response);
#endif
}

std::vector<std::string> LLMClient::parse_available_models_response(const std::string& json_response) const {
    return parse_model_list_structured(json_response);
}

bool LLMClient::configured_model_available() const {
    if (!config_.enabled) {
        return false;
    }

    const auto models = list_available_models();
    return model_list_contains(models, config_.model);
}

// ============================================================================
// Streaming support
// ============================================================================

std::vector<std::string> LLMClient::ask_stream(const std::string& question,
                                               const std::string& context) const {
    std::vector<std::string> chunks;

#if QORNIX_HAS_CURL
    if (!config_.enabled) {
        chunks.push_back("LLM не настроен.");
        return chunks;
    }

    std::string prompt = build_prompt(context, question);
    std::string request_body = build_request_json(prompt, true);

    std::string url = config_.api_url;
    if (url.find("/chat/completions") == std::string::npos &&
        url.find("/api/chat") == std::string::npos) {
        if (url.find("11434") != std::string::npos) {
            url += "/api/chat";
        } else {
            url += "/v1/chat/completions";
        }
    }

    std::string raw_response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        chunks.push_back("Ошибка: CURL инициализация");
        return chunks;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (!config_.api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)config_.request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        chunks.push_back("Ошибка подключения к LLM: " + std::string(curl_easy_strerror(res)));
        return chunks;
    }

    chunks = parse_stream_chunks(raw_response);
    if (chunks.empty()) {
        const auto parsed = parse_response_result(raw_response);
        if (!parsed.answer.empty()) {
            chunks.push_back(parsed.answer);
        }
    }
    if (chunks.empty()) {
        chunks.push_back("Ошибка: не удалось распознать потоковый ответ");
    }

    return chunks;
#else
    (void)question;
    (void)context;
    chunks.push_back("Ошибка: CURL не доступен (сборка без libcurl)");
    return chunks;
#endif
}

// ============================================================================
// Phase 2: SSE Streaming with Callback
// ============================================================================

bool LLMClient::ask_stream_sse(const std::string& question,
                               const std::string& context,
                               SSECallback callback) const {
#if QORNIX_HAS_CURL
    if (!config_.enabled) {
        callback("LLM не настроен.", true);
        return false;
    }

    std::string prompt = build_templated_prompt(context, question);
    std::string request_body = build_request_json(prompt, true);

    std::string url = config_.api_url;
    if (url.find("/chat/completions") == std::string::npos &&
        url.find("/api/chat") == std::string::npos) {
        if (url.find("11434") != std::string::npos) {
            url += "/api/chat";
        } else {
            url += "/v1/chat/completions";
        }
    }

    std::string raw_response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        callback("Ошибка: CURL инициализация", true);
        return false;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (!config_.api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)config_.request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        callback("Ошибка подключения: " + std::string(curl_easy_strerror(res)), true);
        return false;
    }

    const auto chunks = parse_stream_chunks(raw_response);
    if (!chunks.empty()) {
        for (const auto& chunk : chunks) {
            callback(chunk, false);
        }
        callback("", true);
        return true;
    }

    const auto parsed = parse_response_result(raw_response);
    if (!parsed.answer.empty() && parsed.status != "parser_error") {
        callback(parsed.answer, false);
        callback("", true);
        return true;
    }

    callback(parsed.answer.empty() ? "Ошибка: не удалось распознать потоковый ответ" : parsed.answer,
             true);
    return false;
#else
    (void)question;
    (void)context;
    (void)callback;
    callback("Ошибка: CURL не доступен", true);
    return false;
#endif
}

// ============================================================================
// Phase 2: Token Counting
// ============================================================================

size_t LLMClient::estimate_tokens(const std::string& text) const {
    // Simple heuristic: ~4 chars per token for English, ~3 for Russian
    size_t russian_chars = 0;
    size_t total_chars = 0;

    for (char c : text) {
        total_chars++;
        // Simple heuristic: non-ASCII high-byte characters.
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0xc0) {
            russian_chars++;
        }
    }

    size_t english_chars = total_chars - russian_chars;
    size_t tokens = (english_chars / 4) + (russian_chars / 3);
    return std::max(size_t(1), tokens);
}

LLMTokenStats LLMClient::parse_token_stats(const std::string& json_response) const {
    LLMTokenStats stats;
    if (json_response.empty()) {
        return stats;
    }

    boost::system::error_code ec;
    json::value parsed = json::parse(json_response, ec);
    if (!ec) {
        collect_token_stats_from_value(parsed, stats);
        return stats;
    }

    for (const auto& line : json_payload_lines(json_response)) {
        ec = {};
        parsed = json::parse(line, ec);
        if (!ec) {
            collect_token_stats_from_value(parsed, stats);
        }
    }

    return stats;
}

std::vector<std::string> LLMClient::parse_stream_chunks(const std::string& stream_response) const {
    return parse_stream_chunks_structured(stream_response);
}

LLMTokenStats LLMClient::estimate_cost(const std::string& question,
                                       const std::string& context) const {
    LLMTokenStats stats;
    
    // Estimate prompt tokens
    std::string full_prompt = build_templated_prompt(context, question);
    stats.prompt_tokens = estimate_tokens(full_prompt);
    stats.completion_tokens = estimate_tokens("Пример ответа от модели на заданный вопрос.");
    stats.total_tokens = stats.prompt_tokens + stats.completion_tokens;

    // Rough cost estimation (varies by model)
    // Ollama/local: $0, OpenAI GPT-4: ~$0.03/1K tokens
    stats.estimated_cost_usd = (stats.total_tokens / 1000.0f) * 0.01f;

    return stats;
}

// ============================================================================
// Phase 2: Prompt Templating
// ============================================================================

std::string LLMClient::build_templated_prompt(const std::string& context,
                                              const std::string& question) const {
    return apply_prompt_template(config_.prompt_template, context, question);
}

// ============================================================================
// Phase 2: Improved Response Parsing
// ============================================================================

// (parse_response already exists, this is an enhanced version)
// The existing parse_response handles both Ollama and OpenAI formats

// ============================================================================
// Phase 3: Cache and Rate Limiting
// ============================================================================

void LLMClient::set_cache(std::shared_ptr<ICache> cache) {
    cache_ = cache;
}

std::shared_ptr<ICache> LLMClient::get_cache() const {
    return cache_;
}

CacheStats LLMClient::get_cache_stats() const {
    if (cache_) {
        return cache_->get_stats();
    }
    return CacheStats{};
}

void LLMClient::set_rate_limiter(std::shared_ptr<RateLimiter> limiter) {
    rate_limiter_ = limiter;
}

std::shared_ptr<RateLimiter> LLMClient::get_rate_limiter() const {
    return rate_limiter_;
}

RateLimiterStats LLMClient::get_rate_limiter_stats() const {
    if (rate_limiter_) {
        return rate_limiter_->get_stats();
    }
    return RateLimiterStats{};
}

std::string LLMClient::ask(const std::string& question,
                           const std::string& context,
                           const std::string& client_ip) const {
    return ask_with_metadata(question, context, client_ip).answer;
}

LLMGenerationResult LLMClient::ask_with_metadata(const std::string& question,
                                                 const std::string& context,
                                                 const std::string& client_ip) const {
    return ask_with_metadata(question, context, client_ip, std::nullopt, std::nullopt);
}

LLMGenerationResult LLMClient::ask_with_metadata(
    const std::string& question,
    const std::string& context,
    const std::string& client_ip,
    const std::optional<std::string>& system_prompt_override,
    const std::optional<std::string>& prompt_template_override) const {
    LLMGenerationResult result;

    // Phase 3: Check rate limiter
    if (rate_limiter_) {
        auto rejection = rate_limiter_->allow_request(client_ip);
        if (rejection) {
            result.status = "rate_limited";
            result.answer = "[Rate Limited] " + *rejection;
            return result;
        }
    }

    const std::string effective_system_prompt =
        system_prompt_override && !system_prompt_override->empty()
            ? *system_prompt_override
            : config_.system_prompt;
    const std::string effective_prompt_template =
        prompt_template_override && !prompt_template_override->empty()
            ? *prompt_template_override
            : config_.prompt_template;
    const std::string cache_context = context
        + "\n\n__qornix_prompt_template__\n" + effective_prompt_template
        + "\n\n__qornix_system_prompt__\n" + effective_system_prompt;

    // Phase 3: Check cache
    if (cache_) {
        std::string key = make_cache_key(question, cache_context, config_.model, config_.temperature);

        auto cached = cache_->get(key);
        if (cached.has_value()) {
            // Cache hit — return cached answer
            result.status = "cache_hit";
            result.answer = "[CACHE HIT] " + cached->answer;
            return result;
        }
    }

    // Ask LLM
    if (!config_.enabled) {
        result.status = "unavailable";
        result.answer = "LLM не настроен. Используйте /api/health для проверки.";
    } else {
#if !QORNIX_HAS_CURL
        result.status = "unavailable";
        result.answer = "LLM недоступен: qornix_rag собран без libcurl. "
                        "Установите libcurl development package и пересоберите проект.\n\n" + context;
#else
        std::string prompt = apply_prompt_template(effective_prompt_template, context, question);
        std::string request_body = build_request_json(prompt, false, effective_system_prompt);

        // Try primary endpoint
        std::string url = config_.api_url;
        if (url.find("/chat/completions") == std::string::npos &&
            url.find("/api/chat") == std::string::npos &&
            url.find("/v1/chat") == std::string::npos) {
            if (url.find("11434") != std::string::npos) {
                url += "/api/chat";
            } else {
                url += "/v1/chat/completions";
            }
        }

        std::string raw_response = make_request(url, request_body, config_.request_timeout_ms);

        if (response_has_error(raw_response)) {
            // Try failover endpoints
            if (config_.failover_enabled && !config_.failover_endpoints.empty()) {
                for (const auto& endpoint : config_.failover_endpoints) {
                    std::string failover_url = endpoint.url;
                    if (failover_url.find("/chat/completions") == std::string::npos &&
                        failover_url.find("/api/chat") == std::string::npos) {
                        if (failover_url.find("11434") != std::string::npos) {
                            failover_url += "/api/chat";
                        } else {
                            failover_url += "/v1/chat/completions";
                        }
                    }

                    std::string failover_prompt = prompt;
                    std::ostringstream failover_json;
                    failover_json << "{\"model\":\"" << json_escape(endpoint.model) << "\","
                                  << "\"max_tokens\":" << config_.max_tokens << ","
                                  << "\"temperature\":" << std::fixed << std::setprecision(1)
                                  << config_.temperature << ","
                                  << "\"messages\":["
                                  << "{\"role\":\"system\",\"content\":\""
                                  << json_escape(effective_system_prompt) << "\"},"
                                  << "{\"role\":\"user\",\"content\":\""
                                  << json_escape(failover_prompt) << "\"}]"
                                  << "}";

                    raw_response = make_request(failover_url, failover_json.str(),
                                                config_.request_timeout_ms);
                    if (!response_has_error(raw_response)) {
                        break;
                    }
                }
            }

            if (response_has_error(raw_response)) {
                if (response_is_provider_error_json(raw_response)) {
                    result = parse_response_result(raw_response);
                } else {
                    result.status = "unavailable";
                    result.answer = "LLM недоступен. Вот релевантные фрагменты из кода:\n" + context;
                }
            } else {
                result = parse_response_result(raw_response);
            }
        } else {
            result = parse_response_result(raw_response);
        }
#endif
    }

    // Phase 3: Store in cache
    if (cache_ && config_.enabled && result.status != "rate_limited") {
        CacheEntry entry;
        entry.answer = result.answer;
        entry.tokens_used = estimate_tokens(result.answer);
        entry.ttl = std::chrono::hours(1); // Default TTL
        entry.created_at = std::chrono::steady_clock::now();

        std::string key = make_cache_key(question, cache_context, config_.model, config_.temperature);
        cache_->put(key, entry);
    }

    return result;
}
