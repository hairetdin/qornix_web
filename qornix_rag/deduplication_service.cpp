#include "deduplication_service.h"
#include <algorithm>
#include <cmath>
#include <sstream>

DeduplicationService::DeduplicationService(Config config)
    : config_(std::move(config)) {
}

float DeduplicationService::cosineSimilarity(
    const std::vector<float>& a,
    const std::vector<float>& b
) const {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0f;
    }

    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < a.size(); ++i) {
        dot_product += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) {
        return 0.0f;
    }

    return dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

std::vector<float> DeduplicationService::getQuestionEmbedding(
    const std::string& question,
    RagEngine& engine
) {
    // Check cache first
    auto cache_it = embedding_cache_.find(question);
    if (cache_it != embedding_cache_.end()) {
        return cache_it->second;
    }

    // Generate embedding
    auto embedding = engine.generate_embedding(question);

    // Cache it
    if (!embedding.empty()) {
        embedding_cache_[question] = embedding;
    }

    return embedding;
}

std::string DeduplicationService::generateReason(
    const std::string& primary_question,
    const std::string& duplicate_question,
    float similarity
) const {
    std::ostringstream reason;
    reason << "Вопросы семантически схожи (similarity="
           << std::fixed << std::setprecision(2) << similarity << ")";

    // Check for substring relationship
    if (primary_question.length() > 10 &&
        duplicate_question.length() > 10 &&
        (primary_question.find(duplicate_question) != std::string::npos ||
         duplicate_question.find(primary_question) != std::string::npos)) {
        reason << " — один вопрос является подстрокой другого";
    }

    return reason.str();
}

bool DeduplicationService::isExcludedCategory(const std::string& category) const {
    for (const auto& excluded : config_.exclude_categories) {
        if (excluded == category) {
            return true;
        }
    }
    return false;
}

DeduplicationService::DedupResult DeduplicationService::findDuplicates(
    const std::vector<QASource::QAPair>& pairs,
    RagEngine& engine
) {
    DedupResult result;
    result.total_pairs = pairs.size();
    result.duplicates_removed = 0;

    if (pairs.size() < 2) {
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Generate embeddings for all pairs
    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(pairs.size());

    for (const auto& pair : pairs) {
        auto embedding = getQuestionEmbedding(pair.question, engine);
        embeddings.push_back(std::move(embedding));
    }

    // Compare all pairs (O(n²) comparison)
    std::vector<bool> is_duplicate(pairs.size(), false);

    for (size_t i = 0; i < pairs.size(); ++i) {
        if (isExcludedCategory(pairs[i].category)) {
            continue;
        }

        for (size_t j = i + 1; j < pairs.size(); ++j) {
            if (isExcludedCategory(pairs[j].category)) {
                continue;
            }

            // Skip if already marked as duplicate
            if (is_duplicate[j]) {
                continue;
            }

            float similarity = cosineSimilarity(embeddings[i], embeddings[j]);

            if (similarity >= config_.similarity_threshold &&
                pairs[i].id != pairs[j].id) {
                DuplicatePair dup;
                dup.primary_id = pairs[i].id;
                dup.duplicate_id = pairs[j].id;
                dup.similarity = similarity;
                dup.reason = generateReason(pairs[i].question, pairs[j].question, similarity);

                result.duplicates.push_back(dup);
                is_duplicate[j] = true;
                result.duplicates_found++;
            }
        }
    }

    return result;
}

std::optional<DeduplicationService::DuplicatePair> DeduplicationService::isDuplicate(
    const QASource::QAPair& new_pair,
    const std::vector<QASource::QAPair>& existing_pairs,
    RagEngine& engine
) {
    if (existing_pairs.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto new_embedding = getQuestionEmbedding(new_pair.question, engine);
    if (new_embedding.empty()) {
        return std::nullopt;
    }

    for (const auto& existing : existing_pairs) {
        if (isExcludedCategory(existing.category)) {
            continue;
        }

        auto existing_embedding = getQuestionEmbedding(existing.question, engine);
        if (existing_embedding.empty()) {
            continue;
        }

        float similarity = cosineSimilarity(new_embedding, existing_embedding);

        if (similarity >= config_.similarity_threshold) {
            DuplicatePair dup;
            dup.primary_id = existing.id;
            dup.duplicate_id = new_pair.id;
            dup.similarity = similarity;
            dup.reason = generateReason(existing.question, new_pair.question, similarity);

            return dup;
        }
    }

    return std::nullopt;
}

DeduplicationService::DedupResult DeduplicationService::findDuplicatesFromSQLite(
    SQLiteSource& sqlite,
    RagEngine& engine
) {
    auto all_pairs = sqlite.getAllPairs();
    return findDuplicates(all_pairs, engine);
}

size_t DeduplicationService::removeDuplicates(
    SQLiteSource& sqlite,
    const std::vector<DuplicatePair>& duplicates
) {
    size_t removed = 0;

    for (const auto& dup : duplicates) {
        if (sqlite.deleteQAPair(dup.duplicate_id)) {
            removed++;
        }
    }

    return removed;
}
