/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "qa_source.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <sstream>

namespace qornix {
namespace rag {

QASource::QASource(Config config) : config_(std::move(config)) {
    // Load initial QA pairs
    for (const auto& pair : config_.pairs) {
        qa_pairs_[pair.id] = pair;
    }
}

void QASource::cleanup() {
    // QA pairs stay in memory; no cleanup needed
}

void QASource::addQAPair(const QAPair& pair) {
    std::lock_guard<std::mutex> lock(mutex_);
    qa_pairs_[pair.id] = pair;
}

void QASource::removeQAPair(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    qa_pairs_.erase(id);
}

std::optional<QASource::QAPair> QASource::findQAPair(const std::string& question) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto lower_question = question;
    std::transform(lower_question.begin(), lower_question.end(),
                   lower_question.begin(), ::tolower);

    for (const auto& [id, pair] : qa_pairs_) {
        // Check direct question match
        auto lower_q = pair.question;
        std::transform(lower_q.begin(), lower_q.end(), lower_q.begin(), ::tolower);
        if (lower_q == lower_question) {
            return pair;
        }

        // Check aliases
        for (const auto& alias : pair.aliases) {
            auto lower_alias = alias;
            std::transform(lower_alias.begin(), lower_alias.end(),
                           lower_alias.begin(), ::tolower);
            if (lower_alias == lower_question) {
                return pair;
            }
        }
    }

    return std::nullopt;
}

std::vector<QASource::QAPair> QASource::searchByCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<QASource::QAPair> result;
    for (const auto& [id, pair] : qa_pairs_) {
        if (pair.category == category) {
            result.push_back(pair);
        }
    }
    return result;
}

std::vector<QASource::QAPair> QASource::getAllPairs() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<QASource::QAPair> result;
    result.reserve(qa_pairs_.size());
    for (const auto& [id, pair] : qa_pairs_) {
        result.push_back(pair);
    }
    return result;
}

bool QASource::updateQAPair(const QAPair& pair) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = qa_pairs_.find(pair.id);
    if (it == qa_pairs_.end()) {
        return false;
    }
    it->second = pair;
    return true;
}

Document QASource::qaPairToDocument(const QAPair& pair) const {
    Document doc;
    doc.path = "qa://" + pair.id;
    doc.relative_path = "qa://" + pair.id;
    doc.content = "Вопрос: " + pair.question + "\n\nОтвет: " + pair.answer;
    doc.type = "qa_pair";
    doc.language = "text";
    doc.size_bytes = doc.content.size();
    doc.lines_count = 3;
    doc.hash = HashCalculator::compute_md5(pair.id + pair.question + pair.answer);
    doc.last_modified = std::chrono::system_clock::now();

    // Store metadata
    doc.metadata["qa_id"] = pair.id;
    doc.metadata["category"] = pair.category;

    if (!pair.aliases.empty()) {
        doc.metadata["aliases"] = std::accumulate(
            pair.aliases.begin(), pair.aliases.end(), std::string(""),
            [](const std::string& a, const std::string& b) {
                return a.empty() ? b : a + ", " + b;
            }
        );
    }

    for (const auto& [key, value] : pair.metadata) {
        doc.metadata[key] = value;
    }

    return doc;
}

std::vector<Document> QASource::getDocuments() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Document> docs;
    docs.reserve(qa_pairs_.size());

    for (const auto& [id, pair] : qa_pairs_) {
        Document doc = qaPairToDocument(pair);
        docs.push_back(std::move(doc));
    }

    std::cout << "📄 QASource: Loaded " << docs.size() << " QA pairs" << std::endl;
    return docs;
}

void QASource::addDocument(const Document& doc) {
    // Parse document back to QAPair (best-effort)
    // This is a simple implementation; in production, you'd want more robust parsing
    std::lock_guard<std::mutex> lock(mutex_);

    // Extract ID from path
    if (doc.path.substr(0, 5) != "qa://") {
        return; // Not a QA document
    }

    std::string id = doc.path.substr(5);

    // Try to extract question and answer from content
    // Expected format: "Вопрос: ...\n\nОтвет: ..."
    size_t question_pos = doc.content.find("Вопрос: ");
    size_t answer_pos = doc.content.find("\n\nОтвет: ");

    if (question_pos != std::string::npos && answer_pos != std::string::npos) {
        std::string question = doc.content.substr(question_pos + 4, answer_pos - question_pos - 4);
        std::string answer = doc.content.substr(answer_pos + 6);

        // Check if this QA pair already exists
        auto it = qa_pairs_.find(id);
        if (it != qa_pairs_.end()) {
            it->second.question = question;
            it->second.answer = answer;
        } else {
            QAPair pair;
            pair.id = id;
            pair.question = question;
            pair.answer = answer;
            pair.category = doc.metadata.count("category") ? doc.metadata.at("category") : "general";
            qa_pairs_[id] = pair;
        }
    }
}

void QASource::removeDocument(const std::string& id) {
    // For QASource, documents are identified by "qa://id"
    std::string qa_id = (id.substr(0, 5) == "qa://") ? id.substr(5) : id;
    removeQAPair(qa_id);
}

size_t QASource::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return qa_pairs_.size();
}

bool QASource::loadFromJson(const std::string& json_str) {
    // Simple JSON parsing for QA pairs
    // In production, use nlohmann/json or similar
    std::lock_guard<std::mutex> lock(mutex_);

    // Placeholder for JSON parsing implementation
    // This would parse a JSON array of QA pair objects
    std::cout << "QASource: loadFromJson called (placeholder implementation)" << std::endl;
    return true;
}

std::string QASource::toJson() const {
    // Placeholder for JSON serialization implementation
    // This would serialize all QA pairs to a JSON array
    std::cout << "QASource: toJson called (placeholder implementation)" << std::endl;
    return "[]";
}

} // namespace rag
} // namespace qornix
