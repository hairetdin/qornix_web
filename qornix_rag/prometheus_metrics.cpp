/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "prometheus_metrics.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>

// ============================================================================
// CounterMetric
// ============================================================================

void CounterMetric::inc(double amount) {
    value_.fetch_add(amount, std::memory_order_relaxed);
}

double CounterMetric::get() const {
    return value_.load(std::memory_order_relaxed);
}

std::string CounterMetric::render(const std::string& name,
                                   const std::vector<std::string>& labels,
                                   const std::vector<std::string>& values) const {
    std::ostringstream ss;
    ss << "# HELP " << name << " Total count of events\n";
    ss << "# TYPE " << name << " counter\n";

    if (labels.empty()) {
        ss << name << " " << std::fixed << std::setprecision(1) << value_.load() << "\n";
    } else {
        ss << name << "{";
        for (size_t i = 0; i < labels.size(); i++) {
            if (i > 0) ss << ",";
            ss << labels[i] << "=\"" << values[i] << "\"";
        }
        ss << "} " << std::fixed << std::setprecision(1) << value_.load() << "\n";
    }
    return ss.str();
}

// ============================================================================
// GaugeMetric
// ============================================================================

void GaugeMetric::set(double v) {
    value_.store(v, std::memory_order_relaxed);
}

void GaugeMetric::inc(double amount) {
    value_.fetch_add(amount, std::memory_order_relaxed);
}

void GaugeMetric::dec(double amount) {
    value_.fetch_sub(amount, std::memory_order_relaxed);
}

double GaugeMetric::get() const {
    return value_.load(std::memory_order_relaxed);
}

std::string GaugeMetric::render(const std::string& name,
                                 const std::vector<std::string>& labels,
                                 const std::vector<std::string>& values) const {
    std::ostringstream ss;
    ss << "# HELP " << name << " Current value\n";
    ss << "# TYPE " << name << " gauge\n";

    if (labels.empty()) {
        ss << name << " " << std::fixed << std::setprecision(1) << value_.load() << "\n";
    } else {
        ss << name << "{";
        for (size_t i = 0; i < labels.size(); i++) {
            if (i > 0) ss << ",";
            ss << labels[i] << "=\"" << values[i] << "\"";
        }
        ss << "} " << std::fixed << std::setprecision(1) << value_.load() << "\n";
    }
    return ss.str();
}

// ============================================================================
// SummaryMetric
// ============================================================================

void SummaryMetric::observe(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    sum_ += value;
    count_ += 1.0;
    if (value < min_) min_ = value;
    if (value > max_) max_ = value;
}

double SummaryMetric::get_sum() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sum_;
}

double SummaryMetric::get_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

double SummaryMetric::get_avg() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ > 0 ? sum_ / count_ : 0.0;
}

std::string SummaryMetric::render(const std::string& name,
                                   const std::vector<std::string>& labels,
                                   const std::vector<std::string>& values) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream ss;
    ss << "# HELP " << name << " Latency summary\n";
    ss << "# TYPE " << name << " summary\n";

    auto render_labels = [&](const std::string& /*suffix*/) {
        if (labels.empty()) {
            return std::string{};
        }
        std::ostringstream lbl;
        lbl << "{";
        for (size_t i = 0; i < labels.size(); i++) {
            if (i > 0) lbl << ",";
            lbl << labels[i] << "=\"" << values[i] << "\"";
        }
        lbl << "}";
        return lbl.str();
    };

    std::string lbl = render_labels("");
    ss << name << lbl << "_sum " << std::fixed << std::setprecision(6) << sum_ << "\n";
    ss << name << lbl << "_count " << count_ << "\n";

    return ss.str();
}

// ============================================================================
// MetricsRegistry
// ============================================================================

std::shared_ptr<CounterMetric> MetricsRegistry::get_or_create_counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    if (it != counters_.end()) return it->second;
    auto metric = std::make_shared<CounterMetric>();
    counters_[name] = metric;
    return metric;
}

std::shared_ptr<GaugeMetric> MetricsRegistry::get_or_create_gauge(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    if (it != gauges_.end()) return it->second;
    auto metric = std::make_shared<GaugeMetric>();
    gauges_[name] = metric;
    return metric;
}

std::shared_ptr<SummaryMetric> MetricsRegistry::get_or_create_summary(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = summaries_.find(name);
    if (it != summaries_.end()) return it->second;
    auto metric = std::make_shared<SummaryMetric>();
    summaries_[name] = metric;
    return metric;
}

std::string MetricsRegistry::render_all() const {
    std::ostringstream ss;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [name, metric] : counters_) ss << metric->render(name);
    for (const auto& [name, metric] : gauges_) ss << metric->render(name);
    for (const auto& [name, metric] : summaries_) ss << metric->render(name);
    return ss.str();
}

std::string MetricsRegistry::render_one(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto cit = counters_.find(name);
    if (cit != counters_.end()) return cit->second->render(name);
    auto git = gauges_.find(name);
    if (git != gauges_.end()) return git->second->render(name);
    auto sit = summaries_.find(name);
    if (sit != summaries_.end()) return sit->second->render(name);
    return "# No metric found: " + name + "\n";
}

void MetricsRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.clear();
    gauges_.clear();
    summaries_.clear();
}

// ============================================================================
// LLMRAGMetrics
// ============================================================================

LLMRAGMetrics::LLMRAGMetrics(std::shared_ptr<MetricsRegistry> registry)
    : registry_(registry ? std::move(registry) : std::make_shared<MetricsRegistry>()) {
}

void LLMRAGMetrics::inc_llm_requests() {
    registry_->get_or_create_counter("qornix_rag_llm_requests_total")->inc();
}

void LLMRAGMetrics::inc_llm_requests_failed() {
    registry_->get_or_create_counter("qornix_rag_llm_requests_failed_total")->inc();
}

void LLMRAGMetrics::inc_llm_tokens(size_t count) {
    registry_->get_or_create_counter("qornix_rag_llm_tokens_total")->inc(static_cast<double>(count));
}

void LLMRAGMetrics::observe_llm_request_duration_seconds(double seconds) {
    registry_->get_or_create_summary("qornix_rag_llm_request_duration_seconds")->observe(seconds);
}

void LLMRAGMetrics::inc_cache_hits() {
    registry_->get_or_create_counter("qornix_rag_cache_hits_total")->inc();
}

void LLMRAGMetrics::inc_cache_misses() {
    registry_->get_or_create_counter("qornix_rag_cache_misses_total")->inc();
}

void LLMRAGMetrics::set_cache_size(size_t size) {
    registry_->get_or_create_gauge("qornix_rag_cache_size_gauge")->set(static_cast<double>(size));
}

void LLMRAGMetrics::inc_rate_limit_rejections() {
    registry_->get_or_create_counter("qornix_rag_rate_limit_rejections_total")->inc();
}

void LLMRAGMetrics::inc_batch_questions(size_t count) {
    registry_->get_or_create_counter("qornix_rag_batch_questions_total")->inc(static_cast<double>(count));
}

void LLMRAGMetrics::inc_batch_completed(size_t count) {
    registry_->get_or_create_counter("qornix_rag_batch_completed_total")->inc(static_cast<double>(count));
}

void LLMRAGMetrics::set_indexed_files(size_t count) {
    registry_->get_or_create_gauge("qornix_rag_indexed_files_count")->set(static_cast<double>(count));
}

void LLMRAGMetrics::set_indexed_lines(size_t count) {
    registry_->get_or_create_gauge("qornix_rag_indexed_lines_count")->set(static_cast<double>(count));
}

std::string LLMRAGMetrics::render_all() const {
    return registry_->render_all();
}
