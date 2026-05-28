/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "core.h"
#include "qa_source.h"
#include "sqlite_source.h"
#include <string>
#include <vector>
#include <optional>
#include <mutex>

/**
 * Service for detecting duplicate QA pairs using semantic similarity.
 *
 * Uses RagEngine to generate embeddings for questions and computes
 * cosine similarity to identify near-duplicate pairs.
 *
 * Hash-based deduplication is already handled by SQLiteSource::addQAPair()
 * (UNIQUE constraint on source_id + hash). This service performs
 * semantic deduplication on top of that.
 */
class DeduplicationService {
public:
    struct DuplicatePair {
        std::string primary_id;       // Keep this one
        std::string duplicate_id;     // Remove this one
        float similarity;             // Cosine similarity (0-1)
        std::string reason;           // Human-readable explanation
    };

    struct DedupResult {
        size_t total_pairs;
        size_t duplicates_found;
        size_t duplicates_removed;
        std::vector<DuplicatePair> duplicates;
    };

    struct Config {
        float similarity_threshold;  // Similarity threshold for dedup (0.0-1.0)
        bool auto_remove;             // Auto-remove duplicates from source
        std::vector<std::string> exclude_categories;  // Skip these categories

        Config()
            : similarity_threshold(0.85f)
            , auto_remove(false) {}
    };

    explicit DeduplicationService(Config config = Config());

    /**
     * Find duplicate QA pairs in a collection.
     * Compares all pairs against each other using semantic similarity.
     *
     * @param pairs Vector of QA pairs to check
     * @param engine RagEngine for generating question embeddings
     * @return DedupResult with found duplicates
     */
    DedupResult findDuplicates(
        const std::vector<QASource::QAPair>& pairs,
        RagEngine& engine
    );

    /**
     * Check if a new QA pair is a duplicate of existing ones.
     *
     * @param new_pair The new QA pair to check
     * @param existing_pairs Existing QA pairs to compare against
     * @param engine RagEngine for generating question embeddings
     * @return DuplicatePair if duplicate found, nullopt otherwise
     */
    std::optional<DuplicatePair> isDuplicate(
        const QASource::QAPair& new_pair,
        const std::vector<QASource::QAPair>& existing_pairs,
        RagEngine& engine
    );

    /**
     * Find duplicates from a SQLiteSource.
     * Convenience method that loads all pairs from SQLite and runs dedup.
     *
     * @param sqlite SQLiteSource to check
     * @param engine RagEngine for generating question embeddings
     * @return DedupResult with found duplicates
     */
    DedupResult findDuplicatesFromSQLite(
        SQLiteSource& sqlite,
        RagEngine& engine
    );

    /**
     * Remove found duplicates from a SQLiteSource.
     *
     * @param sqlite SQLiteSource to clean
     * @param duplicates Duplicates to remove
     * @return Number of pairs actually removed
     */
    size_t removeDuplicates(SQLiteSource& sqlite,
                           const std::vector<DuplicatePair>& duplicates);

    /**
     * Get the configuration.
     */
    Config getConfig() const { return config_; }
    void setConfig(Config config) { config_ = config; }

private:
    Config config_;
    mutable std::mutex mutex_;

    /**
     * Compute cosine similarity between two vectors.
     */
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;

    /**
     * Generate embedding for a question using RagEngine.
     * Cached to avoid redundant computations.
     */
    std::vector<float> getQuestionEmbedding(
        const std::string& question,
        RagEngine& engine
    );

    /**
     * Generate a human-readable reason for the duplicate.
     */
    std::string generateReason(const std::string& primary_question,
                               const std::string& duplicate_question,
                               float similarity) const;

    /**
     * Check if a category should be excluded.
     */
    bool isExcludedCategory(const std::string& category) const;

    /**
     * Cache for question embeddings to avoid recomputation.
     */
    mutable std::unordered_map<std::string, std::vector<float>> embedding_cache_;
};
