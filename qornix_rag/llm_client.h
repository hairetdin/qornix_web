/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * LLM Client for Qornix RAG
 *
 * OpenAI-compatible API client supporting Ollama, vLLM, LMStudio, Groq, etc.
 * Graceful degradation: works without LLM (returns search context only).
 *
 * Phase 3 features:
 *   - LLM response caching (memory/redis backend)
 *   - Rate limiting (sliding window algorithm)
 */

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <functional>

#if QORNIX_HAS_CURL
#include <curl/curl.h>
#endif

#if QORNIX_HAS_YAML
#include <yaml-cpp/yaml.h>
#endif

// Phase 3 includes
#include "llm_cache.h"
#include "rate_limiter.h"

struct LLMFailoverEndpoint {
    std::string url;
    std::string model;
};

struct LLMTokenStats {
    size_t prompt_tokens = 0;
    size_t completion_tokens = 0;
    size_t total_tokens = 0;
    float estimated_cost_usd = 0.0f;
};

struct LLMConfig {
    bool enabled = false;
    std::string api_url = "http://localhost:11434";
    std::string api_key = "";
    std::string model = "llama3";
    int max_tokens = 1024;
    float temperature = 0.7f;
    float top_p = 0.9f;
    int request_timeout_ms = 30000;
    std::string system_prompt = R"(Ты — помощник разработчика, который отвечает на вопросы
на основе предоставленного контекста из кода проекта.

Правила:
- Используй только информацию из контекста
- Если информации недостаточно, скажи "Недостаточно информации"
- Приводи примеры кода если это уместно
- Отвечай на русском языке
- Цитируй файлы из которых взята информация)";
    bool stream = false;
    bool failover_enabled = false;
    std::vector<LLMFailoverEndpoint> failover_endpoints;

    // Phase 2: Token estimation
    float chars_per_token = 4.0f; // Average chars per token (English)
    float chars_per_token_russian = 3.0f; // Average chars per token (Russian)

    // Phase 2: Prompt templating
    std::string prompt_template = "Контекст:\n{context}\n\nВопрос: {question}";
    std::string answer_prefix = "Ответ: ";
};

class LLMClient {
private:
    LLMConfig config_;
    std::string config_path_;
    int current_endpoint_index_ = 0;

   // Simple JSON helpers (no external JSON library needed)
    static std::string json_escape(const std::string& s);
    static std::string build_messages_json(const std::string& system_prompt,
                                           const std::string& user_message);
#if QORNIX_HAS_CURL
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, std::string* out);
    static size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, std::vector<std::string>* out);
#endif

    // Load config from YAML file
    void load_config(const std::string& config_path);

    // Parse LLM section from flattened config map
    void parse_config_map(const std::unordered_map<std::string, std::string>& config);

    // Make HTTP request using curl
    std::string make_request(const std::string& url,
                            const std::string& request_body,
                            int timeout_ms) const;

    // Check if LLM is available (health check)
    bool check_llm_available() const;

public:
    explicit LLMClient(const std::string& config_path = "");
    ~LLMClient() = default;

    // Non-copyable
    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;

   // Config accessors
    const LLMConfig& get_config() const { return config_; }
    
    // SSE helper (public for web.h)
    static std::string json_escape_chunk(const std::string& s);
    std::string get_api_url() const { return config_.api_url; }
    std::string get_api_key() const { return config_.api_key; }
    std::string get_model() const { return config_.model; }
    int get_max_tokens() const { return config_.max_tokens; }
    float get_temperature() const { return config_.temperature; }
    float get_top_p() const { return config_.top_p; }
    int get_request_timeout_ms() const { return config_.request_timeout_ms; }
    bool is_enabled() const { return config_.enabled; }
    bool is_failover_enabled() const { return config_.failover_enabled; }
    const std::vector<LLMFailoverEndpoint>& get_failover_endpoints() const {
        return config_.failover_endpoints;
    }

    // Prompt building
    std::string build_prompt(const std::string& context, const std::string& question) const;
    std::string build_request_json(const std::string& prompt) const;

    // Response parsing
    std::string parse_response(const std::string& json_response) const;

    // Main ask method
    std::string ask(const std::string& question,
                   const std::string& context = "") const;

    // Health check
    bool is_available() const;
    int health_check() const; // returns response time in ms, or -1 if unavailable

    // Streaming support (Phase 2)
    std::vector<std::string> ask_stream(const std::string& question,
                                        const std::string& context = "") const;
    
    // SSE streaming with callback (Phase 2)
    // callback(chunk, is_done) — call for each chunk
    using SSECallback = std::function<void(const std::string& chunk, bool is_done)>;
    bool ask_stream_sse(const std::string& question,
                       const std::string& context,
                       SSECallback callback) const;
    
    // Token counting (Phase 2)
    size_t estimate_tokens(const std::string& text) const;
    LLMTokenStats estimate_cost(const std::string& question,
                                const std::string& context) const;
    
    // Prompt templating (Phase 2)
    std::string build_templated_prompt(const std::string& context,
                                       const std::string& question) const;
    
    // Response metadata (Phase 2)
    LLMTokenStats parse_token_stats(const std::string& json_response) const;

    // Phase 3: Cache management
    void set_cache(std::shared_ptr<ICache> cache);
    std::shared_ptr<ICache> get_cache() const;
    CacheStats get_cache_stats() const;

    // Phase 3: Rate limiting
    void set_rate_limiter(std::shared_ptr<RateLimiter> limiter);
    std::shared_ptr<RateLimiter> get_rate_limiter() const;
    RateLimiterStats get_rate_limiter_stats() const;

    // Phase 3: Ask with cache and rate limiting
    std::string ask(const std::string& question,
                   const std::string& context = "",
                   const std::string& client_ip = "") const;

private:
    // Phase 3: Cache and rate limiter
    std::shared_ptr<ICache> cache_;
    std::shared_ptr<RateLimiter> rate_limiter_;
};
