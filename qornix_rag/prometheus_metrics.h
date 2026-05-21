/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Prometheus Metrics — Phase 3
 *
 * In-memory metrics collector for LLM/RAG system monitoring.
 * Exposes /metrics endpoint in Prometheus text exposition format.
 *
 * No external prometheus-cpp dependency required.
 *
 * Metrics exposed:
 *   - qornix_rag_llm_requests_total (counter)
 *   - qornix_rag_llm_request_duration_seconds (summary)
 *   - qornix_rag_llm_tokens_total (counter)
 *   - qornix_rag_cache_hits_total (counter)
 *   - qornix_rag_cache_misses_total (counter)
 *   - qornix_rag_cache_size_gauge (gauge)
 *   - qornix_rag_rate_limit_rejections_total (counter)
 *   - qornix_rag_batch_questions_total (counter)
 *   - qornix_rag_batch_completed_total (counter)
 *   - qornix_rag_indexed_files_count (gauge)
 *   - qornix_rag_indexed_lines_count (gauge)
 */

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>
#include <vector>
#include <sstream>
#include <iomanip>
#include <limits>

// ============================================================================
// Metric types
// ============================================================================

enum class MetricType {
    COUNTER,
    GAUGE,
    SUMMARY
};

// ============================================================================
// Counter metric
// ============================================================================

class CounterMetric {
private:
    std::atomic<double> value_{0.0};

public:
    void inc(double amount = 1.0);
    double get() const;
    std::string render(const std::string& name,
                       const std::vector<std::string>& labels = {},
                       const std::vector<std::string>& values = {}) const;
};

// ============================================================================
// Gauge metric
// ============================================================================

class GaugeMetric {
private:
    std::atomic<double> value_{0.0};

public:
    void set(double v);
    void inc(double amount = 1.0);
    void dec(double amount = 1.0);
    double get() const;
    std::string render(const std::string& name,
                       const std::vector<std::string>& labels = {},
                       const std::vector<std::string>& values = {}) const;
};

// ============================================================================
// Summary metric (simple, no histogram buckets)
// ============================================================================

class SummaryMetric {
private:
    mutable std::mutex mutex_;
    double sum_ = 0.0;
    double count_ = 0.0;
    double min_ = std::numeric_limits<double>::max();
    double max_ = 0.0;

public:
    void observe(double value);
    double get_sum() const;
    double get_count() const;
    double get_avg() const;
    std::string render(const std::string& name,
                       const std::vector<std::string>& labels = {},
                       const std::vector<std::string>& values = {}) const;
};

// ============================================================================
// Metrics registry
// ============================================================================

class MetricsRegistry {
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<CounterMetric>> counters_;
    std::unordered_map<std::string, std::shared_ptr<GaugeMetric>> gauges_;
    std::unordered_map<std::string, std::shared_ptr<SummaryMetric>> summaries_;

public:
    std::shared_ptr<CounterMetric> get_or_create_counter(const std::string& name);
    std::shared_ptr<GaugeMetric> get_or_create_gauge(const std::string& name);
    std::shared_ptr<SummaryMetric> get_or_create_summary(const std::string& name);
    std::string render_all() const;
    std::string render_one(const std::string& name) const;
    void clear();
};

// ============================================================================
// Convenience: pre-built metrics for LLM/RAG system
// ============================================================================

class LLMRAGMetrics {
private:
    std::shared_ptr<MetricsRegistry> registry_;

public:
    explicit LLMRAGMetrics(std::shared_ptr<MetricsRegistry> registry = nullptr);

    // LLM request counters
    void inc_llm_requests();
    void inc_llm_requests_failed();
    void inc_llm_tokens(size_t count);

    // LLM latency
    void observe_llm_request_duration_seconds(double seconds);

    // Cache metrics
    void inc_cache_hits();
    void inc_cache_misses();
    void set_cache_size(size_t size);

    // Rate limiter metrics
    void inc_rate_limit_rejections();

    // Batch metrics
    void inc_batch_questions(size_t count);
    void inc_batch_completed(size_t count);

    // RAG indexing metrics
    void set_indexed_files(size_t count);
    void set_indexed_lines(size_t count);

    // Render all metrics
    std::string render_all() const;

    // Get registry
    std::shared_ptr<MetricsRegistry> get_registry() const { return registry_; }
};
