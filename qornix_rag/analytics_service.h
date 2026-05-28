/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>

/**
 * Service for analyzing search queries and identifying knowledge gaps.
 * Tracks search patterns, missing answers, and generates actionable reports.
 */
class AnalyticsService {
public:
    struct SearchQuery {
        std::string query;
        std::chrono::system_clock::time_point timestamp;
        int result_count;
        bool has_answer;
        int response_time_ms;
        std::string client_ip;
        std::string top_result_path;
    };

    struct FeedbackEntry {
        std::string id;
        std::string request_id;
        std::string query;
        std::string question;
        std::string answer;
        std::string rating;
        std::string category;
        std::string comment;
        std::vector<std::string> citations;
        std::string client_ip;
        std::chrono::system_clock::time_point timestamp;
    };

    // Knowledge gap structure
    struct KnowledgeGap {
        std::string query;
        size_t search_count;
        std::chrono::system_clock::time_point last_searched;
    };

    // Missing answer structure
    struct MissingAnswer {
        std::string query;
        size_t search_count;
        std::chrono::system_clock::time_point last_searched;
    };

    // Daily trend structure
    struct DailyTrend {
        std::string date;
        size_t queries;
        size_t missing;
    };

    struct AnalyticsReport {
        // Search statistics
        size_t total_queries;
        size_t queries_with_results;
        size_t queries_without_results;
        float answer_rate_percent;
        float avg_response_time_ms;
        float median_response_time_ms;

        // Top queries (most frequent)
        struct TopQuery {
            std::string query;
            size_t count;
        };
        std::vector<TopQuery> top_queries;

        // Missing answers (queries with no results)
        std::vector<MissingAnswer> missing_answers;

        // Category distribution
        std::map<std::string, size_t> category_distribution;

        // Daily trends (last 30 days)
        std::vector<DailyTrend> daily_trends;

        // Knowledge gaps (frequently searched but missing)
        std::vector<KnowledgeGap> knowledge_gaps;

        // Feedback statistics
        size_t feedback_total = 0;
        size_t feedback_helpful = 0;
        size_t feedback_not_helpful = 0;
        std::map<std::string, size_t> feedback_by_category;
    };

    struct Config {
        size_t max_log_entries = 100000;       // Max entries in memory log
        bool enable_missing_detection = true;  // Detect missing answers
        bool enable_daily_trends = true;       // Calculate daily trends
        size_t top_n = 20;                     // Number of top queries to track
        size_t gap_min_search_count = 3;       // Min searches to be considered a gap
    };

    AnalyticsService();
    explicit AnalyticsService(Config config);

    /**
     * Log a search query.
     */
    void logSearch(const SearchQuery& query);

    /**
     * Capture user feedback about search/ask quality.
     */
    void logFeedback(const FeedbackEntry& feedback);

    std::vector<FeedbackEntry> getFeedbackLog(size_t limit = 100) const;

    /**
     * Get analytics report for a time period.
     */
    AnalyticsReport getReport(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to
    );

    /**
     * Get report for the last 30 days (default).
     */
    AnalyticsReport getRecentReport();

    /**
     * Get knowledge gaps (frequently searched but missing answers).
     */
    std::vector<KnowledgeGap> getKnowledgeGaps(size_t min_search_count = 0);

    /**
     * Get raw search log (for debugging).
     */
    std::vector<SearchQuery> getSearchLog(size_t limit = 100) const;

    /**
     * Get missing answers list.
     */
    std::vector<MissingAnswer> getMissingAnswers(size_t min_count = 1);

    /**
     * Clear old log entries (keep only last N days).
     */
    void pruneLog(size_t keep_days = 30);

    /**
     * Export report to JSON string.
     */
    std::string exportToJson(const AnalyticsReport& report) const;

    /**
     * Get configuration.
     */
    Config getConfig() const { return config_; }
    void setConfig(Config config) { config_ = config; }

    /**
     * Get total number of logged queries.
     */
    size_t getTotalQueries() const;

private:
    Config config_;
    std::vector<SearchQuery> log_;
    std::vector<FeedbackEntry> feedback_log_;
    std::map<std::string, size_t> query_counts_;
    mutable std::mutex mutex_;

    // Internal helpers
    void updateQueryCounts(const SearchQuery& query);
    void pruneQueryCounts();
    std::vector<MissingAnswer> extractMissingQueries() const;
    std::vector<DailyTrend> calculateDailyTrends(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to
    ) const;
    std::vector<KnowledgeGap> calculateKnowledgeGaps() const;
    std::string formatDate(std::chrono::system_clock::time_point tp) const;
    std::string escapeJson(const std::string& str) const;
};
