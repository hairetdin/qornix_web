/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "llm_client.h"
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

bool response_has_error(const std::string& response) {
    return response.empty() ||
           response.find("error") != std::string::npos ||
           response.find("Error") != std::string::npos ||
           response.find("Ошибка") != std::string::npos ||
           response.find("недоступ") != std::string::npos;
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

bool ollama_tags_contain_model(const std::string& response,
                               const std::string& model) {
    if (model.empty()) {
        return true;
    }

    if (response.find("\"name\":\"" + model + "\"") != std::string::npos ||
        response.find("\"name\":\"" + model + ":latest\"") != std::string::npos) {
        return true;
    }

    // Fallback for pretty-printed JSON or model tags such as llama3:8b.
    return response.find(model + ":") != std::string::npos;
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

std::vector<std::string> extract_json_string_values(const std::string& json,
                                                    const std::vector<std::string>& keys) {
    std::vector<std::string> values;

    for (const auto& key : keys) {
        std::size_t pos = 0;
        const std::string needle = "\"" + key + "\"";

        while ((pos = json.find(needle, pos)) != std::string::npos) {
            const std::size_t colon = json.find(':', pos + needle.size());
            if (colon == std::string::npos) {
                break;
            }

            const std::size_t value_start = json.find('"', colon + 1);
            if (value_start == std::string::npos) {
                pos = colon + 1;
                continue;
            }

            const std::size_t value_end = json.find('"', value_start + 1);
            if (value_end == std::string::npos) {
                break;
            }

            std::string value = json.substr(value_start + 1, value_end - value_start - 1);
            if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }

            pos = value_end + 1;
        }
    }

    return values;
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


bool parse_json_string_at(const std::string& json,
                          std::size_t quote_pos,
                          std::string& value,
                          std::size_t* end_pos = nullptr) {
    if (quote_pos >= json.size() || json[quote_pos] != '"') {
        return false;
    }

    value.clear();
    value.reserve(64);

    for (std::size_t i = quote_pos + 1; i < json.size(); ++i) {
        const char c = json[i];

        if (c == '"') {
            if (end_pos) {
                *end_pos = i;
            }
            return true;
        }

        if (c != '\\') {
            value += c;
            continue;
        }

        if (i + 1 >= json.size()) {
            return false;
        }

        const char esc = json[++i];
        switch (esc) {
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '/': value += '/'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case 'u': {
                // Keep unicode escapes intact. Ollama/OpenAI usually return UTF-8
                // directly for Cyrillic text, while preserving the escape avoids
                // corrupting non-ASCII code points without adding a JSON library.
                if (i + 4 >= json.size()) {
                    return false;
                }
                value += "\\u";
                value.append(json, i + 1, 4);
                i += 4;
                break;
            }
            default:
                // Be permissive: preserve unknown escapes instead of truncating.
                value += esc;
                break;
        }
    }

    return false;
}

std::vector<std::string> extract_json_string_field_values(const std::string& json,
                                                          const std::string& key) {
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;

    while ((pos = json.find(needle, pos)) != std::string::npos) {
        const std::size_t colon = json.find(':', pos + needle.size());
        if (colon == std::string::npos) {
            break;
        }

        std::size_t value_pos = colon + 1;
        while (value_pos < json.size() &&
               (json[value_pos] == ' ' || json[value_pos] == '\t' ||
                json[value_pos] == '\r' || json[value_pos] == '\n')) {
            ++value_pos;
        }

        if (value_pos >= json.size()) {
            break;
        }

        if (json[value_pos] != '"') {
            // The field may be null or an object; skip this occurrence.
            pos = value_pos + 1;
            continue;
        }

        std::string value;
        std::size_t value_end = value_pos;
        if (!parse_json_string_at(json, value_pos, value, &value_end)) {
            break;
        }

        if (!value.empty()) {
            values.push_back(std::move(value));
        }

        pos = value_end + 1;
    }

    return values;
}

std::string join_json_chunks(const std::vector<std::string>& chunks) {
    std::string result;
    for (const auto& chunk : chunks) {
        result += chunk;
    }
    return result;
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
    std::ostringstream prompt;
    prompt << "Контекст:\n";
    if (!context.empty()) {
        prompt << context;
    } else {
        prompt << "(Нет доступного контекста)";
    }
    prompt << "\n\nВопрос: " << question;
    return prompt.str();
}

std::string LLMClient::build_request_json(const std::string& prompt) const {
    std::ostringstream json;

    if (is_ollama_url(config_.api_url)) {
        json << "{\"model\":\"" << json_escape(config_.model) << "\","
             << "\"stream\":false,"
             << "\"messages\":[";

        json << "{\"role\":\"system\",\"content\":\""
             << json_escape(config_.system_prompt) << "\"},";

        json << "{\"role\":\"user\",\"content\":\""
             << json_escape(prompt) << "\"}],";

        json << "\"options\":{"
             << "\"temperature\":" << std::fixed << std::setprecision(1)
             << config_.temperature << ","
             << "\"top_p\":" << std::fixed << std::setprecision(1)
             << config_.top_p << ","
             << "\"num_predict\":" << config_.max_tokens
             << "}}";
        return json.str();
    }

    json << "{\"model\":\"" << json_escape(config_.model) << "\","
         << "\"max_tokens\":" << config_.max_tokens << ","
         << "\"temperature\":" << std::fixed << std::setprecision(1)
         << config_.temperature << ","
         << "\"messages\":[";

    // System message
    json << "{\"role\":\"system\",\"content\":\""
         << json_escape(config_.system_prompt) << "\"},";

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
    if (json_response.empty()) {
        return "Ошибка: пустой ответ от LLM";
    }

    // Ollama /api/chat and OpenAI-compatible chat completions both expose the
    // generated text as a JSON string field named "content". The response may
    // contain escaped quotes, backslashes, or newlines in code snippets, so this
    // must not be parsed with a plain find('"') after the field start.
    const auto content_chunks = extract_json_string_field_values(json_response, "content");
    if (!content_chunks.empty()) {
        return join_json_chunks(content_chunks);
    }

    // Ollama /api/generate uses the top-level "response" string. If streaming
    // accidentally remains enabled, several JSON objects can be returned as
    // newline-delimited chunks; joining all response fields preserves the answer.
    const auto response_chunks = extract_json_string_field_values(json_response, "response");
    if (!response_chunks.empty()) {
        return join_json_chunks(response_chunks);
    }

    // Provider error payloads are not standardized. Surface the message when it
    // is a JSON string instead of hiding the useful diagnostic from the user.
    const auto error_messages = extract_json_string_field_values(json_response, "error");
    if (!error_messages.empty()) {
        return "Ошибка LLM: " + error_messages.front();
    }

    const auto message_values = extract_json_string_field_values(json_response, "message");
    if (!message_values.empty()) {
        return "Ошибка LLM: " + message_values.front();
    }

    return "Ошибка: не удалось распознать формат ответа LLM";
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
    if (!config_.enabled) {
        return "LLM не настроен. Используйте /api/health для проверки.";
    }

#if !QORNIX_HAS_CURL
    (void)question;
    return "LLM недоступен: qornix_rag собран без libcurl. "
           "Установите libcurl development package и пересоберите проект.\n\n" + context;
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
            return "LLM недоступен. Вот релевантные фрагменты из кода:\n" + context;
        }
    }

    return parse_response(raw_response);
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

int LLMClient::health_check() const {
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
        const std::string response = make_get_request(url, 3000);

        auto end = std::chrono::steady_clock::now();
        const long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();

        if (response_has_error(response) ||
            !ollama_tags_contain_model(response, config_.model)) {
            return -1;
        }

        return static_cast<int>(duration);
    }

    url = openai_models_url(url);

    const std::string response = make_get_request(url, 3000);

    auto end = std::chrono::steady_clock::now();
    const long long duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    if (response_has_error(response)) {
        return -1;
    }

    const auto models = extract_json_string_values(response, {"id", "name", "model"});
    if (!models.empty() && !model_list_contains(models, config_.model)) {
        return -1;
    }

    return static_cast<int>(duration);
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

    return extract_json_string_values(response, {"name", "model", "id"});
#endif
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
    std::string request_body = build_request_json(prompt);

    // Enable streaming in request
    request_body += ",\"stream\":true}"; // Replace closing brace

    std::string url = config_.api_url;
    if (url.find("/chat/completions") == std::string::npos &&
        url.find("/api/chat") == std::string::npos) {
        if (url.find("11434") != std::string::npos) {
            url += "/api/chat";
        } else {
            url += "/v1/chat/completions";
        }
    }

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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunks);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)config_.request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        chunks.clear();
        chunks.push_back("Ошибка подключения к LLM: " + std::string(curl_easy_strerror(res)));
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
    std::string request_body = build_request_json(prompt);
    request_body += ",\"stream\":true}";

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

    // Parse SSE/JSON stream
    bool found_content = false;
    std::string full_answer;
    
    // Try to find content in JSON response
    size_t content_pos = raw_response.find("\"content\"");
    if (content_pos != std::string::npos) {
        size_t colon_pos = raw_response.find(':', content_pos + 9);
        if (colon_pos != std::string::npos) {
            size_t value_start = raw_response.find('"', colon_pos + 1);
            if (value_start != std::string::npos) {
                size_t value_end = raw_response.find('"', value_start + 1);
                if (value_end != std::string::npos) {
                    full_answer = raw_response.substr(value_start + 1,
                                                      value_end - value_start - 1);
                    found_content = true;
                }
            }
        }
    }

    if (found_content) {
        // Stream the answer in chunks (word by word)
        std::istringstream stream(full_answer);
        std::string word;
        std::string chunk;
        
        while (stream >> word) {
            chunk += (chunk.empty() ? "" : " ") + word;
            if (chunk.size() >= 20 || stream.eof()) {
                callback(chunk, false);
                chunk.clear();
            }
        }
        if (!chunk.empty()) {
            callback(chunk, false);
        }
        callback("", true); // Done signal
    } else {
        callback("Ошибка: не удалось распознать ответ", true);
        return false;
    }

    return true;
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

    // Try to extract usage from OpenAI format
    // {"usage": {"prompt_tokens": 100, "completion_tokens": 50, "total_tokens": 150}}
    size_t usage_pos = json_response.find("\"usage\"");
    if (usage_pos != std::string::npos) {
        size_t prompt_pos = json_response.find("\"prompt_tokens\"", usage_pos);
        if (prompt_pos != std::string::npos) {
            size_t colon_pos = json_response.find(':', prompt_pos + 15);
            if (colon_pos != std::string::npos) {
                size_t num_start = json_response.find_first_of("0123456789", colon_pos + 1);
                if (num_start != std::string::npos) {
                    stats.prompt_tokens = std::stoul(json_response.substr(num_start));
                }
            }
        }

        size_t completion_pos = json_response.find("\"completion_tokens\"", usage_pos);
        if (completion_pos != std::string::npos) {
            size_t colon_pos = json_response.find(':', completion_pos + 19);
            if (colon_pos != std::string::npos) {
                size_t num_start = json_response.find_first_of("0123456789", colon_pos + 1);
                if (num_start != std::string::npos) {
                    stats.completion_tokens = std::stoul(json_response.substr(num_start));
                }
            }
        }

        size_t total_pos = json_response.find("\"total_tokens\"", usage_pos);
        if (total_pos != std::string::npos) {
            size_t colon_pos = json_response.find(':', total_pos + 14);
            if (colon_pos != std::string::npos) {
                size_t num_start = json_response.find_first_of("0123456789", colon_pos + 1);
                if (num_start != std::string::npos) {
                    stats.total_tokens = std::stoul(json_response.substr(num_start));
                }
            }
        }
    }

    // If no usage info, estimate
    if (stats.prompt_tokens == 0 && stats.completion_tokens == 0) {
        stats.total_tokens = 0;
    }

    return stats;
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
    std::string template_str = config_.prompt_template;
    
    // Replace {context} and {question} placeholders
    size_t pos = 0;
    
    pos = template_str.find("{context}");
    if (pos != std::string::npos) {
        template_str.replace(pos, 9, context.empty() ? "(Нет контекста)" : context);
    }
    
    pos = template_str.find("{question}");
    if (pos != std::string::npos) {
        template_str.replace(pos, 10, question);
    }
    
    return template_str;
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
    // Phase 3: Check rate limiter
    if (rate_limiter_) {
        auto rejection = rate_limiter_->allow_request(client_ip);
        if (rejection) {
            return "[Rate Limited] " + *rejection;
        }
    }

    // Phase 3: Check cache
    if (cache_) {
        std::string key = make_cache_key(question, context, config_.model, config_.temperature);

        auto cached = cache_->get(key);
        if (cached.has_value()) {
            // Cache hit — return cached answer
            return "[CACHE HIT] " + cached->answer;
        }
    }

    // Ask LLM
    std::string answer;

    if (!config_.enabled) {
        answer = "LLM не настроен. Используйте /api/health для проверки.";
    } else {
#if !QORNIX_HAS_CURL
        answer = "LLM недоступен: qornix_rag собран без libcurl. "
                 "Установите libcurl development package и пересоберите проект.\n\n" + context;
#else
        std::string prompt = build_prompt(context, question);
        std::string request_body = build_request_json(prompt);

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
                        break;
                    }
                }
            }

            if (response_has_error(raw_response)) {
                answer = "LLM недоступен. Вот релевантные фрагменты из кода:\n" + context;
            } else {
                answer = parse_response(raw_response);
            }
        } else {
            answer = parse_response(raw_response);
        }
#endif
    }

    // Phase 3: Store in cache
    if (cache_ && config_.enabled && answer.find("[Rate Limited]") == std::string::npos) {
        CacheEntry entry;
        entry.answer = answer;
        entry.tokens_used = estimate_tokens(answer);
        entry.ttl = std::chrono::hours(1); // Default TTL
        entry.created_at = std::chrono::steady_clock::now();

        std::string key = make_cache_key(question, context, config_.model, config_.temperature);
        cache_->put(key, entry);
    }

    return answer;
}
