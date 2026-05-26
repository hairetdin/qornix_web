/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Qornix RAG - QA Pairs Knowledge Base Data Source
 *
 * Store question-answer pairs and search by question similarity.
 * Enables using qornix_rag as a knowledge base without code dependency.
 */

#include "data_source.h"
#include <optional>

namespace qornix {
namespace rag {

/**
 * QA pairs knowledge base data source.
 * Store question-answer pairs and search by question similarity.
 *
 * Usage:
 *   QASource::Config config;
 *   config.pairs = {
 *       {"qa_001", "How to build?", "Run cmake...", "setup", {"build", "compile"}},
 *       {"qa_002", "What is DI?", "DI is dependency injection...", "architecture", {"di", "injection"}}
 *   };
 *   auto source = std::make_shared<QASource>(config);
 *   engine.addDataSource(source);
 */
class QASource : public DataSource {
public:
    /**
     * Single QA pair.
     */
    struct QAPair {
        std::string id;                      // Unique identifier
        std::string question;                // Question text
        std::string answer;                  // Answer text
        std::string category = "general";    // Optional category/tag
        std::vector<std::string> aliases;    // Alternative ways to ask
        std::vector<std::string> tags;       // Additional tags for filtering/attribution
        std::map<std::string, std::string> metadata; // Extra metadata (author, date, version, etc.)

        QAPair() = default;
        QAPair(std::string id_, std::string question_, std::string answer_,
               std::string category_ = "general",
               std::vector<std::string> aliases_ = {},
               std::vector<std::string> tags_ = {})
            : id(std::move(id_)), question(std::move(question_)),
              answer(std::move(answer_)), category(std::move(category_)),
              aliases(std::move(aliases_)), tags(std::move(tags_)) {}
    };

    struct Config {
        std::vector<QAPair> pairs;           // Initial QA pairs
        std::string name = "QA Knowledge Base";
        std::string source_id = "qa_default";
        bool enable_fuzzy_search = true;
    };

    explicit QASource(Config config);
    ~QASource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::QA_KB; }
    size_t count() const override;
    bool initialize() override { return true; }
    void cleanup() override;
    std::string getId() const override { return config_.source_id; }
    std::string getName() const override { return config_.name; }

    // QA-specific API
    /**
     * Add a QA pair to the knowledge base.
     */
    void addQAPair(const QAPair& pair);

    /**
     * Remove a QA pair by ID.
     */
    void removeQAPair(const std::string& id);

    /**
     * Find a QA pair by question text (exact match on question or aliases).
     */
    std::optional<QAPair> findQAPair(const std::string& question) const;

    /**
     * Search QA pairs by category.
     */
    std::vector<QAPair> searchByCategory(const std::string& category) const;

    /**
     * Get all QA pairs.
     */
    std::vector<QAPair> getAllPairs() const;

    /**
     * Update an existing QA pair.
     */
    bool updateQAPair(const QAPair& pair);

    /**
     * Load QA pairs from a JSON string.
     */
    bool loadFromJson(const std::string& json_str);

    /**
     * Save QA pairs to a JSON string.
     */
    std::string toJson() const;

private:
    /**
     * Convert a QAPair to a Document for indexing.
     * The document content is structured as: "Question: ...\n\nAnswer: ..."
     */
    Document qaPairToDocument(const QAPair& pair) const;

    Config config_;
    std::map<std::string, QAPair> qa_pairs_; // id -> QAPair
    mutable std::mutex mutex_;
};

} // namespace rag
} // namespace qornix
