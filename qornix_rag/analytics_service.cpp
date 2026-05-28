/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "analytics_service.h"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <iostream>

// ============================================================================
// AnalyticsService implementation
// ============================================================================

AnalyticsService::AnalyticsService()
    : AnalyticsService(Config{}) {
}

AnalyticsService::AnalyticsService(Config config)
    : config_(std::move(config)) {
}

void AnalyticsService::logSearch(const SearchQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);

    log_.push_back(query);
    updateQueryCounts(query);

    // Prune if over limit
    if (log_.size() > config_.max_log_entries) {
        size_t remove_count = log_.size() - config_.max_log_entries / 2;
        log_.erase(log_.begin(), log_.begin() + remove_count);
        pruneQueryCounts();
    }
}

void AnalyticsService::logFeedback(const FeedbackEntry& feedback) {
    std::lock_guard<std::mutex> lock(mutex_);
    feedback_log_.push_back(feedback);
    if (feedback_log_.size() > config_.max_log_entries) {
        size_t remove_count = feedback_log_.size() - config_.max_log_entries / 2;
        feedback_log_.erase(feedback_log_.begin(), feedback_log_.begin() + remove_count);
    }
}

std::vector<AnalyticsService::FeedbackEntry> AnalyticsService::getFeedbackLog(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FeedbackEntry> result;
    size_t start = feedback_log_.size() > limit ? feedback_log_.size() - limit : 0;
    for (size_t i = start; i < feedback_log_.size(); ++i) {
        result.push_back(feedback_log_[i]);
    }
    return result;
}

AnalyticsService::AnalyticsReport AnalyticsService::getReport(
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) {

    std::lock_guard<std::mutex> lock(mutex_);

    AnalyticsReport report;
    report.total_queries = 0;
    report.queries_with_results = 0;
    report.queries_without_results = 0;
    report.answer_rate_percent = 0.0f;
    report.avg_response_time_ms = 0.0f;
    report.median_response_time_ms = 0.0f;

    // Filter queries by time range
    std::vector<SearchQuery> filtered;
    for (const auto& query : log_) {
        if (query.timestamp >= from && query.timestamp <= to) {
            filtered.push_back(query);
        }
    }

    report.total_queries = filtered.size();

    auto collect_feedback = [&]() {
        for (const auto& feedback : feedback_log_) {
            if (feedback.timestamp >= from && feedback.timestamp <= to) {
                report.feedback_total++;
                if (feedback.rating == "helpful" || feedback.rating == "positive" || feedback.rating == "up") {
                    report.feedback_helpful++;
                } else if (feedback.rating == "not_helpful" || feedback.rating == "negative" || feedback.rating == "down") {
                    report.feedback_not_helpful++;
                }
                if (!feedback.category.empty()) {
                    report.feedback_by_category[feedback.category]++;
                }
            }
        }
    };

    if (filtered.empty()) {
        collect_feedback();
        return report;
    }

    // Calculate statistics
    for (const auto& query : filtered) {
        if (query.has_answer) {
            report.queries_with_results++;
        } else {
            report.queries_without_results++;
        }
    }

    report.answer_rate_percent = static_cast<float>(report.queries_with_results)
                                  / static_cast<float>(report.total_queries) * 100.0f;

    // Average response time
    double total_time = 0.0;
    std::vector<int> response_times;
    for (const auto& query : filtered) {
        total_time += query.response_time_ms;
        response_times.push_back(query.response_time_ms);
    }
    report.avg_response_time_ms = static_cast<float>(total_time / response_times.size());

    // Median response time
    std::sort(response_times.begin(), response_times.end());
    size_t mid = response_times.size() / 2;
    if (response_times.size() % 2 == 0) {
        report.median_response_time_ms = static_cast<float>(
            (response_times[mid - 1] + response_times[mid]) / 2);
    } else {
        report.median_response_time_ms = static_cast<float>(response_times[mid]);
    }

    // Top queries
    std::vector<std::pair<std::string, size_t>> sorted_counts(
        query_counts_.begin(), query_counts_.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    size_t top_n = std::min(config_.top_n, sorted_counts.size());
    for (size_t i = 0; i < top_n; ++i) {
        AnalyticsReport::TopQuery tq;
        tq.query = sorted_counts[i].first;
        tq.count = sorted_counts[i].second;
        report.top_queries.push_back(tq);
    }

    // Missing answers
    if (config_.enable_missing_detection) {
        report.missing_answers = extractMissingQueries();
    }

    // Daily trends
    if (config_.enable_daily_trends) {
        report.daily_trends = calculateDailyTrends(from, to);
    }

    // Knowledge gaps
    report.knowledge_gaps = calculateKnowledgeGaps();

    collect_feedback();

    return report;
}

AnalyticsService::AnalyticsReport AnalyticsService::getRecentReport() {
    auto to = std::chrono::system_clock::now();
    auto from = to - std::chrono::days(30);
    return getReport(from, to);
}

std::vector<AnalyticsService::KnowledgeGap> AnalyticsService::getKnowledgeGaps(size_t min_search_count) {
    if (min_search_count == 0) {
        min_search_count = config_.gap_min_search_count;
    }

    auto report = getRecentReport();
    std::vector<KnowledgeGap> filtered;
    for (const auto& gap : report.knowledge_gaps) {
        if (gap.search_count >= min_search_count) {
            filtered.push_back(gap);
        }
    }
    return filtered;
}

std::vector<AnalyticsService::SearchQuery> AnalyticsService::getSearchLog(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<SearchQuery> result;
    size_t start = log_.size() > limit ? log_.size() - limit : 0;
    for (size_t i = start; i < log_.size(); ++i) {
        result.push_back(log_[i]);
    }
    return result;
}

std::vector<AnalyticsService::MissingAnswer> AnalyticsService::getMissingAnswers(size_t min_count) {
    auto report = getRecentReport();
    std::vector<MissingAnswer> result;

    for (const auto& answer : report.missing_answers) {
        if (answer.search_count >= min_count) {
            result.push_back(answer);
        }
    }

    return result;
}

void AnalyticsService::pruneLog(size_t keep_days) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto cutoff = std::chrono::system_clock::now() - std::chrono::days(keep_days);

    // Remove old entries
    auto it = std::remove_if(log_.begin(), log_.end(),
        [cutoff](const SearchQuery& query) {
            return query.timestamp < cutoff;
        });
    log_.erase(it, log_.end());

    auto feedback_it = std::remove_if(feedback_log_.begin(), feedback_log_.end(),
        [cutoff](const FeedbackEntry& feedback) {
            return feedback.timestamp < cutoff;
        });
    feedback_log_.erase(feedback_it, feedback_log_.end());

    pruneQueryCounts();
}

std::string AnalyticsService::exportToJson(const AnalyticsReport& report) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"total_queries\": " << report.total_queries << ",\n";
    json << "  \"queries_with_results\": " << report.queries_with_results << ",\n";
    json << "  \"queries_without_results\": " << report.queries_without_results << ",\n";
    json << std::fixed << std::setprecision(1);
    json << "  \"answer_rate_percent\": " << report.answer_rate_percent << ",\n";
    json << "  \"avg_response_time_ms\": " << report.avg_response_time_ms << ",\n";
    json << "  \"median_response_time_ms\": " << report.median_response_time_ms << ",\n";

    // Top queries
    json << "  \"top_queries\": [\n";
    for (size_t i = 0; i < report.top_queries.size(); ++i) {
        const auto& tq = report.top_queries[i];
        json << "    {\"query\": \"" << escapeJson(tq.query)
             << "\", \"count\": " << tq.count << "}";
        if (i < report.top_queries.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";

    // Missing answers
    json << "  \"missing_answers\": [\n";
    for (size_t i = 0; i < report.missing_answers.size(); ++i) {
        const auto& ma = report.missing_answers[i];
        json << "    {\"query\": \"" << escapeJson(ma.query)
             << "\", \"search_count\": " << ma.search_count << "}";
        if (i < report.missing_answers.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";

    // Knowledge gaps
    json << "  \"knowledge_gaps\": [\n";
    for (size_t i = 0; i < report.knowledge_gaps.size(); ++i) {
        const auto& kg = report.knowledge_gaps[i];
        json << "    {\"query\": \"" << escapeJson(kg.query)
             << "\", \"search_count\": " << kg.search_count << "}";
        if (i < report.knowledge_gaps.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";

    json << "  \"feedback\": {"
         << "\"total\": " << report.feedback_total << ", "
         << "\"helpful\": " << report.feedback_helpful << ", "
         << "\"not_helpful\": " << report.feedback_not_helpful << ", "
         << "\"by_category\": {";
    size_t category_index = 0;
    for (const auto& [category, count] : report.feedback_by_category) {
        if (category_index++ > 0) {
            json << ", ";
        }
        json << "\"" << escapeJson(category) << "\": " << count;
    }
    json << "}}\n";

    json << "}";
    return json.str();
}

size_t AnalyticsService::getTotalQueries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return log_.size();
}

// ============================================================================
// Internal helpers
// ============================================================================

void AnalyticsService::updateQueryCounts(const SearchQuery& query) {
    query_counts_[query.query]++;
}

void AnalyticsService::pruneQueryCounts() {
    // Remove entries that are no longer in the log
    std::map<std::string, size_t> valid_counts;
    for (const auto& query : log_) {
        valid_counts[query.query]++;
    }
    query_counts_ = std::move(valid_counts);
}

std::vector<AnalyticsService::MissingAnswer> AnalyticsService::extractMissingQueries() const {
    std::vector<MissingAnswer> missing;

    // Find queries without results
    std::map<std::string, size_t> missing_counts;
    std::map<std::string, std::chrono::system_clock::time_point> missing_last;

    for (const auto& query : log_) {
        if (!query.has_answer) {
            missing_counts[query.query]++;
            if (missing_last[query.query] < query.timestamp) {
                missing_last[query.query] = query.timestamp;
            }
        }
    }

    for (const auto& [query, count] : missing_counts) {
        MissingAnswer ma;
        ma.query = query;
        ma.search_count = count;
        ma.last_searched = missing_last[query];
        missing.push_back(ma);
    }

    // Sort by search count (descending)
    std::sort(missing.begin(), missing.end(),
              [](const MissingAnswer& a, const MissingAnswer& b) {
                  return a.search_count > b.search_count;
              });

    return missing;
}

std::vector<AnalyticsService::DailyTrend> AnalyticsService::calculateDailyTrends(
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) const {

    std::vector<DailyTrend> trends;

    // Create date buckets
    std::map<std::string, DailyTrend> date_map;

    auto current = from;
    while (current <= to) {
        std::string date_str = formatDate(current);
        DailyTrend trend;
        trend.date = date_str;
        trend.queries = 0;
        trend.missing = 0;
        date_map[date_str] = trend;
        current += std::chrono::days(1);
    }

    // Count queries per day
    for (const auto& query : log_) {
        if (query.timestamp >= from && query.timestamp <= to) {
            std::string date_str = formatDate(query.timestamp);
            if (date_map.count(date_str)) {
                date_map[date_str].queries++;
                if (!query.has_answer) {
                    date_map[date_str].missing++;
                }
            }
        }
    }

    // Convert to vector
    for (const auto& [date, trend] : date_map) {
        trends.push_back(trend);
    }

    return trends;
}

std::vector<AnalyticsService::KnowledgeGap> AnalyticsService::calculateKnowledgeGaps() const {
    auto missing = extractMissingQueries();
    std::vector<KnowledgeGap> gaps;

    for (const auto& ma : missing) {
        if (ma.search_count >= config_.gap_min_search_count) {
            KnowledgeGap gap;
            gap.query = ma.query;
            gap.search_count = ma.search_count;
            gap.last_searched = ma.last_searched;
            gaps.push_back(gap);
        }
    }

    return gaps;
}

std::string AnalyticsService::formatDate(std::chrono::system_clock::time_point tp) const {
    auto time_t_value = std::chrono::system_clock::to_time_t(tp);
    struct tm tm_buf;
    localtime_r(&time_t_value, &tm_buf);

    std::ostringstream oss;
    oss << std::setfill('0')
        << (tm_buf.tm_year + 1900) << "-"
        << std::setw(2) << (tm_buf.tm_mon + 1) << "-"
        << std::setw(2) << tm_buf.tm_mday;
    return oss.str();
}

std::string AnalyticsService::escapeJson(const std::string& str) const {
    std::string result;
    result.reserve(str.size() + 10);

    for (char c : str) {
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
