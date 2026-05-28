/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "rag_service.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <boost/json.hpp>

using qornix::rag::QASource;

namespace {

std::string qaPairId(const std::string& source_id) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return source_id + "_qa_" + std::to_string(millis);
}

std::string ingestionJobId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "ingest_" + std::to_string(millis);
}

#if QORNIX_HAS_SQLITE
RagServiceIngestionJob serviceJob(const SQLiteSource::IngestionJobRecord& record) {
    RagServiceIngestionJob job;
    job.id = record.id;
    job.source_id = record.source_id;
    job.root_path = record.root_path;
    job.status = record.status;
    job.files_seen = record.files_seen;
    job.documents_imported = record.documents_imported;
    job.duplicates_found = record.duplicates_found;
    job.skipped = record.skipped;
    job.errors = record.errors;
    job.progress_percent = record.status == "completed" || record.status == "failed" ? 100 : 0;
    job.background = false;
    job.error_message = record.error_message;
    job.started_at = record.started_at;
    job.finished_at = record.finished_at;
    return job;
}
#endif

std::string qaPath(const QASource::QAPair& pair) {
    return "qa://" + (pair.category.empty() ? std::string("general") : pair.category) + "/" + pair.id;
}

std::string qaSnippet(const QASource::QAPair& pair) {
    return "Q: " + pair.question + "\nA: " + pair.answer;
}

std::string qaAttribution(const QASource::QAPair& pair) {
    auto source_it = pair.metadata.find("source");
    auto author_it = pair.metadata.find("author");
    auto updated_it = pair.metadata.find("updated_at");
    std::string attribution = "QA Knowledge Base";
    if (source_it != pair.metadata.end() && !source_it->second.empty()) {
        attribution = source_it->second;
    }
    if (author_it != pair.metadata.end() && !author_it->second.empty()) {
        attribution += " by " + author_it->second;
    }
    if (updated_it != pair.metadata.end() && !updated_it->second.empty()) {
        attribution += " updated " + updated_it->second;
    }
    return attribution;
}

std::string stringArrayJson(const std::vector<std::string>& values) {
    boost::json::array array;
    for (const auto& value : values) {
        if (!value.empty()) {
            array.emplace_back(value);
        }
    }
    return boost::json::serialize(array);
}

std::vector<std::string> jsonStringArray(const boost::json::object& object, const char* key) {
    std::vector<std::string> values;
    if (!object.contains(key) || !object.at(key).is_array()) {
        return values;
    }
    for (const auto& value : object.at(key).as_array()) {
        if (value.is_string()) {
            values.emplace_back(value.as_string().c_str());
        }
    }
    return values;
}

std::map<std::string, std::string> jsonStringMap(const boost::json::object& object, const char* key) {
    std::map<std::string, std::string> metadata;
    if (!object.contains(key) || !object.at(key).is_object()) {
        return metadata;
    }
    for (const auto& entry : object.at(key).as_object()) {
        if (entry.value().is_string()) {
            metadata[std::string(entry.key())] = entry.value().as_string().c_str();
        }
    }
    return metadata;
}

double qaScore(const QASource::QAPair& pair, const std::string& query, size_t rank) {
    std::string lowered = query;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    double score = std::max(0.35, 0.98 - static_cast<double>(rank) * 0.04);
    const std::string question = lower(pair.question);
    const std::string answer = lower(pair.answer);
    if (!lowered.empty() && question.find(lowered) != std::string::npos) {
        score = 1.0;
    } else if (!lowered.empty() && answer.find(lowered) != std::string::npos) {
        score = std::max(score, 0.88);
    }
    for (const auto& alias : pair.aliases) {
        if (!lowered.empty() && lower(alias).find(lowered) != std::string::npos) {
            score = std::max(score, 0.94);
        }
    }
    if (score <= 0.0) {
        return 0.0;
    }
    return score >= 1.0 ? 1.0 : score;
}

double confidenceFromScore(double score) {
    if (score <= 0.0) {
        return 0.0;
    }
    if (score >= 1.0) {
        return 1.0;
    }
    return score;
}

std::string sourcePathForDocument(const Document& doc) {
    auto it = doc.metadata.find("chunk_of");
    return it == doc.metadata.end() || it->second.empty()
        ? doc.relative_path
        : it->second;
}

std::string metadataValue(const std::map<std::string, std::string>& metadata, const std::string& key) {
    auto it = metadata.find(key);
    return it == metadata.end() ? std::string{} : it->second;
}

void addLocatorPart(std::vector<std::string>& parts, const std::string& label, const std::string& value) {
    if (!value.empty()) {
        parts.push_back(label + " " + value);
    }
}

std::string joinLocatorParts(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += parts[i];
    }
    return out;
}

std::string sourceLocatorForDocument(const Document& doc) {
    std::vector<std::string> parts;
    const auto& metadata = doc.metadata;

    addLocatorPart(parts, "page", metadataValue(metadata, "chunk_page"));
    addLocatorPart(parts, "slide", metadataValue(metadata, "chunk_slide"));

    const std::string sheet = metadataValue(metadata, "chunk_sheet");
    if (!sheet.empty()) {
        std::string row = metadataValue(metadata, "chunk_row_start");
        const std::string row_end = metadataValue(metadata, "chunk_row_end");
        if (!row.empty() && !row_end.empty() && row_end != row) {
            row += "-" + row_end;
        }
        parts.push_back(row.empty() ? "sheet " + sheet : "sheet " + sheet + ", row " + row);
    }

    const std::string heading = metadataValue(metadata, "chunk_heading");
    if (!heading.empty()) {
        parts.push_back("heading " + heading);
    }

    const std::string symbol_name = metadataValue(metadata, "chunk_symbol_name");
    if (!symbol_name.empty()) {
        const std::string symbol_kind = metadataValue(metadata, "chunk_symbol_kind");
        parts.push_back((symbol_kind.empty() ? "symbol" : symbol_kind) + " " + symbol_name);
    }

    const std::string ocr_region = metadataValue(metadata, "chunk_ocr_region");
    if (!ocr_region.empty()) {
        std::string part = "ocr " + ocr_region;
        const std::string confidence = metadataValue(metadata, "chunk_ocr_confidence").empty()
            ? metadataValue(metadata, "image_ocr_confidence_avg")
            : metadataValue(metadata, "chunk_ocr_confidence");
        if (!confidence.empty()) {
            part += " confidence " + confidence;
        }
        parts.push_back(std::move(part));
    }

    return joinLocatorParts(parts);
}

std::string resolveRequestedScanPath(const std::shared_ptr<RagEngine>& engine,
                                     const std::optional<std::string>& scan_path) {
    if (scan_path && !scan_path->empty()) {
        return *scan_path;
    }
    if (engine) {
        return engine->get_indexed_project_root();
    }
    return {};
}

std::string citationLabelForDocument(const std::string& citation_id, const Document& doc) {
    std::string label = "[" + citation_id + "] " + sourcePathForDocument(doc);
    const std::string locator = sourceLocatorForDocument(doc);
    if (!locator.empty()) {
        label += " (" + locator + ")";
    }
    return label;
}

std::map<std::string, std::string> curatedMetadata(const Document& doc) {
    std::map<std::string, std::string> metadata;
    static const std::vector<std::string> preferred = {
        "metadata_contract", "structure_contract", "ingestion_parser", "chunk_strategy",
        "chunk_index", "chunk_count", "chunk_page", "chunk_sheet", "chunk_row_start",
        "chunk_row_end", "chunk_slide", "chunk_heading", "chunk_symbol_name",
        "chunk_symbol_kind", "chunk_symbol_scope", "chunk_ocr_region",
        "image_ocr_confidence_avg", "pdf_page_count", "docx_heading_count",
        "xlsx_sheet_count", "pptx_slide_count"
    };
    for (const auto& key : preferred) {
        auto it = doc.metadata.find(key);
        if (it != doc.metadata.end() && !it->second.empty()) {
            metadata[key] = it->second;
        }
    }
    return metadata;
}

std::string citationId(const std::string& prefix, size_t index) {
    return prefix + std::to_string(index + 1);
}

RagServiceSearchItem projectSearchItem(const SearchResult& result) {
    RagServiceSearchItem item;
    item.path = result.document.relative_path;
    item.source_path = sourcePathForDocument(result.document);
    auto chunk_it = result.document.metadata.find("chunk_index");
    item.citation_id = chunk_it == result.document.metadata.end()
        ? "S"
        : "S" + chunk_it->second;
    item.type = result.document.type;
    item.language = result.document.language;
    item.score = result.score;
    item.confidence = confidenceFromScore(result.fused_score > 0.0 ? result.fused_score : result.score);
    item.vector_score = result.vector_score;
    item.text_score = result.text_score;
    item.fused_score = result.fused_score;
    item.snippet = result.snippet;
    item.lines = result.document.lines_count;
    item.size = result.document.size_bytes;
    item.source_type = "project";
    item.source_locator = sourceLocatorForDocument(result.document);
    item.citation_label = citationLabelForDocument(item.citation_id, result.document);
    item.metadata = curatedMetadata(result.document);
    return item;
}

RagServiceSearchItem qaSearchItem(const QASource::QAPair& pair, double score) {
    RagServiceSearchItem item;
    item.path = qaPath(pair);
    item.source_path = item.path;
    item.type = "QA";
    item.language = "knowledge_base";
    item.score = score;
    item.confidence = confidenceFromScore(score);
    item.text_score = score;
    item.fused_score = score;
    item.snippet = qaSnippet(pair);
    item.lines = 2;
    item.size = pair.question.size() + pair.answer.size();
    item.source_type = "qa";
    item.citation_label = item.path;
    item.category = pair.category;
    item.pair_id = pair.id;
    item.tags = pair.tags;
    item.attribution = qaAttribution(pair);
    return item;
}

std::string groundingStatus(double confidence, size_t context_count) {
    if (context_count == 0) {
        return "no_context";
    }
    if (confidence >= 0.75) {
        return "grounded";
    }
    if (confidence >= 0.35) {
        return "partial";
    }
    return "weak";
}

bool isFallbackAnswer(const std::string& answer) {
    return answer.rfind("LLM unavailable", 0) == 0 ||
           answer.rfind("LLM is not configured", 0) == 0 ||
           answer.rfind("Error:", 0) == 0;
}

std::vector<std::string> uniqueOrdered(const std::vector<std::string>& values) {
    std::vector<std::string> output;
    std::set<std::string> seen;
    for (const auto& value : values) {
        if (!value.empty() && seen.insert(value).second) {
            output.push_back(value);
        }
    }
    return output;
}

std::vector<std::string> extractCitations(const std::string& text) {
    std::vector<std::string> citations;
    const std::regex citation_re(R"(\[((?:S|Q)\d+)\])");
    auto begin = std::sregex_iterator(text.begin(), text.end(), citation_re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        if (it->size() > 1) {
            citations.push_back((*it)[1].str());
        }
    }
    return uniqueOrdered(citations);
}

bool containsValue(const std::vector<std::string>& values, const std::string& target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

void postProcessGrounding(RagServiceAskResponse& response) {
    response.citations = uniqueOrdered(response.citations);
    response.sources = uniqueOrdered(response.sources);
    response.answer_citations = extractCitations(response.answer);
    if (response.answer_citations.empty() &&
        !response.citations.empty() &&
        response.llm_status != "unavailable" &&
        response.llm_status != "not_configured" &&
        response.llm_status != "invalid_request") {
        response.answer += "\n\nSources: ";
        for (size_t i = 0; i < response.citations.size(); ++i) {
            if (i > 0) {
                response.answer += ", ";
            }
            response.answer += "[" + response.citations[i] + "]";
        }
        response.citations_post_processed = true;
        response.answer_citations = extractCitations(response.answer);
    }
    response.missing_citations.clear();
    response.uncited_context_citations.clear();

    for (const auto& citation : response.answer_citations) {
        if (!containsValue(response.citations, citation)) {
            response.missing_citations.push_back(citation);
        }
    }
    for (const auto& citation : response.citations) {
        if (!containsValue(response.answer_citations, citation)) {
            response.uncited_context_citations.push_back(citation);
        }
    }

    if (response.context.empty()) {
        response.grounding_status = "no_context";
    } else if (!response.missing_citations.empty()) {
        response.grounding_status = "unsupported_citations";
    } else if (response.answer_citations.empty()) {
        response.grounding_status = response.llm_status == "unavailable" ? response.grounding_status : "uncited";
    } else {
        response.grounding_status = groundingStatus(response.retrieval_confidence, response.context.size());
    }
}


std::string lowerQualityCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalizeQualityWhitespace(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    bool previous_space = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch) || std::ispunct(ch)) {
            if (!previous_space) {
                output.push_back(' ');
            }
            previous_space = true;
        } else {
            output.push_back(static_cast<char>(ch));
            previous_space = false;
        }
    }
    while (!output.empty() && output.front() == ' ') output.erase(output.begin());
    while (!output.empty() && output.back() == ' ') output.pop_back();
    return output;
}

std::vector<std::string> qualityTerms(const std::string& value) {
    static const std::set<std::string> stop_words = {
        "what", "which", "where", "when", "why", "how", "does", "do", "did", "the", "and", "or",
        "is", "are", "was", "were", "a", "an", "to", "of", "for", "in", "on", "with", "that", "this",
        "как", "что", "где", "когда", "почему", "зачем", "это", "или", "для", "при", "про", "надо", "нужно"
    };
    std::vector<std::string> terms;
    std::istringstream stream(normalizeQualityWhitespace(lowerQualityCopy(value)));
    std::string term;
    while (stream >> term) {
        if (term.size() < 2 || stop_words.count(term) > 0) {
            continue;
        }
        terms.push_back(term);
    }
    return terms;
}

std::string rewriteQualityQuery(const std::string& query) {
    std::vector<std::string> terms = qualityTerms(query);
    if (terms.empty()) {
        return normalizeQualityWhitespace(query);
    }
    std::string out;
    std::set<std::string> seen;
    for (const auto& term : terms) {
        if (!seen.insert(term).second) {
            continue;
        }
        if (!out.empty()) out += ' ';
        out += term;
    }
    return out.empty() ? normalizeQualityWhitespace(query) : out;
}

std::string compactQueryKey(const std::string& value) {
    return normalizeQualityWhitespace(lowerQualityCopy(value));
}

std::string resultMergeKey(const SearchResult& result) {
    const auto& doc = result.document;
    std::string key = doc.relative_path;
    const std::vector<std::string> discriminators = {
        "chunk_index", "chunk_page", "chunk_sheet", "chunk_row_start", "chunk_slide", "chunk_symbol_name", "chunk_ocr_region"
    };
    for (const auto& discriminator : discriminators) {
        auto it = doc.metadata.find(discriminator);
        if (it != doc.metadata.end() && !it->second.empty()) {
            key += "|" + discriminator + "=" + it->second;
        }
    }
    return key;
}

double cosineQualityScore(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.empty() || rhs.empty() || lhs.size() != rhs.size()) {
        return 0.0;
    }
    double dot = 0.0;
    double lhs_norm = 0.0;
    double rhs_norm = 0.0;
    for (size_t i = 0; i < lhs.size(); ++i) {
        dot += static_cast<double>(lhs[i]) * static_cast<double>(rhs[i]);
        lhs_norm += static_cast<double>(lhs[i]) * static_cast<double>(lhs[i]);
        rhs_norm += static_cast<double>(rhs[i]) * static_cast<double>(rhs[i]);
    }
    if (lhs_norm <= 0.0 || rhs_norm <= 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(lhs_norm) * std::sqrt(rhs_norm));
}

struct QualitySearchOutput {
    std::vector<SearchResult> results;
    std::vector<RagServiceRetrievalQuery> retrieval_queries;
    std::string rewritten_query;
    bool multi_query_applied = false;
    std::string retrieval_strategy = "single_query";
    std::string reranker_type = "none";
};

QualitySearchOutput runQualitySearch(RagEngine& engine,
                                     const std::string& query,
                                     size_t top_k,
                                     const std::vector<MetadataFilter>& filters) {
    QualitySearchOutput output;
    output.rewritten_query = rewriteQualityQuery(query);
    output.multi_query_applied = engine.is_multi_query_retrieval_enabled();
    output.reranker_type = engine.is_embedding_reranker_enabled()
        ? "embedding_model_semantic_reranker"
        : (engine.is_reranking_enabled() ? "metadata_lexical_reranker" : "none");

    std::vector<std::pair<std::string, std::string>> candidates;
    auto add_candidate = [&](std::string candidate, const std::string& origin) {
        candidate = normalizeQualityWhitespace(candidate);
        if (candidate.empty()) return;
        const auto key = compactQueryKey(candidate);
        for (const auto& existing : candidates) {
            if (compactQueryKey(existing.first) == key) {
                return;
            }
        }
        candidates.emplace_back(std::move(candidate), origin);
    };

    add_candidate(query, "original");
    const auto expanded = engine.expand_query(query);
    if (expanded != query) {
        add_candidate(expanded, "expanded");
    }
    if (output.rewritten_query != query) {
        add_candidate(output.rewritten_query, "rewritten");
    }

    // A compact high-signal variant helps long natural-language questions and generated-app route queries.
    const auto terms = qualityTerms(query);
    if (terms.size() > 2) {
        std::string compact;
        for (size_t i = 0; i < terms.size() && i < 6; ++i) {
            if (!compact.empty()) compact += ' ';
            compact += terms[i];
        }
        add_candidate(compact, "keywords");
    }

    const size_t max_variants = output.multi_query_applied
        ? std::max<size_t>(1, engine.multi_query_max_variants())
        : 1;
    if (candidates.size() > max_variants) {
        candidates.resize(max_variants);
    }
    output.retrieval_strategy = candidates.size() > 1 ? "multi_query_union" : "single_query";
    output.multi_query_applied = candidates.size() > 1;

    std::unordered_map<std::string, SearchResult> merged;
    std::unordered_map<std::string, double> best_scores;
    const std::vector<float> original_embedding = engine.is_embedding_reranker_enabled()
        ? engine.generate_embedding(query)
        : std::vector<float>{};

    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& [candidate_query, origin] = candidates[i];
        SearchOptions options;
        options.top_k = std::max<size_t>(top_k, 1) * (candidates.size() > 1 ? 2 : 1);
        options.filters = filters;
        auto results = engine.search(candidate_query, options);
        RagServiceRetrievalQuery diag;
        diag.query = candidate_query;
        diag.origin = origin;
        diag.result_count = results.size();
        output.retrieval_queries.push_back(std::move(diag));

        const double query_weight = origin == "original" ? 1.00 : (origin == "expanded" ? 0.97 : (origin == "rewritten" ? 0.94 : 0.90));
        for (auto result : results) {
            double base = result.fused_score > 0.0 ? result.fused_score : result.score;
            double quality_score = base * query_weight;
            if (!original_embedding.empty() && !result.document.embedding.empty()) {
                quality_score += std::max(0.0, cosineQualityScore(original_embedding, result.document.embedding)) * 0.20;
            }
            if (result.snippet.find(query) != std::string::npos) {
                quality_score += 0.08;
            }
            result.score = quality_score;
            result.fused_score = quality_score;
            result.document.metadata["retrieval_query_origin"] = origin;
            result.document.metadata["retrieval_strategy"] = output.retrieval_strategy;
            result.document.metadata["reranker_type"] = output.reranker_type;

            const auto key = resultMergeKey(result);
            auto best_it = best_scores.find(key);
            if (best_it == best_scores.end() || quality_score > best_it->second) {
                best_scores[key] = quality_score;
                merged[key] = std::move(result);
            }
        }
    }

    output.results.reserve(merged.size());
    for (auto& [_, result] : merged) {
        output.results.push_back(std::move(result));
    }
    std::stable_sort(output.results.begin(), output.results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;
    });
    if (output.results.size() > top_k) {
        output.results.resize(top_k);
    }
    return output;
}

double tokenOverlapScore(const std::string& claim, const std::string& context) {
    const auto claim_terms = qualityTerms(claim);
    if (claim_terms.empty() || context.empty()) {
        return 0.0;
    }
    const std::string context_lower = lowerQualityCopy(context);
    size_t matched = 0;
    std::set<std::string> unique_terms(claim_terms.begin(), claim_terms.end());
    for (const auto& term : unique_terms) {
        if (context_lower.find(term) != std::string::npos) {
            matched++;
        }
    }
    return static_cast<double>(matched) / static_cast<double>(unique_terms.size());
}

std::vector<std::string> splitAnswerClaims(const std::string& answer) {
    std::vector<std::string> claims;
    std::string current;
    for (char ch : answer) {
        current.push_back(ch);
        if (ch == '.' || ch == '!' || ch == '?' || ch == '\n') {
            auto normalized = normalizeQualityWhitespace(current);
            if (normalized.size() >= 24) {
                claims.push_back(std::move(normalized));
            }
            current.clear();
        }
        if (claims.size() >= 12) {
            break;
        }
    }
    auto normalized = normalizeQualityWhitespace(current);
    if (claims.size() < 12 && normalized.size() >= 24) {
        claims.push_back(std::move(normalized));
    }
    return claims;
}

void evaluateClaimGrounding(RagServiceAskResponse& response) {
    if (response.context.empty()) {
        response.grounding_evaluator = "none";
        response.claim_grounding_status = "no_context";
        return;
    }
    if (response.llm_status == "unavailable" || response.llm_status == "not_configured") {
        response.grounding_evaluator = "retrieval_citation_baseline";
        response.claim_grounding_status = response.grounding_status;
        return;
    }

    std::map<std::string, std::string> context_by_citation;
    for (const auto& item : response.context) {
        context_by_citation[item.citation_id] = item.snippet;
    }

    response.grounding_evaluator = "citation_lexical_claim_grounding";
    response.grounded_claims.clear();
    bool saw_uncited = false;
    bool saw_unsupported = false;
    bool saw_claim = false;
    for (const auto& claim_text : splitAnswerClaims(response.answer)) {
        RagServiceGroundingClaim claim;
        claim.claim = claim_text;
        claim.citations = extractCitations(claim_text);
        if (claim.citations.empty()) {
            claim.status = "uncited";
            saw_uncited = true;
        } else {
            double best = 0.0;
            for (const auto& citation : claim.citations) {
                auto it = context_by_citation.find(citation);
                if (it != context_by_citation.end()) {
                    best = std::max(best, tokenOverlapScore(claim_text, it->second));
                }
            }
            claim.support_score = best;
            if (best >= 0.30) {
                claim.status = "grounded";
            } else if (best >= 0.12) {
                claim.status = "partial";
            } else {
                claim.status = "unsupported";
                saw_unsupported = true;
            }
        }
        saw_claim = true;
        response.grounded_claims.push_back(std::move(claim));
    }

    if (!saw_claim) {
        response.claim_grounding_status = "no_claims";
    } else if (saw_unsupported) {
        response.claim_grounding_status = "unsupported_claims";
    } else if (saw_uncited) {
        response.claim_grounding_status = "uncited_claims";
    } else {
        response.claim_grounding_status = "grounded";
    }
}

std::string sanitizeHistoryContent(const std::string& content, size_t max_chars = 700) {
    std::string output;
    output.reserve(std::min(content.size(), max_chars));
    bool previous_space = false;
    for (unsigned char ch : content) {
        if (output.size() >= max_chars) {
            break;
        }
        if (std::iscntrl(ch) && ch != '\n' && ch != '\t') {
            continue;
        }
        if (std::isspace(ch)) {
            if (!previous_space) {
                output.push_back(' ');
            }
            previous_space = true;
        } else {
            output.push_back(static_cast<char>(ch));
            previous_space = false;
        }
    }
    while (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }
    return output;
}

std::string buildConversationHistoryBlock(const std::vector<RagServiceConversationTurn>& history,
                                          size_t& turns_used) {
    turns_used = 0;
    if (history.empty()) {
        return {};
    }

    std::vector<RagServiceConversationTurn> accepted;
    for (auto it = history.rbegin(); it != history.rend() && accepted.size() < 6; ++it) {
        if (it->role != "user" && it->role != "assistant") {
            continue;
        }
        const auto content = sanitizeHistoryContent(it->content);
        if (content.empty()) {
            continue;
        }
        accepted.push_back({it->role, content});
    }
    std::reverse(accepted.begin(), accepted.end());

    if (accepted.empty()) {
        return {};
    }

    std::ostringstream out;
    out << "Conversation history is provided only to resolve pronouns and follow-up wording.\n";
    out << "Do not treat conversation history as a factual source and do not cite it.\n";
    out << "Only cite bracketed source ids from the retrieved context.\n";
    for (const auto& turn : accepted) {
        out << (turn.role == "user" ? "User" : "Assistant") << ": " << turn.content << "\n";
    }
    turns_used = accepted.size();
    return out.str();
}

} // namespace

RagService::RagService(std::shared_ptr<RagEngine> engine,
                       std::shared_ptr<LLMClient> llm
#if QORNIX_HAS_SQLITE
                       , std::shared_ptr<SQLiteSource> sqlite
#endif
)
    : rag_engine_(std::move(engine)),
      llm_client_(std::move(llm))
#if QORNIX_HAS_SQLITE
      , sqlite_source_(std::move(sqlite))
#endif
{}

RagServiceIndexResponse RagService::indexProject(const std::optional<std::string>& scan_path) {
    RagServiceIndexResponse response;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = resolveRequestedScanPath(rag_engine_, scan_path);
    if (path.empty()) {
        response.message = "scan_path is not configured";
        return response;
    }
    rag_engine_->index_project(path);
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        const auto persisted = sqlite_source_->persistIndexedDocuments(
            rag_engine_->get_documents_snapshot(),
            "project:" + path,
            rag_engine_->get_embedding_model_id(),
            rag_engine_->get_embedding_backend()
        );
        if (persisted.documents > 0) {
            std::cout << "💾 Persisted RAG index snapshot: "
                      << persisted.documents << " documents, "
                      << persisted.chunks << " chunks, "
                      << persisted.embeddings << " embeddings, "
                      << persisted.deleted_documents << " stale documents deleted" << std::endl;
        }
    }
#endif
    response.success = true;
    response.message = "Scan path indexed successfully";
    response.stats = rag_engine_->get_statistics();
    return response;
}

RagServiceIngestResponse RagService::ingestProject(const std::optional<std::string>& scan_path) {
    RagServiceIngestResponse response;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = resolveRequestedScanPath(rag_engine_, scan_path);
    if (path.empty()) {
        response.message = "scan_path is not configured";
        return response;
    }

    RagServiceIngestionJob job;
    job.id = ingestionJobId();
    job.root_path = path;
    job.source_id = "project:" + path;
    job.status = "running";
    job.progress_percent = 0;

    return runIngestionJob(std::move(job), path);
}

RagServiceIngestResponse RagService::startBackgroundIngestProject(const std::optional<std::string>& scan_path) {
    RagServiceIngestResponse response;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }

    std::string path = resolveRequestedScanPath(rag_engine_, scan_path);
    if (path.empty()) {
        response.message = "scan_path is not configured";
        return response;
    }

    RagServiceIngestionJob job;
    job.id = ingestionJobId();
    job.root_path = path;
    job.source_id = "project:" + path;
    job.status = "queued";
    job.progress_percent = 0;
    job.background = true;
    updateActiveIngestionJob(job);

#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        sqlite_source_->recordIngestionJobStarted(job.id, job.source_id, path);
    }
#endif

    response.success = true;
    response.message = "Ingestion job queued";
    response.job = job;

    std::thread([this, job, path]() mutable {
        job.status = "running";
        job.progress_percent = 5;
        job.background = true;
        updateActiveIngestionJob(job);
        runIngestionJob(std::move(job), path);
    }).detach();

    return response;
}

RagServiceIngestResponse RagService::runIngestionJob(RagServiceIngestionJob job, const std::string& path) {
    RagServiceIngestResponse response;
    response.job = job;
    response.job.status = "running";
    response.job.progress_percent = std::max(response.job.progress_percent, 5);
    updateActiveIngestionJob(response.job);

#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        sqlite_source_->recordIngestionJobStarted(response.job.id, response.job.source_id, path);
    }
#endif

    try {
        response.job.progress_percent = 15;
        updateActiveIngestionJob(response.job);
        auto indexed = indexProject(path);
        const auto ingestion = rag_engine_->get_last_ingestion_result();
        response.success = indexed.success;
        response.message = indexed.message;
        response.stats = indexed.stats;
        response.job.status = indexed.success ? "completed" : "failed";
        response.job.progress_percent = 100;
        response.job.files_seen = ingestion.files_seen;
        response.job.documents_imported = ingestion.documents_imported;
        response.job.duplicates_found = ingestion.duplicates_found;
        response.job.skipped = ingestion.skipped;
        response.job.errors = ingestion.errors;
        updateActiveIngestionJob(response.job);

#if QORNIX_HAS_SQLITE
        if (sqlite_source_) {
            sqlite_source_->recordIngestionJobFinished(
                response.job.id,
                response.job.status,
                ingestion,
                response.success ? "" : response.message
            );
            if (auto persisted = sqlite_source_->findIngestionJob(response.job.id)) {
                response.job = serviceJob(*persisted);
                response.job.progress_percent = 100;
                response.job.background = job.background;
            }
        }
#endif
    } catch (const std::exception& e) {
        response.success = false;
        response.message = e.what();
        response.job.status = "failed";
        response.job.progress_percent = 100;
        response.job.error_message = response.message;
        updateActiveIngestionJob(response.job);
#if QORNIX_HAS_SQLITE
        if (sqlite_source_) {
            qornix::rag::IngestionJobResult empty_result;
            sqlite_source_->recordIngestionJobFinished(
                response.job.id,
                response.job.status,
                empty_result,
                response.message
            );
            if (auto persisted = sqlite_source_->findIngestionJob(response.job.id)) {
                response.job = serviceJob(*persisted);
                response.job.progress_percent = 100;
                response.job.background = job.background;
            }
        }
#endif
    }

    removeActiveIngestionJob(response.job.id);
    return response;
}

std::optional<RagServiceIngestionJob> RagService::findIngestionJob(const std::string& job_id) const {
    {
        std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
        auto it = active_ingestion_jobs_.find(job_id);
        if (it != active_ingestion_jobs_.end()) {
            return it->second;
        }
    }
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        if (auto job = sqlite_source_->findIngestionJob(job_id)) {
            return serviceJob(*job);
        }
    }
#else
    (void)job_id;
#endif
    return std::nullopt;
}

std::vector<RagServiceIngestionJob> RagService::listIngestionJobs(size_t limit) const {
    std::vector<RagServiceIngestionJob> jobs;
    {
        std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
        for (const auto& [_, job] : active_ingestion_jobs_) {
            jobs.push_back(job);
            if (jobs.size() >= limit) {
                return jobs;
            }
        }
    }
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        for (const auto& job : sqlite_source_->listIngestionJobs(limit)) {
            if (jobs.size() >= limit) break;
            jobs.push_back(serviceJob(job));
        }
    }
#else
    (void)limit;
#endif
    return jobs;
}

void RagService::updateActiveIngestionJob(const RagServiceIngestionJob& job) {
    std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
    active_ingestion_jobs_[job.id] = job;
}

void RagService::removeActiveIngestionJob(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(ingestion_jobs_mutex_);
    active_ingestion_jobs_.erase(job_id);
}

bool RagService::deletePersistedDocument(const std::string& relative_path,
                                         const std::string& source_id) {
#if QORNIX_HAS_SQLITE
    return sqlite_source_ && sqlite_source_->deletePersistedDocument(relative_path, source_id);
#else
    (void)relative_path;
    (void)source_id;
    return false;
#endif
}

RagServiceSearchResponse RagService::search(const std::string& query, size_t top_k) {
    return search(query, top_k, {});
}

RagServiceSearchResponse RagService::search(const std::string& query,
                                            size_t top_k,
                                            const std::vector<MetadataFilter>& filters) {
    RagServiceSearchResponse response;
    response.query = query;
    response.filters = filters;
    response.filters_applied = !filters.empty();
    const auto started = std::chrono::steady_clock::now();

    if (!rag_engine_ || query.empty()) {
        response.success = false;
        return response;
    }

    response.expanded_query = rag_engine_->expand_query(query);
    response.query_expansion_applied = response.expanded_query != query;
    response.reranking_applied = rag_engine_->is_reranking_enabled();

    auto quality = runQualitySearch(*rag_engine_, query, top_k, filters);
    response.rewritten_query = quality.rewritten_query;
    response.multi_query_applied = quality.multi_query_applied;
    response.retrieval_strategy = quality.retrieval_strategy;
    response.reranker_type = quality.reranker_type;
    response.retrieval_queries = quality.retrieval_queries;

    response.results.reserve(quality.results.size());
    for (const auto& result : quality.results) {
        auto item = projectSearchItem(result);
        item.citation_id = citationId("S", response.results.size());
        item.citation_label = citationLabelForDocument(item.citation_id, result.document);
        response.results.push_back(std::move(item));
    }

    if (filters.empty()) {
        const auto qa_pairs = searchQa(query, std::min<size_t>(top_k, 5));
        for (size_t i = 0; i < qa_pairs.size(); ++i) {
            auto item = qaSearchItem(qa_pairs[i], qaScore(qa_pairs[i], query, i));
            item.citation_id = citationId("Q", i);
            item.citation_label = "[" + item.citation_id + "] " + item.source_path;
            response.results.push_back(std::move(item));
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    response.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
    return response;
}

RagServiceAskResponse RagService::ask(const std::string& question, size_t top_k, const std::string& client_ip) {
    return ask(question, top_k, client_ip, {}, {});
}

RagServiceAskResponse RagService::ask(const std::string& question,
                                      size_t top_k,
                                      const std::string& client_ip,
                                      const std::vector<RagServiceConversationTurn>& history) {
    return ask(question, top_k, client_ip, history, {});
}

RagServiceAskResponse RagService::ask(const std::string& question,
                                      size_t top_k,
                                      const std::string& client_ip,
                                      const std::vector<RagServiceConversationTurn>& history,
                                      const std::vector<MetadataFilter>& filters,
                                      const std::optional<std::string>& system_prompt,
                                      const std::optional<std::string>& prompt_template) {
    RagServiceAskResponse response;
    response.question = question;
    response.filters = filters;
    response.filters_applied = !filters.empty();
    if (!rag_engine_ || question.empty()) {
        response.success = false;
        response.llm_status = "invalid_request";
        return response;
    }
    response.expanded_query = rag_engine_->expand_query(question);
    response.query_expansion_applied = response.expanded_query != question;
    response.reranking_applied = rag_engine_->is_reranking_enabled();

    std::string context_text;
    const auto qa_pairs = filters.empty() ? searchQa(question, std::min<size_t>(top_k, 5)) : std::vector<QASource::QAPair>{};
    for (size_t i = 0; i < qa_pairs.size(); ++i) {
        const auto& pair = qa_pairs[i];
        const std::string citation = citationId("Q", i);
        const double score = qaScore(pair, question, i);
        if (!context_text.empty()) {
            context_text += "\n---\n";
        }
        context_text += "[" + citation + "] [" + qaAttribution(pair) + "] " + qaPath(pair) + "\n" + qaSnippet(pair);

        RagServiceAskContextItem item;
        item.path = qaPath(pair);
        item.source_path = item.path;
        item.citation_id = citation;
        item.score = score;
        item.confidence = confidenceFromScore(item.score);
        item.snippet = qaSnippet(pair);
        item.source_type = "qa";
        item.category = pair.category;
        item.pair_id = pair.id;
        item.tags = pair.tags;
        item.attribution = qaAttribution(pair);
        item.citation_label = "[" + citation + "] " + item.source_path;
        response.context.push_back(std::move(item));
        response.sources.push_back(qaPath(pair));
        response.citations.push_back(citation);
    }

    auto quality = runQualitySearch(*rag_engine_, question, top_k, filters);
    response.rewritten_query = quality.rewritten_query;
    response.multi_query_applied = quality.multi_query_applied;
    response.retrieval_strategy = quality.retrieval_strategy;
    response.reranker_type = quality.reranker_type;
    response.retrieval_queries = quality.retrieval_queries;

    for (size_t i = 0; i < quality.results.size(); ++i) {
        const auto& result = quality.results[i];
        const std::string citation = citationId("S", i);
        const std::string source_path = sourcePathForDocument(result.document);
        const std::string locator = sourceLocatorForDocument(result.document);
        std::ostringstream ctx;
        ctx << "[" << citation << "] " << source_path;
        if (!locator.empty()) {
            ctx << " (" << locator << ")";
        }
        ctx << ": " << result.snippet;
        if (!context_text.empty()) {
            context_text += "\n---\n";
        }
        context_text += ctx.str();

        RagServiceAskContextItem item;
        item.path = result.document.relative_path;
        item.source_path = source_path;
        item.citation_id = citation;
        item.score = result.fused_score;
        item.confidence = confidenceFromScore(result.fused_score > 0.0 ? result.fused_score : result.score);
        item.snippet = result.snippet;
        item.source_type = "project";
        item.source_locator = locator;
        item.citation_label = citationLabelForDocument(citation, result.document);
        item.metadata = curatedMetadata(result.document);
        response.context.push_back(std::move(item));
        response.sources.push_back(locator.empty() ? source_path : source_path + " (" + locator + ")");
        response.citations.push_back(citation);
    }

    for (const auto& item : response.context) {
        response.retrieval_confidence = std::max(response.retrieval_confidence, item.confidence);
    }
    response.grounding_status = groundingStatus(response.retrieval_confidence, response.context.size());

    size_t history_turns_used = 0;
    const auto history_block = buildConversationHistoryBlock(history, history_turns_used);
    response.conversation_turns_used = history_turns_used;

    if (!context_text.empty()) {
        context_text =
            "Use the bracketed source ids such as [S1] or [Q1] when citing facts from context.\n"
            "If the context is insufficient, say so explicitly.\n\n" + context_text;
    }
    if (!history_block.empty()) {
        context_text = history_block + "\n---\n" + context_text;
    }

    if (llm_client_ && llm_client_->is_enabled()) {
        const auto started = std::chrono::steady_clock::now();
        const auto llm_result = llm_client_->ask_with_metadata(
            question,
            context_text,
            client_ip,
            system_prompt,
            prompt_template);
        const auto finished = std::chrono::steady_clock::now();
        response.answer = llm_result.answer;
        response.llm_parser_error = llm_result.parser_error;
        response.llm_finish_reason = llm_result.finish_reason;
        response.llm_truncated = llm_result.truncated;
        response.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
        if (llm_result.status == "parser_error" ||
            llm_result.status == "provider_error" ||
            llm_result.status == "truncated" ||
            llm_result.status == "rate_limited" ||
            llm_result.status == "cache_hit" ||
            llm_result.status == "unavailable") {
            response.llm_status = llm_result.status;
        } else {
            response.llm_status = isFallbackAnswer(response.answer)
                ? "fallback"
                : (llm_client_->is_available() ? "ok" : "unavailable");
        }
    } else {
        response.answer = "LLM unavailable. Relevant context fragments:\n" + context_text;
        response.llm_status = "unavailable";
        response.response_time_ms = 0;
    }

    postProcessGrounding(response);
    evaluateClaimGrounding(response);
    return response;
}

bool RagService::recordFeedback(const RagServiceFeedbackEntry& feedback) {
    return !feedback.rating.empty() || !feedback.comment.empty() ||
           !feedback.category.empty() || !feedback.query.empty() ||
           !feedback.question.empty();
}

RagServiceHealth RagService::health() const {
    RagServiceHealth health;
    if (!rag_engine_) {
        health.status = "not_configured";
        return health;
    }

    auto stats = rag_engine_->get_statistics();
    health.indexed = rag_engine_->is_indexed();
    health.files = stats.total_files;
    health.lines = stats.total_lines;
    health.embedding_backend = rag_engine_->get_embedding_backend();
    const auto embedding_info = rag_engine_->get_embedding_model_info();
    health.embedding_model_id = embedding_info.id;
    health.embedding_model_name = embedding_info.name;
    health.embedding_dim = embedding_info.dimension;
    health.embedding_active_model_id = embedding_info.active_model_id;
    health.embedding_registry_size = embedding_info.registry_size;
    health.embedding_registry_model_ids = embedding_info.registry_model_ids;
    health.embedding_registry_warnings = embedding_info.registry_warnings;
    health.vector_store_backend = rag_engine_->get_vector_store_backend();
    health.vector_store_status = rag_engine_->get_vector_store_status();
    const auto vector_diagnostics = rag_engine_->get_vector_store_diagnostics();
    health.vector_store_health_status = vector_diagnostics.status;
    health.vector_store_health_detail = vector_diagnostics.detail;
    health.vector_store_ready = vector_diagnostics.ready;
    health.vector_store_size = vector_diagnostics.size;
    health.vector_store_dimension = vector_diagnostics.dimension;
    health.query_expansion = rag_engine_->is_query_expansion_enabled();
    health.multi_query_retrieval = rag_engine_->is_multi_query_retrieval_enabled();
    health.embedding_reranker = rag_engine_->is_embedding_reranker_enabled();
    health.reranking = rag_engine_->is_reranking_enabled();
    health.xapian = rag_engine_->get_xapian_diagnostics();

    if (llm_client_ && llm_client_->is_enabled()) {
        health.llm_response_time_ms = llm_client_->provider_health_check();
        health.available_models = llm_client_->list_available_models();
        health.configured_model_available = llm_client_->configured_model_available();
        health.llm_provider = llm_client_->provider_name();
        health.llm_provider_available = health.llm_response_time_ms >= 0;
        health.llm_available = health.llm_provider_available;
        health.llm_ready = health.llm_provider_available &&
            ((health.llm_provider != "ollama" && health.available_models.empty()) ||
             health.configured_model_available);
        health.llm_model = llm_client_->get_model();
        health.llm_api_url = llm_client_->get_api_url();
        if (!health.llm_provider_available) {
            health.llm_status = "provider_unavailable";
        } else if (health.available_models.empty()) {
            health.llm_status = "models_unreported";
        } else if (!health.configured_model_available) {
            health.llm_status = "model_not_found";
        } else {
            health.llm_status = "ok";
        }
    }

    return health;
}

std::vector<RagServiceSourceInfo> RagService::sources() const {
    std::vector<RagServiceSourceInfo> output;
    if (!rag_engine_) {
        return output;
    }
    for (const auto& source : rag_engine_->getDataSources()) {
        output.push_back(RagServiceSourceInfo{
            source->getId(),
            data_source_type_to_string(source->getType()),
            source->getName(),
            source->count()
        });
    }
    return output;
}

RagServiceEmbeddingModelsResponse RagService::embeddingModels() const {
    RagServiceEmbeddingModelsResponse response;
    if (!rag_engine_) {
        response.success = false;
        return response;
    }

    const auto info = rag_engine_->get_embedding_model_info();
    response.active_model_id = info.active_model_id;
    response.effective_model_id = info.id;
    response.backend = info.backend;
    response.warnings = info.registry_warnings;

    RagServiceEmbeddingModelItem effective;
    effective.id = info.id;
    effective.backend = info.backend;
    effective.name = info.name;
    effective.version = info.version;
    effective.model_path = info.model_path;
    effective.tokenizer_path = info.tokenizer_path;
    effective.tokenizer_type = info.tokenizer_type;
    effective.pooling = info.pooling;
    effective.dimension = info.dimension;
    effective.max_seq_len = info.max_seq_len;
    effective.tokenizer_vocab_size = info.tokenizer_vocab_size;
    effective.effective_chunk_token_limit = info.effective_chunk_token_limit;
    effective.active = true;
    effective.ready = info.ready;
    effective.files_present = info.backend != "onnx" || (!info.model_path.empty() && !info.tokenizer_path.empty());
    effective.persistent_cache_enabled = info.persistent_cache_enabled;
    effective.status = info.status;
    effective.tokenizer_status = info.tokenizer_status;
    effective.model_signature = info.model_signature;

    const auto& registry = rag_engine_->get_embedding_model_registry();
    if (registry.models.empty()) {
        response.models.push_back(std::move(effective));
        return response;
    }

    bool included_active = false;
    for (const auto& [id, model] : registry.models) {
        RagServiceEmbeddingModelItem item;
        item.id = id;
        item.backend = model.backend;
        item.name = model.name;
        item.version = model.version;
        item.model_path = model.model_path;
        item.tokenizer_path = model.tokenizer_path;
        item.tokenizer_type = model.tokenizer_type;
        item.pooling = model.pooling;
        item.dimension = model.dimension;
        item.max_seq_len = model.max_seq_len;
        item.tokenizer_vocab_size = model.tokenizer_vocab_size;
        item.effective_chunk_token_limit = model.max_seq_len > 2 ? model.max_seq_len - 2 : model.max_seq_len;
        item.active = id == response.active_model_id;
        item.ready = item.active ? info.ready : (model.backend != "onnx" || model.files_present);
        item.discovered = model.discovered;
        item.files_present = model.files_present;
        item.persistent_cache_enabled = rag_engine_->get_embedding_config().persistent_cache_enabled;
        item.status = item.active ? info.status : (model.files_present ? "installed" : "not ready");
        item.tokenizer_status = item.active ? info.tokenizer_status : model.tokenizer_status;
        item.model_status = model.model_status;
        item.source = model.source;
        included_active = included_active || item.active;
        response.models.push_back(std::move(item));
    }
    if (!included_active) {
        response.models.push_back(std::move(effective));
    }
    return response;
}

RagServiceEmbeddingSwitchResponse RagService::switchEmbeddingModel(
    const std::string& model_id,
    bool reindex,
    bool force_reembed,
    const std::optional<std::string>& scan_path) {
    RagServiceEmbeddingSwitchResponse response;
    response.active_model_id = model_id;
    response.force_reembed = force_reembed;
    if (!rag_engine_) {
        response.message = "RAG engine is not configured";
        return response;
    }
    if (model_id.empty()) {
        response.message = "Embedding model id is required";
        return response;
    }

    response.previous_model_id = rag_engine_->get_embedding_model_id();
    if (!rag_engine_->switch_active_embedding_model(model_id, force_reembed)) {
        response.message = "Embedding model not found in registry: " + model_id;
        return response;
    }

    response.effective_model_id = rag_engine_->get_embedding_model_id();
    response.success = true;
    response.reindex_required = true;
    response.message = "Embedding model switched; reindex required";

    if (reindex) {
        auto indexed = indexProject(scan_path);
        response.reindexed = indexed.success;
        response.stats = indexed.stats;
        response.reindex_required = !indexed.success;
        response.message = indexed.success
            ? "Embedding model switched and project reindexed"
            : "Embedding model switched but reindex failed: " + indexed.message;
        response.success = indexed.success;
    }
    return response;
}

std::vector<QASource::QAPair> RagService::listQaPairs(size_t page, size_t per_page, const std::string& source_id) const {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        return sqlite_source_->getAllPairs(page, per_page);
    }
#endif
    if (!rag_engine_) {
        return {};
    }
    for (const auto& source : rag_engine_->getDataSources()) {
        if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
            if (source_id.empty() || qa->getId() == source_id) {
                auto pairs = qa->getAllPairs();
                const size_t start = (page > 0 ? page - 1 : 0) * per_page;
                if (start >= pairs.size()) {
                    return {};
                }
                const size_t end = std::min(start + per_page, pairs.size());
                return std::vector<QASource::QAPair>(pairs.begin() + static_cast<std::ptrdiff_t>(start),
                                                     pairs.begin() + static_cast<std::ptrdiff_t>(end));
            }
        }
    }
    return {};
}

RagServiceQaListResponse RagService::listQaPairs(const RagServiceQaListOptions& options) const {
    RagServiceQaListResponse response;
    response.limit = options.limit == 0 ? 25 : std::min<size_t>(options.limit, 500);
    response.offset = options.offset;
    response.source_id = options.source_id;

#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (options.source_id.empty() ||
                           options.source_id == sqlite_source_->getId() ||
                           options.source_id == sqlite_source_->getSourceId())) {
        SQLiteSource::QAListOptions sqlite_options;
        sqlite_options.query = options.query;
        sqlite_options.category = options.category;
        sqlite_options.tag = options.tag;
        sqlite_options.limit = response.limit;
        sqlite_options.offset = response.offset;
        auto listed = sqlite_source_->listQAPairs(sqlite_options);
        response.source_id = sqlite_source_->getId();
        response.items = std::move(listed.items);
        response.total = listed.total;
        response.limit = listed.limit;
        response.offset = listed.offset;
        response.has_more = response.offset + response.items.size() < response.total;
        return response;
    }
#endif

    if (!rag_engine_) {
        return response;
    }

    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    const std::string query = lower(options.query);
    const std::string category = options.category;
    const std::string tag = lower(options.tag);

    for (const auto& source : rag_engine_->getDataSources()) {
        if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
            if (!options.source_id.empty() && qa->getId() != options.source_id) {
                continue;
            }

            response.source_id = qa->getId();
            auto pairs = qa->getAllPairs();
            std::vector<QASource::QAPair> filtered;
            for (const auto& pair : pairs) {
                const std::string pair_category = pair.category.empty() ? "general" : pair.category;
                if (!category.empty() && pair_category != category) {
                    continue;
                }
                if (!tag.empty()) {
                    bool tag_match = false;
                    for (const auto& value : pair.tags) {
                        if (lower(value) == tag) {
                            tag_match = true;
                            break;
                        }
                    }
                    if (!tag_match) {
                        continue;
                    }
                }
                if (!query.empty()) {
                    const std::string haystack = lower(pair.id + " " + pair.question + " " + pair.answer + " " + pair_category);
                    if (haystack.find(query) == std::string::npos) {
                        continue;
                    }
                }
                filtered.push_back(pair);
            }

            response.total = filtered.size();
            if (response.offset < filtered.size()) {
                const size_t end = std::min(response.offset + response.limit, filtered.size());
                response.items.assign(filtered.begin() + static_cast<std::ptrdiff_t>(response.offset),
                                      filtered.begin() + static_cast<std::ptrdiff_t>(end));
            }
            response.has_more = response.offset + response.items.size() < response.total;
            return response;
        }
    }

    return response;
}

std::vector<RagServiceQaSuggestion> RagService::suggestQaPairs(const std::string& query,
                                                               size_t limit,
                                                               const std::string& source_id) const {
    std::vector<RagServiceQaSuggestion> suggestions;
    if (query.empty()) {
        return suggestions;
    }
    limit = limit == 0 ? 10 : std::min<size_t>(limit, 50);

#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        for (const auto& item : sqlite_source_->suggestQAPairs(query, limit)) {
            suggestions.push_back(RagServiceQaSuggestion{item.id, item.question, item.category});
        }
        return suggestions;
    }
#endif

    RagServiceQaListOptions options;
    options.query = query;
    options.source_id = source_id;
    options.limit = limit;
    auto listed = listQaPairs(options);
    for (const auto& pair : listed.items) {
        suggestions.push_back(RagServiceQaSuggestion{pair.id, pair.question, pair.category});
    }
    return suggestions;
}

std::vector<std::string> RagService::listQaCategories(const std::string& query,
                                                      size_t limit,
                                                      const std::string& source_id) const {
    limit = limit == 0 ? 50 : std::min<size_t>(limit, 200);
#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        return sqlite_source_->listQACategories(query, limit);
    }
#endif

    std::vector<std::string> categories;
    if (!rag_engine_) {
        return categories;
    }
    auto contains = [](const std::vector<std::string>& values, const std::string& value) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    const std::string lowered_query = lower(query);
    for (const auto& source : rag_engine_->getDataSources()) {
        if (auto qa = std::dynamic_pointer_cast<QASource>(source)) {
            if (!source_id.empty() && qa->getId() != source_id) {
                continue;
            }
            for (const auto& pair : qa->getAllPairs()) {
                const std::string category = pair.category.empty() ? "general" : pair.category;
                if (!lowered_query.empty() && lower(category).find(lowered_query) == std::string::npos) {
                    continue;
                }
                if (!contains(categories, category)) {
                    categories.push_back(category);
                    if (categories.size() >= limit) {
                        return categories;
                    }
                }
            }
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

std::vector<std::string> RagService::listQaTags(const std::string& query,
                                                size_t limit,
                                                const std::string& source_id) const {
    limit = limit == 0 ? 50 : std::min<size_t>(limit, 200);
#if QORNIX_HAS_SQLITE
    if (sqlite_source_ && (source_id.empty() || source_id == sqlite_source_->getId() || source_id == sqlite_source_->getSourceId())) {
        return sqlite_source_->listQATags(query, limit);
    }
#endif
    std::vector<std::string> tags;
    auto contains = [](const std::vector<std::string>& values, const std::string& value) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    const std::string lowered_query = lower(query);

    RagServiceQaListOptions options;
    options.source_id = source_id;
    options.limit = 500;
    size_t offset = 0;
    while (tags.size() < limit) {
        options.offset = offset;
        auto page = listQaPairs(options);
        if (page.items.empty()) {
            break;
        }
        for (const auto& pair : page.items) {
            for (const auto& tag : pair.tags) {
                if (tag.empty()) {
                    continue;
                }
                if (!lowered_query.empty() && lower(tag).find(lowered_query) == std::string::npos) {
                    continue;
                }
                if (!contains(tags, tag)) {
                    tags.push_back(tag);
                    if (tags.size() >= limit) {
                        break;
                    }
                }
            }
            if (tags.size() >= limit) {
                break;
            }
        }
        if (!page.has_more) {
            break;
        }
        offset += page.items.size();
    }
    std::sort(tags.begin(), tags.end());
    return tags;
}

bool RagService::addQaPair(const std::string& question,
                           const std::string& answer,
                           const std::string& category,
                           std::string* pair_id,
                           const std::vector<std::string>& tags,
                           const std::vector<std::string>& aliases,
                           const std::map<std::string, std::string>& metadata) {
    if (question.empty() || answer.empty()) {
        return false;
    }
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        const std::string id = qaPairId(sqlite_source_->getId());
        auto metadata_with_tags = metadata;
        if (!tags.empty()) {
            metadata_with_tags["tags"] = stringArrayJson(tags);
        }
        if (!sqlite_source_->addQAPair(id, question, answer, category, stringArrayJson(aliases), [&]() {
                boost::json::object object;
                for (const auto& [key, value] : metadata_with_tags) {
                    object[key] = value;
                }
                return boost::json::serialize(object);
            }())) {
            return false;
        }
        if (pair_id) {
            *pair_id = id;
        }
        return true;
    }
#endif
    return false;
}

bool RagService::updateQaPair(const std::string& pair_id,
                              const std::string& question,
                              const std::string& answer,
                              const std::string& category,
                              const std::vector<std::string>& tags,
                              const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& metadata) {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        auto metadata_with_tags = metadata;
        if (!tags.empty()) {
            metadata_with_tags["tags"] = stringArrayJson(tags);
        }
        boost::json::object metadata_json;
        for (const auto& [key, value] : metadata_with_tags) {
            metadata_json[key] = value;
        }
        return sqlite_source_->updateQAPair(pair_id,
                                            answer,
                                            category,
                                            aliases.empty() ? "" : stringArrayJson(aliases),
                                            question,
                                            metadata_with_tags.empty() ? "" : boost::json::serialize(metadata_json));
    }
#else
    (void)pair_id;
    (void)question;
    (void)answer;
    (void)category;
#endif
    return false;
}

bool RagService::deleteQaPair(const std::string& pair_id) {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        return sqlite_source_->deleteQAPair(pair_id);
    }
#else
    (void)pair_id;
#endif
    return false;
}

std::string RagService::exportQaPairsJson(const RagServiceQaListOptions& options) const {
    auto listed = listQaPairs(options);
    boost::json::array pairs;
    for (const auto& pair : listed.items) {
        boost::json::object object;
        object["id"] = pair.id;
        object["question"] = pair.question;
        object["answer"] = pair.answer;
        object["category"] = pair.category.empty() ? "general" : pair.category;
        boost::json::array aliases;
        for (const auto& alias : pair.aliases) {
            aliases.emplace_back(alias);
        }
        object["aliases"] = aliases;
        boost::json::array tags;
        for (const auto& tag : pair.tags) {
            tags.emplace_back(tag);
        }
        object["tags"] = tags;
        boost::json::object metadata;
        for (const auto& [key, value] : pair.metadata) {
            metadata[key] = value;
        }
        object["metadata"] = metadata;
        pairs.emplace_back(std::move(object));
    }
    boost::json::object root;
    root["success"] = true;
    root["source_id"] = listed.source_id;
    root["total"] = static_cast<std::int64_t>(listed.total);
    root["exported"] = static_cast<std::int64_t>(pairs.size());
    root["pairs"] = pairs;
    return boost::json::serialize(root);
}

RagServiceQaImportResponse RagService::importQaPairsJson(const std::string& json,
                                                         const std::string& default_category) {
    RagServiceQaImportResponse response;
    try {
        auto parsed = boost::json::parse(json);
        const boost::json::array* array = nullptr;
        if (parsed.is_array()) {
            array = &parsed.as_array();
        } else if (parsed.is_object() && parsed.as_object().contains("pairs") && parsed.as_object().at("pairs").is_array()) {
            array = &parsed.as_object().at("pairs").as_array();
        }
        if (!array) {
            response.errors = 1;
            response.error_messages.push_back("Expected JSON array or object with pairs array");
            return response;
        }
        for (const auto& value : *array) {
            if (!value.is_object()) {
                ++response.errors;
                response.error_messages.push_back("Skipped non-object QA item");
                continue;
            }
            const auto& object = value.as_object();
            if (!object.contains("question") || !object.contains("answer")) {
                ++response.errors;
                response.error_messages.push_back("Skipped QA item without question/answer");
                continue;
            }
            const std::string question = object.at("question").as_string().c_str();
            const std::string answer = object.at("answer").as_string().c_str();
            std::string category = default_category.empty() ? "general" : default_category;
            if (object.contains("category") && object.at("category").is_string()) {
                category = object.at("category").as_string().c_str();
            }
            auto metadata = jsonStringMap(object, "metadata");
            const auto tags = jsonStringArray(object, "tags");
            const auto aliases = jsonStringArray(object, "aliases");
            std::string id;
            if (addQaPair(question, answer, category, &id, tags, aliases, metadata)) {
                ++response.imported;
                response.imported_ids.push_back(id);
            } else {
                ++response.duplicates;
            }
        }
        response.success = response.errors == 0;
    } catch (const std::exception& e) {
        response.errors = 1;
        response.error_messages.push_back(e.what());
    }
    return response;
}

std::vector<QASource::QAPair> RagService::searchQa(const std::string& query, size_t limit) const {
#if QORNIX_HAS_SQLITE
    if (sqlite_source_) {
        auto pairs = sqlite_source_->searchByQuestion(query);
        if (pairs.size() > limit) {
            pairs.resize(limit);
        }
        return pairs;
    }
#else
    (void)query;
    (void)limit;
#endif
    return {};
}
