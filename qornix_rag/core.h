/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * Qornix RAG - Retrieval-Augmented Generation system in C++
 *
 * System core: indexing, search, context building
 */

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <memory>
#include <chrono>
#include <iostream>
#include <regex>
#include <iomanip>
#include <queue>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <optional>
#include <utility>

// Include xapian first
#include <xapian.h>
#include <boost/json.hpp>

#include "ingestion_pipeline.h"
#include "vector_store.h"

#ifdef QORNIX_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

// Hybrid search configuration
struct HybridSearchConfig {
    float vector_weight = 0.6f; // Vector search weight (HNSW)
    float text_weight = 0.4f; // Text search weight (Xapian)
    size_t top_k = 10; // Number of results from each method
    float min_score_threshold = 0.1f; // Minimum score for inclusion in result
    bool use_hybrid = true; // Hybrid search usage flag
    bool use_query_expansion = true;
    bool xapian_enabled = true;
    std::string xapian_language = "auto"; // auto | none | Xapian language name or alias, e.g. english/en
    bool xapian_stemming = true;
    std::string xapian_stemming_strategy = "some"; // none | some | all
    bool xapian_cjk_ngrams = false;
    bool xapian_word_breaks = true;
    bool xapian_spelling = false;
    bool xapian_metadata_prefixes = true;
    bool use_reranking = true;
    size_t rerank_input_multiplier = 3;
    float rerank_path_boost = 0.15f;
    float rerank_metadata_boost = 0.10f;
    float rerank_exact_content_boost = 0.05f;
    bool use_multi_query_retrieval = true;
    size_t multi_query_max_variants = 4;
    bool use_embedding_reranker = true;
    float rerank_embedding_boost = 0.20f;
    float rerank_coverage_boost = 0.08f;
};

struct EmbeddingConfig {
    std::string backend = "tfidf"; // tfidf | onnx
    std::string active_model_id;
    std::string model_id;
    std::string model_name;
    std::string model_version;
    std::string model_path = "models/semantic_model.onnx";
    std::string tokenizer_path = "models/tokenizer.json";
    std::string tokenizer_type = "basic_wordpiece";
    std::string pooling = "mean"; // mean | cls
    size_t dimension = 0; // 0 means discover from backend/output
    size_t max_seq_len = 256;
    size_t onnx_threads = 1;
    std::string models_dir = "models";
    bool auto_discover_models = true;
    bool validate_model_files = true;
    bool persistent_cache_enabled = true;
    size_t chunk_token_margin = 2;
    bool normalize_embeddings = true;
    bool enable_fallback = true;
    bool lowercase_tokens = true;
};

struct EmbeddingModelDefinition {
    std::string id;
    std::string backend = "onnx";
    std::string name;
    std::string version;
    std::string model_path;
    std::string tokenizer_path;
    std::string tokenizer_type = "basic_wordpiece";
    std::string pooling = "mean";
    size_t dimension = 0;
    size_t max_seq_len = 256;
    size_t onnx_threads = 1;
    bool normalize_embeddings = true;
    bool lowercase_tokens = true;
    bool enable_fallback = true;
    bool discovered = false;
    bool files_present = false;
    size_t tokenizer_vocab_size = 0;
    std::string tokenizer_status;
    std::string model_status;
    std::string license;
    std::string source;
};

struct EmbeddingModelInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string backend;
    std::string model_path;
    std::string tokenizer_path;
    std::string tokenizer_type;
    std::string pooling;
    size_t dimension = 0;
    size_t max_seq_len = 0;
    size_t tokenizer_vocab_size = 0;
    size_t effective_chunk_token_limit = 0;
    bool ready = false;
    bool persistent_cache_enabled = false;
    std::string status;
    std::string tokenizer_status;
    std::string model_signature;
    std::string active_model_id;
    size_t registry_size = 0;
    std::vector<std::string> registry_model_ids;
    std::vector<std::string> registry_warnings;
};

struct EmbeddingModelRegistry {
    std::map<std::string, EmbeddingModelDefinition> models;
    std::vector<std::string> warnings;
};

inline void apply_embedding_model_definition(EmbeddingConfig& config,
                                             const EmbeddingModelDefinition& model) {
    config.active_model_id = model.id;
    config.model_id = model.id;
    config.backend = model.backend.empty() ? config.backend : model.backend;
    config.model_name = model.name;
    config.model_version = model.version;
    if (!model.model_path.empty()) {
        config.model_path = model.model_path;
    }
    if (!model.tokenizer_path.empty()) {
        config.tokenizer_path = model.tokenizer_path;
    }
    config.tokenizer_type = model.tokenizer_type;
    config.pooling = model.pooling;
    config.dimension = model.dimension;
    config.max_seq_len = model.max_seq_len;
    config.onnx_threads = model.onnx_threads;
    config.normalize_embeddings = model.normalize_embeddings;
    config.lowercase_tokens = model.lowercase_tokens;
    config.enable_fallback = model.enable_fallback;
}

struct VectorStoreConfig {
    std::string backend = "local_hnsw"; // local_hnsw, faiss, qdrant, pgvector
    std::string index_path;
    std::string metadata_path;
    std::string endpoint;
    std::string api_key;
    std::string collection = "qornix_rag_vectors";
    std::string connection_string;
    std::string table = "qornix_rag_vectors";
    std::string distance = "Cosine";
    size_t upsert_batch_size = 512;
    bool recreate = false;
    bool auto_load = true;
    bool auto_save = false;
};

struct RagEngineConfig {
    HybridSearchConfig search;
    EmbeddingConfig embedding;
    EmbeddingModelRegistry embedding_registry;
    VectorStoreConfig vector_store;
    size_t max_file_size_kb = 4096;
};

struct Document {
    std::string path;
    std::string relative_path;
    std::string content;
    std::string type;
    std::string language;
    size_t size_bytes;
    size_t lines_count;
    std::string hash;
    std::chrono::system_clock::time_point last_modified;
    std::vector<float> embedding; // Vector representation
    std::map<std::string, std::string> metadata; // Additional metadata

    Document() : size_bytes(0), lines_count(0) {
    }
};

#include "document_chunker.h"

/**
 * Type of data source (Phase 4).
 */
enum class DataSourceType {
    FILESYSTEM,   // Files on disk (code, configs)
    QA_KB,        // QA pairs knowledge base
    TEXT_DOCS,    // Text documents (.md, .txt, .rst)
    DATABASE,     // Database source
    MEMORY,       // In-memory documents
    CUSTOM        // User-defined source
};

// DataSource class is defined below to avoid circular dependency

/**
 * Convert DataSourceType to string.
 */
inline std::string data_source_type_to_string(DataSourceType type) {
    switch (type) {
        case DataSourceType::FILESYSTEM: return "FILESYSTEM";
        case DataSourceType::QA_KB:      return "QA_KB";
        case DataSourceType::TEXT_DOCS:  return "TEXT_DOCS";
        case DataSourceType::DATABASE:   return "DATABASE";
        case DataSourceType::MEMORY:     return "MEMORY";
        case DataSourceType::CUSTOM:     return "CUSTOM";
        default: return "UNKNOWN";
    }
}

/**
 * Abstract interface for RAG data sources.
 * All data sources must implement this interface to be used with RagEngine.
 */
class DataSource {
public:
    virtual ~DataSource() = default;

    virtual std::vector<Document> getDocuments() = 0;
    virtual void addDocument(const Document& doc) = 0;
    virtual void removeDocument(const std::string& id) = 0;
    virtual DataSourceType getType() const = 0;
    virtual size_t count() const = 0;
    virtual std::string getId() const = 0;
    virtual std::string getName() const = 0;
    virtual bool initialize() = 0;
    virtual void cleanup() = 0;

    virtual void setProgressCallback(std::function<void(size_t current, size_t total)> callback) {
        progress_callback_ = std::move(callback);
    }

protected:
    std::function<void(size_t current, size_t total)> progress_callback_;
};


struct XapianSearchDiagnostics {
    bool enabled = true;
    bool ready = false;
    std::string status = "not_built";
    std::string detail;
    std::string language = "auto";
    std::string effective_index_language = "none";
    std::string effective_query_language = "none";
    bool stemming = true;
    std::string stemming_strategy = "some";
    bool cjk_ngrams = false;
    bool word_breaks = true;
    bool spelling = false;
    bool metadata_prefixes = true;
    size_t documents_indexed = 0;
};

struct SearchResult {
    Document document;
    double score;
    double vector_score;
    double text_score;
    double fused_score;
    std::string snippet;
    std::vector<std::pair<size_t, size_t>> match_locations;

    // Phase 4: Source information
    DataSourceType source_type = DataSourceType::FILESYSTEM;
    std::string source_id;

    SearchResult() : score(0.0), vector_score(0.0), text_score(0.0), fused_score(0.0) {
    }
};

struct MetadataFilter {
    // key examples: type, language, source_path, page, sheet, slide,
    // chunk_symbol_name, metadata_contract, image_ocr_confidence_avg.
    std::string key;
    std::string value;
    // equals | contains | prefix | exists | not_empty | gte | lte
    std::string op = "equals";

    MetadataFilter() = default;
    MetadataFilter(std::string filter_key, std::string filter_value, std::string filter_op = "equals")
        : key(std::move(filter_key)), value(std::move(filter_value)), op(std::move(filter_op)) {}
};

struct SearchOptions {
    size_t top_k = 10;
    std::vector<MetadataFilter> filters;
    bool include_metadata = true;
};

namespace qornix_rag_xapian_detail {

inline std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline std::string normalizeLanguageName(std::string language) {
    language = toLowerAscii(std::move(language));
    if (language.empty() || language == "default") return "auto";
    if (language == "none" || language == "off" || language == "disabled" || language == "false") return "none";
    if (language == "auto" || language == "detect") return "auto";
    if (language == "ar") return "arabic";
    if (language == "hy") return "armenian";
    if (language == "eu") return "basque";
    if (language == "ca") return "catalan";
    if (language == "da") return "danish";
    if (language == "nl") return "dutch";
    if (language == "en" || language == "eng") return "english";
    if (language == "eo") return "esperanto";
    if (language == "et") return "estonian";
    if (language == "fi") return "finnish";
    if (language == "fr") return "french";
    if (language == "de") return "german";
    if (language == "el") return "greek";
    if (language == "hi") return "hindi";
    if (language == "hu") return "hungarian";
    if (language == "id") return "indonesian";
    if (language == "ga") return "irish";
    if (language == "it") return "italian";
    if (language == "lt") return "lithuanian";
    if (language == "ne") return "nepali";
    if (language == "no" || language == "nb" || language == "nn") return "norwegian";
    if (language == "pl") return "polish";
    if (language == "pt") return "portuguese";
    if (language == "ro") return "romanian";
    if (language == "ru" || language == "rus") return "russian";
    if (language == "sr") return "serbian";
    if (language == "es") return "spanish";
    if (language == "sv") return "swedish";
    if (language == "ta") return "tamil";
    if (language == "tr") return "turkish";
    if (language == "yi") return "yiddish";
    return language;
}

inline bool containsCyrillicUtf8(const std::string& text) {
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        const auto b1 = static_cast<unsigned char>(text[i + 1]);
        if ((b0 == 0xD0 && b1 >= 0x80) || (b0 == 0xD1 && b1 <= 0xBF)) return true;
    }
    return false;
}

inline bool isSupportedStemLanguage(const std::string& language) {
    const auto normalized = normalizeLanguageName(language);
    if (normalized.empty() || normalized == "auto" || normalized == "none") return false;

    static std::mutex cache_mutex;
    static std::unordered_map<std::string, bool> cache;

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        const auto it = cache.find(normalized);
        if (it != cache.end()) {
            return it->second;
        }
    }

    bool supported = false;
    try {
        (void)Xapian::Stem(normalized);
        supported = true;
    } catch (const Xapian::Error&) {
        supported = false;
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[normalized] = supported;
    }
    return supported;
}

inline std::string detectLanguage(const std::string& configured,
                                  const std::string& content,
                                  const std::string& fallback_language = {}) {
    const auto normalized = normalizeLanguageName(configured);
    if (normalized != "auto") return normalized;

    // Document::language is used by qornix_rag for both natural languages and
    // technical/file languages (for example: python, javascript, html, xml,
    // pdf, image, c/c++ header).  Xapian::Stem only accepts natural-language
    // stemmer identifiers, so auto mode must not pass technical languages
    // through to Xapian.  Use the fallback only when the installed Xapian
    // library confirms it is a supported stemmer language.
    const auto fallback = normalizeLanguageName(fallback_language);
    if (isSupportedStemLanguage(fallback)) {
        return fallback;
    }

    if (containsCyrillicUtf8(content)) return "russian";
    return "english";
}

inline Xapian::TermGenerator::stem_strategy termStemStrategy(const HybridSearchConfig& config) {
    if (!config.xapian_stemming) return Xapian::TermGenerator::STEM_NONE;
    const auto strategy = toLowerAscii(config.xapian_stemming_strategy);
    if (strategy == "all") return Xapian::TermGenerator::STEM_ALL;
    if (strategy == "none" || strategy == "off" || strategy == "false") return Xapian::TermGenerator::STEM_NONE;
    return Xapian::TermGenerator::STEM_SOME;
}

inline Xapian::QueryParser::stem_strategy queryStemStrategy(const HybridSearchConfig& config) {
    if (!config.xapian_stemming) return Xapian::QueryParser::STEM_NONE;
    const auto strategy = toLowerAscii(config.xapian_stemming_strategy);
    if (strategy == "all") return Xapian::QueryParser::STEM_ALL;
    if (strategy == "none" || strategy == "off" || strategy == "false") return Xapian::QueryParser::STEM_NONE;
    return Xapian::QueryParser::STEM_SOME;
}

inline void configureTermGenerator(Xapian::TermGenerator& termgen,
                                   const HybridSearchConfig& config,
                                   const std::string& language) {
    if (!config.xapian_stemming) return;
    const auto normalized = normalizeLanguageName(language);
    if (normalized.empty() || normalized == "none" || normalized == "auto") return;
    Xapian::Stem stemmer(normalized);
    termgen.set_stemmer(stemmer);
    termgen.set_stemming_strategy(termStemStrategy(config));
}

inline void configureQueryParser(Xapian::QueryParser& parser,
                                 const HybridSearchConfig& config,
                                 const std::string& language) {
    if (!config.xapian_stemming) return;
    const auto normalized = normalizeLanguageName(language);
    if (normalized.empty() || normalized == "none" || normalized == "auto") return;
    Xapian::Stem stemmer(normalized);
    parser.set_stemmer(stemmer);
    parser.set_stemming_strategy(queryStemStrategy(config));
}

inline bool utf8NextCodepoint(const std::string& text, size_t& i, uint32_t& cp, std::string& bytes) {
    if (i >= text.size()) return false;
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t len = 1;
    cp = c;
    if ((c & 0x80) == 0) {
        len = 1;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
        len = 2;
        cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
        len = 3;
        cp = ((c & 0x0F) << 12) |
             ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(text[i + 2]) & 0x3F);
    } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
        len = 4;
        cp = ((c & 0x07) << 18) |
             ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
             ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(text[i + 3]) & 0x3F);
    } else {
        len = 1;
        cp = c;
    }
    bytes = text.substr(i, len);
    i += len;
    return true;
}

inline bool isCjkCodepoint(uint32_t cp) {
    return (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0x3040 && cp <= 0x30FF) ||
           (cp >= 0xAC00 && cp <= 0xD7AF);
}

inline std::vector<std::string> extractCjkNgrams(const std::string& text, size_t n = 2) {
    std::vector<std::string> grams;
    std::vector<std::string> run;
    auto flush = [&]() {
        if (run.empty()) return;
        if (run.size() < n) {
            grams.insert(grams.end(), run.begin(), run.end());
        } else {
            for (size_t i = 0; i + n <= run.size(); ++i) {
                std::string gram;
                for (size_t j = 0; j < n; ++j) gram += run[i + j];
                grams.push_back(std::move(gram));
            }
        }
        run.clear();
    };
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = 0;
        std::string bytes;
        if (!utf8NextCodepoint(text, i, cp, bytes)) break;
        if (isCjkCodepoint(cp)) {
            run.push_back(bytes);
        } else {
            flush();
        }
    }
    flush();
    std::sort(grams.begin(), grams.end());
    grams.erase(std::unique(grams.begin(), grams.end()), grams.end());
    return grams;
}

inline std::string safeTermValue(const std::string& value) {
    std::string out;
    out.reserve(std::min<size_t>(value.size(), 180));
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/' || c == ':' || c == '#') {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
        if (out.size() >= 180) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "empty" : out;
}

inline void addExactTerm(Xapian::Document& xdoc,
                         const std::string& prefix,
                         const std::string& value,
                         Xapian::termcount wdf = 1) {
    xdoc.add_term(prefix + safeTermValue(value), wdf);
}

inline Xapian::Query orWithTerm(Xapian::Query base, const std::string& term) {
    Xapian::Query next(term);
    if (base.empty()) return next;
    return Xapian::Query(Xapian::Query::op::OP_OR, base, next);
}

inline void addCjkTerms(Xapian::Document& xdoc, const std::string& text, const std::string& prefix) {
    for (const auto& gram : extractCjkNgrams(text)) {
        if (!gram.empty()) xdoc.add_term(prefix + gram, 1);
    }
}

inline std::vector<std::string> splitWordBreakTokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    auto flush = [&]() {
        if (current.size() >= 2) tokens.push_back(toLowerAscii(current));
        current.clear();
    };
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (std::isalnum(c)) {
            if (!current.empty() && std::islower(static_cast<unsigned char>(current.back())) && std::isupper(c)) {
                flush();
            }
            current.push_back(static_cast<char>(c));
        } else if (c == '_' || c == '-' || c == '.' || c == '/' || c == ':' || c == '#') {
            flush();
        } else {
            flush();
        }
    }
    flush();
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

inline void addWordBreakTerms(Xapian::Document& xdoc, const std::string& text, const std::string& prefix) {
    for (const auto& token : splitWordBreakTokens(text)) {
        xdoc.add_term(prefix + safeTermValue(token), 1);
    }
}

inline void configurePrefixes(Xapian::QueryParser& parser) {
    parser.add_prefix("path", "QPATH");
    parser.add_prefix("file", "QPATH");
    parser.add_prefix("source", "QSOURCE");
    parser.add_prefix("type", "QTYPE");
    parser.add_prefix("lang", "QLANG");
    parser.add_prefix("language", "QLANG");
    parser.add_prefix("meta", "QMETA");
    parser.add_prefix("metadata", "QMETA");
    parser.add_prefix("symbol", "QSYMBOL");
    parser.add_prefix("page", "QPAGE");
    parser.add_prefix("sheet", "QSHEET");
    parser.add_prefix("slide", "QSLIDE");
}

} // namespace qornix_rag_xapian_detail

struct ProjectStats {
    size_t total_files;
    size_t total_lines;
    size_t total_size_bytes;
    size_t index_duration_ms = 0;
    size_t indexed_chunks = 0;
    size_t reused_embeddings = 0;
    size_t generated_embeddings = 0;
    size_t stale_embeddings = 0;
    std::map<std::string, size_t> files_by_type;
    std::map<std::string, size_t> files_by_directory;
    std::string last_indexed;
};

class HashCalculator {
public:
    static std::string compute_md5(const std::string &data) {
        unsigned long hash = 5381;
        for (char c: data) {
            hash = ((hash << 5) + hash) + c;
        }
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
};

class TfidfVectorizer {
private:
    std::unordered_map<std::string, int> term_frequency_;
    std::unordered_map<std::string, int> document_frequency_;
    size_t total_terms_ = 0;
    size_t num_documents_ = 0;

    std::set<std::string> stop_words_ = {
        "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "could", "should",
        "i", "you", "he", "she", "it", "we", "they", "what", "which", "who",
        "this", "that", "these", "those", "am", "of", "at", "by", "for", "with",
        "about", "against", "between", "into", "through", "during", "before", "after",
        "above", "below", "to", "from", "up", "down", "in", "out", "on", "off",
        "over", "under", "again", "further", "then", "once", "here", "there", "when",
        "where", "why", "how", "all", "each", "few", "more", "most", "other", "some",
        "such", "no", "nor", "not", "only", "own", "same", "so", "than", "too", "very",
        "can", "just", "don", "now",

        "int", "void", "return", "if", "else", "for", "while", "switch", "case",
        "break", "continue", "default", "goto", "sizeof", "typedef", "struct",
        "class", "public", "private", "protected", "virtual", "static", "const",
        "new", "delete", "try", "catch", "throw", "namespace", "using", "include",
        "define", "ifdef", "ifndef", "endif", "pragma",

        "и", "в", "во", "не", "что", "он", "на", "я", "с", "со", "как", "а", "то",
        "но", "да", "ты", "к", "у", "же", "вы", "за", "бы", "по", "только", "ее",
        "мне", "было", "вот", "от", "меня", "еще", "нет", "о", "из", "ему", "теперь",
        "когда", "даже", "ну", "вдруг", "ли", "если", "уже", "или", "ни", "быть",
        "был", "него", "до", "вас", "нибудь", "опять", "уж", "вам", "ведь", "там",
        "потом", "себя", "ничего", "ей", "может", "они", "тут", "где", "есть", "надо",
        "ней", "для", "мы", "тебя", "их", "им", "более", "всегда", "конечно", "всю",
        "между"
    };

public:
    int get_document_frequency(const std::string& term) const {
        auto it = document_frequency_.find(term);
        return it != document_frequency_.end() ? it->second : 0;
    }

    size_t get_num_documents() const {
        return num_documents_;
    }

    std::vector<std::string> tokenize(const std::string &text) const {
        std::vector<std::string> tokens;
        std::string current_token;

        for (size_t i = 0; i < text.length(); ++i) {
            char c = text[i];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                current_token += std::tolower(static_cast<unsigned char>(c));
            } else {
                if (!current_token.empty() && current_token.length() >= 3) {
                    if (stop_words_.find(current_token) == stop_words_.end()) {
                        tokens.push_back(current_token);
                    }
                }
                current_token.clear();
            }
        }

        if (!current_token.empty() && current_token.length() >= 3) {
            if (stop_words_.find(current_token) == stop_words_.end()) {
                tokens.push_back(current_token);
            }
        }

        return tokens;
    }

    std::vector<std::string> extract_code_identifiers(const std::string &content) const {
        std::vector<std::string> identifiers;

        try {
            std::regex func_regex(R"(\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\()");
            auto func_begin = std::sregex_iterator(content.begin(), content.end(), func_regex);
            auto func_end = std::sregex_iterator();

            for (auto it = func_begin; it != func_end; ++it) {
                std::string func_name = (*it)[1].str();
                if (func_name.length() >= 3 && stop_words_.find(func_name) == stop_words_.end()) {
                    identifiers.push_back(func_name);
                }
            }

            std::regex class_regex(R"(\bclass\s+([a-zA-Z_][a-zA-Z0-9_]*))");
            auto class_begin = std::sregex_iterator(content.begin(), content.end(), class_regex);
            auto class_end = std::sregex_iterator();

            for (auto it = class_begin; it != class_end; ++it) {
                std::string class_name = (*it)[1].str();
                if (class_name.length() >= 3 && stop_words_.find(class_name) == stop_words_.end()) {
                    identifiers.push_back(class_name);
                }
            }
        } catch (const std::regex_error &e) {
            auto tokens = tokenize(content);
            for (const auto &token: tokens) {
                if (token.length() > 4 && std::isupper(token[0])) {
                    identifiers.push_back(token);
                }
            }
        }

        return identifiers;
    }

    void index_document(const std::string &doc_id, const std::string &content) {
        (void)doc_id;

        auto terms = tokenize(content);
        auto identifiers = extract_code_identifiers(content);

        std::map<std::string, int> local_term_freq;
        std::set<std::string> unique_terms;

        for (const auto &term: terms) {
            local_term_freq[term]++;
            total_terms_++;
            unique_terms.insert(term);
        }

        for (const auto &ident: identifiers) {
            local_term_freq[ident] += 3;
            total_terms_ += 3;
            unique_terms.insert(ident);
        }

        for (const auto &[term, count]: local_term_freq) {
            term_frequency_[term] += count;
            document_frequency_[term]++;
        }

        num_documents_++;
    }

    double compute_bm25_score(const std::string &doc_content,
                              const std::vector<std::string> &query_terms) const {
        if (num_documents_ == 0) return 0.0;

        auto doc_tokens = tokenize(doc_content);
        auto doc_identifiers = extract_code_identifiers(doc_content);

        std::map<std::string, double> doc_term_weights;
        for (const auto &token: doc_tokens) {
            doc_term_weights[token] += 1.0;
        }
        for (const auto &ident: doc_identifiers) {
            doc_term_weights[ident] += 2.0;
        }

        double total_score = 0.0;
        const double k1 = 1.5;
        const double b = 0.75;

        double avg_doc_length = total_terms_ / static_cast<double>(num_documents_);
        double doc_length = 0.0;
        for (const auto &[term, weight]: doc_term_weights) {
            doc_length += weight;
        }

        for (const auto &query_term: query_terms) {
            auto df_it = document_frequency_.find(query_term);
            if (df_it == document_frequency_.end()) continue;

            auto tf_it = doc_term_weights.find(query_term);
            if (tf_it == doc_term_weights.end() || tf_it->second == 0) continue;

            double tf = tf_it->second;
            double idf = std::log((num_documents_ - df_it->second + 0.5) /
                                  (df_it->second + 0.5) + 1.0);

            double numerator = tf * (k1 + 1.0);
            double denominator = tf + k1 * (1.0 - b + b * (doc_length / avg_doc_length));

            total_score += idf * (numerator / denominator);
        }

        return total_score;
    }
};

class RagEngine {
private:
    std::vector<Document> documents_;
    std::unordered_map<std::string, size_t> path_to_index_;
    TfidfVectorizer vectorizer_;

    std::unique_ptr<VectorStore> vector_store_;

    std::unique_ptr<Xapian::WritableDatabase> xapian_db_;
    HybridSearchConfig search_config_;
    EmbeddingConfig embedding_config_;
    EmbeddingModelRegistry embedding_registry_;
    VectorStoreConfig vector_store_config_;
    size_t max_file_size_bytes_ = 4096 * 1024;
    bool is_hybrid_indexed_ = false;
    mutable std::mutex mutex_;
    bool is_indexed_ = false;
    std::string indexed_project_root_ = ".";
    const std::atomic<bool>* stop_flag_ = nullptr;
    size_t last_index_duration_ms_ = 0;
    size_t last_reused_embeddings_ = 0;
    size_t last_generated_embeddings_ = 0;
    size_t last_stale_embeddings_ = 0;
    bool force_reembed_next_index_ = false;
    qornix::rag::IngestionJobResult last_ingestion_result_;

    // ============================================
    // Phase 4: Data Source Support
    // ============================================
    std::vector<std::shared_ptr<DataSource>> data_sources_;
    mutable std::mutex sources_mutex_;
    std::unordered_map<std::string, int64_t> tokenizer_vocab_;
    std::string tokenizer_model_type_ = "basic_wordpiece";
    std::string tokenizer_status_message_ = "not loaded";
    size_t tokenizer_vocab_size_ = 0;
    int64_t unk_token_id_ = 100;
    int64_t cls_token_id_ = 101;
    int64_t sep_token_id_ = 102;
    int64_t pad_token_id_ = 0;
    size_t embedding_dim_ = 256;
    bool onnx_ready_ = false;
    std::string onnx_status_message_ = "not initialized";
    EmbeddingModelInfo embedding_model_;
    std::string last_vector_store_status_ = "not initialized";
    XapianSearchDiagnostics xapian_diagnostics_;

#ifdef QORNIX_HAS_ONNX
    std::unique_ptr<Ort::Env> ort_env_;
    std::unique_ptr<Ort::Session> ort_session_;
    std::unique_ptr<Ort::SessionOptions> ort_session_options_;
    std::vector<std::string> ort_input_names_storage_;
    std::vector<std::string> ort_output_names_storage_;
    std::vector<const char*> ort_input_names_;
    std::vector<const char*> ort_output_names_;
#endif

    static constexpr size_t EMBEDDING_DIM = 256;

    const std::vector<std::string> source_extensions_ = {
        ".cpp", ".c", ".cc", ".cxx", ".c++",
        ".h", ".hpp", ".hxx", ".h++",
        ".java", ".cs", ".py", ".js", ".ts",
        ".go", ".rs", ".swift", ".kt", ".scala"
    };

    const std::vector<std::string> config_extensions_ = {
        ".txt", ".cmake", ".ini", ".cfg", ".conf",
        ".xml", ".json", ".yaml", ".yml", ".toml",
        ".md", ".rst", ".adoc"
    };

    bool should_skip_directory(const std::string &path) const {
        const std::vector<std::string> skip_dirs = {
            "cmake-build", ".git", "build", "__pycache__",
            "node_modules", ".venv", "dist", "bin", "obj",
            "qornix_rag/models", "/models/"
        };

        for (const auto &skip: skip_dirs) {
            if (path.find(skip) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    bool is_source_file(const std::string &ext) const {
        return std::find(source_extensions_.begin(), source_extensions_.end(), ext)
               != source_extensions_.end();
    }

    bool is_config_file(const std::string &ext) const {
        return std::find(config_extensions_.begin(), config_extensions_.end(), ext)
               != config_extensions_.end();
    }

    std::string get_language_from_extension(const std::string &ext) const {
        static const std::map<std::string, std::string> lang_map = {
            {".cpp", "C++"}, {".c", "C"}, {".h", "C/C++ Header"},
            {".hpp", "C++ Header"}, {".java", "Java"}, {".py", "Python"},
            {".js", "JavaScript"}, {".ts", "TypeScript"}
        };

        auto it = lang_map.find(ext);
        return it != lang_map.end() ? it->second : "Unknown";
    }

    Document read_file(const std::string &path, const std::string &type,
                       const std::string &language) {
        Document doc;
        doc.path = path;
        doc.type = type;
        doc.language = language;

        try {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return doc;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            doc.content = buffer.str();

            doc.size_bytes = doc.content.length();
            doc.lines_count = std::count(doc.content.begin(), doc.content.end(), '\n') + 1;
            doc.hash = HashCalculator::compute_md5(doc.content);

            if (path.find("/src/") != std::string::npos) {
                auto pos = path.find("/src/");
                doc.relative_path = path.substr(pos + 5);
            } else if (path.find("/include/") != std::string::npos) {
                auto pos = path.find("/include/");
                doc.relative_path = path.substr(pos + 9);
            } else {
                doc.relative_path = std::filesystem::relative(path).string();
            }

            auto file_time = std::filesystem::last_write_time(path);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            doc.last_modified = sctp;
        } catch (const std::exception &e) {
            std::cerr << "Error reading file " << path << ": " << e.what() << std::endl;
        }

        return doc;
    }

    std::string extract_snippet(const Document &doc,
                                const std::vector<std::string> &query_terms,
                                size_t context_lines = 3) const {
        auto lines = split_lines(doc.content);

        for (size_t i = 0; i < lines.size(); ++i) {
            std::string line_lower = lines[i];
            std::transform(line_lower.begin(), line_lower.end(),
                           line_lower.begin(), ::tolower);

            for (const auto &term: query_terms) {
                if (line_lower.find(term) != std::string::npos) {
                    std::stringstream snippet;

                    size_t start = i > context_lines ? i - context_lines : 0;
                    size_t end = std::min(i + context_lines + 1, lines.size());

                    for (size_t j = start; j < end; ++j) {
                        snippet << std::setw(4) << (j + 1) << " | " << lines[j] << "\n";
                    }

                    return snippet.str();
                }
            }
        }

        std::stringstream snippet;
        size_t max_lines = std::min(size_t(20), lines.size());
        for (size_t i = 0; i < max_lines; ++i) {
            snippet << std::setw(4) << (i + 1) << " | " << lines[i] << "\n";
        }
        return snippet.str();
    }

    std::vector<std::pair<size_t, size_t>> find_match_locations(
        const Document &doc, const std::vector<std::string> &query_terms) const {
        std::vector<std::pair<size_t, size_t>> locations;
        auto lines = split_lines(doc.content);

        for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
            std::string line_lower = lines[line_idx];
            std::transform(line_lower.begin(), line_lower.end(),
                           line_lower.begin(), ::tolower);

            for (const auto &term: query_terms) {
                size_t pos = 0;
                while ((pos = line_lower.find(term, pos)) != std::string::npos) {
                    locations.emplace_back(line_idx + 1, pos);
                    pos++;
                }
            }
        }

        return locations;
    }

    std::vector<std::string> split_lines(const std::string &text) const {
        std::vector<std::string> lines;
        std::stringstream ss(text);
        std::string line;

        while (std::getline(ss, line)) {
            lines.push_back(line);
        }

        return lines;
    }

public:
    explicit RagEngine(const RagEngineConfig &config = RagEngineConfig())
        : search_config_(config.search),
          embedding_config_(config.embedding),
          embedding_registry_(config.embedding_registry),
          vector_store_config_(config.vector_store),
          max_file_size_bytes_(config.max_file_size_kb * 1024) {
        apply_active_embedding_model();
        initialize_embedding_backend();
    }

    ~RagEngine() {
        if (vector_store_) {
            vector_store_->clear();
        }
    }

    std::vector<float> generate_tfidf_embedding(const std::string &content) {
        if (stop_flag_ && !stop_flag_->load()) {
            return {};
        }
        auto terms = vectorizer_.tokenize(content);
        std::vector<float> embedding(EMBEDDING_DIM, 0.0f);

        if (terms.empty()) {
            return embedding;
        }

        size_t iter = 0;
        for (const auto &term: terms) {
            if ((++iter % 128 == 0) && stop_flag_ && !stop_flag_->load()) {
                return {};
            }
            size_t hash = std::hash<std::string>{}(term);
            size_t dim = hash % EMBEDDING_DIM;

            double tf = static_cast<double>(std::count(terms.begin(), terms.end(), term)) / terms.size();
            double idf = 0.0;

            int df = vectorizer_.get_document_frequency(term);
            if (df > 0 && vectorizer_.get_num_documents() > 0) {
                idf = std::log(static_cast<double>(vectorizer_.get_num_documents()) / (1.0 + df));
            }

            embedding[dim] += static_cast<float>(tf * idf);
        }

        float norm = 0.0f;
        for (float val: embedding) {
            norm += val * val;
        }
        norm = std::sqrt(norm);

        if (norm > 0.0f) {
            for (float &val: embedding) {
                val /= norm;
            }
        }

        return embedding;
    }

    void set_search_config(const HybridSearchConfig &config) {
        search_config_ = config;
    }

    void set_embedding_config(const EmbeddingConfig &config) {
        embedding_config_ = config;
        apply_active_embedding_model();
        initialize_embedding_backend();
    }

    void set_embedding_model_registry(const EmbeddingModelRegistry &registry) {
        embedding_registry_ = registry;
        apply_active_embedding_model();
        initialize_embedding_backend();
    }

    bool switch_active_embedding_model(const std::string& model_id, bool force_reembed = true) {
        auto it = embedding_registry_.models.find(model_id);
        if (it == embedding_registry_.models.end()) {
            return false;
        }
        embedding_config_.active_model_id = model_id;
        apply_embedding_model_definition(embedding_config_, it->second);
        initialize_embedding_backend();
        if (force_reembed) {
            force_reembed_next_index_ = true;
        }
        last_vector_store_status_ = "embedding model switched; reindex required";
        return true;
    }

    void force_reembed_on_next_index() {
        force_reembed_next_index_ = true;
    }

    const EmbeddingModelRegistry& get_embedding_model_registry() const {
        return embedding_registry_;
    }

    const EmbeddingConfig& get_embedding_config() const {
        return embedding_config_;
    }

    std::string get_embedding_backend() const {
        if (embedding_config_.backend == "onnx" && onnx_ready_) {
            return "onnx";
        }
        return "tfidf";
    }

    size_t get_embedding_dim() const {
        return embedding_dim_;
    }

    std::string get_embedding_model_id() const {
        return embedding_model_.id.empty()
            ? build_embedding_model_id(embedding_config_, get_embedding_backend(), embedding_dim_)
            : embedding_model_.id;
    }

    EmbeddingModelInfo get_embedding_model_info() const {
        return embedding_model_;
    }

    std::string get_embedding_cache_namespace() const {
        return get_embedding_model_id();
    }

    std::string get_vector_store_backend() const {
        if (vector_store_) {
            return vector_store_->backendName();
        }
        return search_config_.use_hybrid ? vector_store_config_.backend : "none";
    }

    std::string get_vector_store_status() const {
        return last_vector_store_status_;
    }

    VectorStoreDiagnostics get_vector_store_diagnostics() const {
        if (!search_config_.use_hybrid) {
            return VectorStoreDiagnostics{"none", "disabled", "hybrid search is disabled", false, 0, 0};
        }
        if (vector_store_) {
            return vector_store_->diagnostics();
        }
        auto store = create_vector_store();
        if (!store) {
            return VectorStoreDiagnostics{
                vector_store_config_.backend,
                "backend_unavailable",
                "unknown vector store backend",
                false,
                0,
                0
            };
        }
        auto diagnostics = store->diagnostics();
        if (diagnostics.detail.empty() && !last_vector_store_status_.empty()) {
            diagnostics.detail = last_vector_store_status_;
        }
        return diagnostics;
    }

    bool is_query_expansion_enabled() const {
        return search_config_.use_query_expansion;
    }

    bool is_reranking_enabled() const {
        return search_config_.use_reranking;
    }

    bool is_multi_query_retrieval_enabled() const {
        return search_config_.use_multi_query_retrieval;
    }

    bool is_embedding_reranker_enabled() const {
        return search_config_.use_embedding_reranker;
    }

    bool is_xapian_enabled() const {
        return search_config_.xapian_enabled;
    }

    XapianSearchDiagnostics get_xapian_diagnostics() const {
        return xapian_diagnostics_;
    }

    size_t multi_query_max_variants() const {
        return search_config_.multi_query_max_variants;
    }

    std::string expand_query(const std::string& query) const {
        if (!search_config_.use_query_expansion) {
            return query;
        }
        auto terms = expand_query_terms(query);
        if (terms.empty()) {
            return query;
        }

        std::ostringstream expanded;
        expanded << query;
        for (const auto& term : terms) {
            if (query.find(term) == std::string::npos) {
                expanded << ' ' << term;
            }
        }
        return expanded.str();
    }

    bool save_vector_index(const std::string& path) const {
        if (!vector_store_ || !vector_store_->isReady() || !vector_store_->save(path)) {
            return false;
        }
        return write_vector_index_metadata(collect_vector_records());
    }

    bool load_vector_index(const std::string& path) {
        const auto records = collect_vector_records();
        std::string metadata_reason;
        if (!vector_index_metadata_matches(records, metadata_reason)) {
            last_vector_store_status_ = "load skipped: " + metadata_reason;
            return false;
        }
        auto store = create_vector_store();
        if (!store->load(path, embedding_dim_, documents_.size())) {
            last_vector_store_status_ = "load failed: " + path;
            return false;
        }
        vector_store_ = std::move(store);
        is_hybrid_indexed_ = true;
        last_vector_store_status_ = "loaded: " + path;
        return true;
    }

    bool is_onnx_ready() const {
        return onnx_ready_;
    }

    std::string get_onnx_status_message() const {
        return onnx_status_message_;
    }

    std::vector<float> generate_embedding(const std::string &query) {
        if (embedding_config_.backend == "onnx" && onnx_ready_) {
            auto onnx_embedding = generate_onnx_embedding(query);
            if (!onnx_embedding.empty()) {
                return onnx_embedding;
            }
            if (!embedding_config_.enable_fallback) {
                return onnx_embedding;
            }
        }
        return generate_tfidf_embedding(query);
    }

    void index_project(const std::string &project_root) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto index_start = std::chrono::steady_clock::now();
        indexed_project_root_ = project_root;
        auto reusable_embeddings = (force_reembed_next_index_ || !embedding_config_.persistent_cache_enabled)
            ? std::unordered_map<std::string, std::vector<float>>{}
            : collect_reusable_embeddings();
        reset_incremental_stats(reusable_embeddings.size());
        force_reembed_next_index_ = false;

        documents_.clear();
        path_to_index_.clear();
        vectorizer_ = TfidfVectorizer();

        std::cout << "🔍 Сканирование проекта: " << project_root << std::endl;

        qornix::rag::IngestionPipeline::Config ingestion_config;
        ingestion_config.root_path = project_root;
        ingestion_config.max_file_size_kb = max_file_size_bytes_ / 1024;
        qornix::rag::IngestionPipeline pipeline(std::move(ingestion_config));
        auto ingestion = pipeline.ingestRoot();
        last_ingestion_result_ = ingestion;

        if (!ingestion.issues.empty()) {
            size_t reported = 0;
            for (const auto& issue : ingestion.issues) {
                if (reported++ >= 5) {
                    break;
                }
                if (issue.severity == qornix::rag::IngestionIssueSeverity::ERROR) {
                    std::cerr << "⚠️  Ingestion " << issue.code << ": " << issue.path
                              << " (" << issue.message << ")" << std::endl;
                }
            }
        }

        for (const auto& ingested : ingestion.documents) {
            if (stop_flag_ && !stop_flag_->load()) {
                std::cout << "🛑 Индексация прервана сигналом остановки" << std::endl;
                is_indexed_ = false;
                auto index_end = std::chrono::steady_clock::now();
                last_index_duration_ms_ = static_cast<size_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
                );
                return;
            }

            Document doc;
            doc.path = ingested.path;
            doc.relative_path = ingested.relative_path;
            doc.content = ingested.content;
            doc.type = ingested.document_type;
            doc.language = ingested.language;
            doc.size_bytes = ingested.size_bytes;
            doc.lines_count = ingested.lines_count;
            doc.hash = ingested.hash;
            doc.last_modified = ingested.last_modified;
            doc.metadata = ingested.metadata;
            doc.metadata["embedding_model_id"] = get_embedding_model_id();
            doc.metadata["embedding_backend"] = get_embedding_backend();
            doc.metadata["embedding_model_signature"] = embedding_model_.model_signature;
            doc.metadata["embedding_token_limit"] = std::to_string(embedding_content_token_limit());
            doc.metadata["tokenizer_max_tokens"] = std::to_string(embedding_content_token_limit());
            doc.metadata["tokenizer_type"] = embedding_config_.tokenizer_type;

            DocumentChunker::Config chunker_config;
            chunker_config.tokenizer_max_tokens = embedding_content_token_limit();
            DocumentChunker chunker(chunker_config);
            auto chunks = chunker.chunkDocument(doc);
            if (chunks.empty()) {
                chunks.push_back(doc);
            }

            for (auto& chunk : chunks) {
                chunk.metadata["embedding_estimated_tokens"] = std::to_string(estimate_embedding_token_count(chunk.content));
                chunk.metadata["embedding_model_id"] = get_embedding_model_id();
                chunk.metadata["embedding_backend"] = get_embedding_backend();
                chunk.metadata["embedding_model_signature"] = embedding_model_.model_signature;
                assign_incremental_embedding(chunk, reusable_embeddings);

                if (stop_flag_ && !stop_flag_->load()) {
                    std::cout << "🛑 Индексация прервана сигналом остановки" << std::endl;
                    is_indexed_ = false;
                    auto index_end = std::chrono::steady_clock::now();
                    last_index_duration_ms_ = static_cast<size_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
                    );
                    return;
                }

                documents_.push_back(std::move(chunk));
                path_to_index_[documents_.back().relative_path] = documents_.size() - 1;
                vectorizer_.index_document(documents_.back().relative_path, documents_.back().content);
            }
        }

        is_indexed_ = true;

        auto stats = get_statistics();
        std::cout << "✅ Проиндексировано файлов: " << stats.total_files << std::endl;
        std::cout << "   Просмотрено файлов: " << ingestion.files_seen
                  << ", пропущено: " << ingestion.skipped
                  << ", дубликатов: " << ingestion.duplicates_found
                  << ", ошибок: " << ingestion.errors << std::endl;
        std::cout << "   Chunks: " << stats.indexed_chunks
                  << ", reused embeddings: " << stats.reused_embeddings
                  << ", generated embeddings: " << stats.generated_embeddings
                  << ", stale embeddings: " << stats.stale_embeddings << std::endl;
        std::cout << "   Всего строк: " << stats.total_lines << std::endl;
        std::cout << "   Размер: " << (stats.total_size_bytes / 1024) << " KB" << std::endl;

        build_hybrid_index();
        auto index_end = std::chrono::steady_clock::now();
        last_index_duration_ms_ = static_cast<size_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
        );
        std::cout << "   Время индексации: " << last_index_duration_ms_ << " ms" << std::endl;
    }

private:
    std::unique_ptr<VectorStore> create_vector_store() const {
        if (!search_config_.use_hybrid) {
            return nullptr;
        }
        VectorStoreOptions options;
        options.backend = vector_store_config_.backend;
        options.index_path = vector_store_config_.index_path;
        options.endpoint = vector_store_config_.endpoint;
        options.api_key = vector_store_config_.api_key;
        options.collection = vector_store_config_.collection;
        options.connection_string = vector_store_config_.connection_string;
        options.table = vector_store_config_.table;
        options.distance = vector_store_config_.distance;
        options.upsert_batch_size = vector_store_config_.upsert_batch_size;
        options.recreate = vector_store_config_.recreate;
        return createVectorStore(options);
    }

    std::vector<VectorRecord> collect_vector_records() const {
        std::vector<VectorRecord> records;
        records.reserve(documents_.size());
        for (size_t i = 0; i < documents_.size(); ++i) {
            const auto& doc = documents_[i];
            if (!doc.embedding.empty() && doc.embedding.size() == embedding_dim_) {
                records.push_back(VectorRecord{i, doc.embedding});
            }
        }
        return records;
    }

    std::string reusable_embedding_key(const Document& document) const {
        return embedding_model_.model_signature + "|" + get_embedding_model_id() + "|" + document.relative_path + "|" + document.hash;
    }

    std::unordered_map<std::string, std::vector<float>> collect_reusable_embeddings() const {
        std::unordered_map<std::string, std::vector<float>> reusable;
        reusable.reserve(documents_.size());
        for (const auto& document : documents_) {
            if (!document.embedding.empty() && document.embedding.size() == embedding_dim_) {
                reusable[reusable_embedding_key(document)] = document.embedding;
            }
        }
        return reusable;
    }

    void reset_incremental_stats(size_t previous_embedding_count) {
        last_reused_embeddings_ = 0;
        last_generated_embeddings_ = 0;
        last_stale_embeddings_ = previous_embedding_count;
    }

    void assign_incremental_embedding(
        Document& chunk,
        const std::unordered_map<std::string, std::vector<float>>& reusable_embeddings) {
        auto reusable = reusable_embeddings.find(reusable_embedding_key(chunk));
        if (reusable != reusable_embeddings.end() && reusable->second.size() == embedding_dim_) {
            chunk.embedding = reusable->second;
            ++last_reused_embeddings_;
            if (last_stale_embeddings_ > 0) {
                --last_stale_embeddings_;
            }
            return;
        }

        chunk.embedding = generate_embedding(chunk.content);
        ++last_generated_embeddings_;
    }

    std::string vector_index_metadata_path() const {
        if (!vector_store_config_.metadata_path.empty()) {
            return vector_store_config_.metadata_path;
        }
        if (!vector_store_config_.index_path.empty()) {
            return vector_store_config_.index_path + ".meta.json";
        }
        return "";
    }

    std::string compute_vector_snapshot_hash(const std::vector<VectorRecord>& records) const {
        std::stringstream ss;
        ss << get_embedding_model_id() << "|" << embedding_dim_ << "|" << records.size() << "\n";
        for (const auto& record : records) {
            if (record.label >= documents_.size()) {
                continue;
            }
            const auto& doc = documents_[record.label];
            ss << record.label << "|"
               << doc.relative_path << "|"
               << doc.hash << "|"
               << doc.content.size() << "|";
            auto chunk_index = doc.metadata.find("chunk_index");
            if (chunk_index != doc.metadata.end()) {
                ss << chunk_index->second;
            }
            ss << "\n";
        }
        return HashCalculator::compute_md5(ss.str());
    }

    bool write_vector_index_metadata(const std::vector<VectorRecord>& records) const {
        const auto metadata_path = vector_index_metadata_path();
        if (metadata_path.empty()) {
            return false;
        }

        try {
            boost::json::object metadata;
            metadata["backend"] = vector_store_config_.backend;
            metadata["index_path"] = vector_store_config_.index_path;
            metadata["embedding_model_id"] = get_embedding_model_id();
            metadata["embedding_backend"] = get_embedding_backend();
            metadata["embedding_dimension"] = static_cast<std::int64_t>(embedding_dim_);
            metadata["vector_count"] = static_cast<std::int64_t>(records.size());
            metadata["snapshot_hash"] = compute_vector_snapshot_hash(records);
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            metadata["build_time"] = std::string(std::ctime(&now));

            const std::filesystem::path path(metadata_path);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::ofstream out(metadata_path);
            out << boost::json::serialize(metadata) << "\n";
            return out.good();
        } catch (const std::exception& e) {
            std::cerr << "Error writing vector index metadata: " << e.what() << std::endl;
            return false;
        }
    }

    bool vector_index_metadata_matches(const std::vector<VectorRecord>& records, std::string& reason) const {
        const auto metadata_path = vector_index_metadata_path();
        if (metadata_path.empty()) {
            reason = "metadata path is not configured";
            return false;
        }
        if (!std::filesystem::exists(metadata_path)) {
            reason = "metadata file is missing";
            return false;
        }

        try {
            std::ifstream in(metadata_path);
            std::stringstream buffer;
            buffer << in.rdbuf();
            auto parsed = boost::json::parse(buffer.str());
            if (!parsed.is_object()) {
                reason = "metadata root is not an object";
                return false;
            }

            const auto& object = parsed.as_object();
            auto get_string = [&object](const char* key) -> std::optional<std::string> {
                auto it = object.find(key);
                if (it == object.end() || !it->value().is_string()) {
                    return std::nullopt;
                }
                return std::string(it->value().as_string().c_str());
            };
            auto get_int = [&object](const char* key) -> std::optional<std::int64_t> {
                auto it = object.find(key);
                if (it == object.end() || !it->value().is_int64()) {
                    return std::nullopt;
                }
                return it->value().as_int64();
            };

            const auto backend = get_string("backend");
            const auto model_id = get_string("embedding_model_id");
            const auto dimension = get_int("embedding_dimension");
            const auto vector_count = get_int("vector_count");
            const auto snapshot_hash = get_string("snapshot_hash");

            if (!backend || *backend != vector_store_config_.backend) {
                reason = "backend mismatch";
                return false;
            }
            if (!model_id || *model_id != get_embedding_model_id()) {
                reason = "embedding model mismatch";
                return false;
            }
            if (!dimension || static_cast<size_t>(*dimension) != embedding_dim_) {
                reason = "embedding dimension mismatch";
                return false;
            }
            if (!vector_count || static_cast<size_t>(*vector_count) != records.size()) {
                reason = "vector count mismatch";
                return false;
            }
            if (!snapshot_hash || *snapshot_hash != compute_vector_snapshot_hash(records)) {
                reason = "snapshot hash mismatch";
                return false;
            }

            return true;
        } catch (const std::exception& e) {
            reason = std::string("metadata parse failed: ") + e.what();
            return false;
        }
    }

    static std::string trim(const std::string &value) {
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
            ++start;
        }
        size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
            --end;
        }
        return value.substr(start, end - start);
    }

    static std::string lower_copy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static std::string source_path_for_document(const Document& doc) {
        const auto chunk_of = doc.metadata.find("chunk_of");
        return chunk_of == doc.metadata.end() || chunk_of->second.empty()
            ? doc.relative_path
            : chunk_of->second;
    }

    static bool parse_double_value(const std::string& value, double& output) {
        if (value.empty()) {
            return false;
        }
        char* end = nullptr;
        output = std::strtod(value.c_str(), &end);
        return end != value.c_str();
    }

    static std::string metadata_value_for_filter(const Document& doc, const std::string& raw_key) {
        const std::string key = lower_copy(raw_key);
        if (key == "type" || key == "document_type") {
            return doc.type;
        }
        if (key == "language") {
            return doc.language;
        }
        if (key == "path") {
            return doc.relative_path;
        }
        if (key == "source_path" || key == "source") {
            return source_path_for_document(doc);
        }
        if (key == "page") {
            auto it = doc.metadata.find("chunk_page");
            if (it != doc.metadata.end()) return it->second;
            it = doc.metadata.find("chunk_page_start");
            if (it != doc.metadata.end()) return it->second;
            it = doc.metadata.find("pdf_page_start");
            if (it != doc.metadata.end()) return it->second;
        }
        if (key == "sheet") {
            auto it = doc.metadata.find("chunk_sheet");
            if (it != doc.metadata.end()) return it->second;
        }
        if (key == "slide") {
            auto it = doc.metadata.find("chunk_slide");
            if (it != doc.metadata.end()) return it->second;
        }
        if (key == "row") {
            auto it = doc.metadata.find("chunk_row_start");
            if (it != doc.metadata.end()) return it->second;
        }
        if (key == "symbol" || key == "symbol_name") {
            auto it = doc.metadata.find("chunk_symbol_name");
            if (it != doc.metadata.end()) return it->second;
            it = doc.metadata.find("chunk_symbol");
            if (it != doc.metadata.end()) return it->second;
        }
        if (key == "ocr_confidence") {
            auto it = doc.metadata.find("chunk_ocr_confidence");
            if (it != doc.metadata.end()) return it->second;
            it = doc.metadata.find("image_ocr_confidence_avg");
            if (it != doc.metadata.end()) return it->second;
        }
        auto it = doc.metadata.find(raw_key);
        if (it != doc.metadata.end()) {
            return it->second;
        }
        for (const auto& [meta_key, meta_value] : doc.metadata) {
            if (lower_copy(meta_key) == key) {
                return meta_value;
            }
        }
        return {};
    }

    static bool page_filter_matches(const Document& doc, const std::string& expected) {
        double page = 0.0;
        if (!parse_double_value(expected, page)) {
            return false;
        }
        double start = 0.0;
        double end = 0.0;
        const std::string start_value = metadata_value_for_filter(doc, "chunk_page_start");
        const std::string end_value = metadata_value_for_filter(doc, "chunk_page_end");
        if (!parse_double_value(start_value, start)) {
            return false;
        }
        if (!parse_double_value(end_value, end)) {
            end = start;
        }
        return page >= start && page <= end;
    }

    static bool row_filter_matches(const Document& doc, const std::string& expected) {
        double row = 0.0;
        if (!parse_double_value(expected, row)) {
            return false;
        }
        double start = 0.0;
        double end = 0.0;
        if (!parse_double_value(metadata_value_for_filter(doc, "chunk_row_start"), start)) {
            return false;
        }
        if (!parse_double_value(metadata_value_for_filter(doc, "chunk_row_end"), end)) {
            end = start;
        }
        return row >= start && row <= end;
    }

    static bool metadata_filter_matches(const Document& doc, const MetadataFilter& filter) {
        if (filter.key.empty()) {
            return true;
        }
        const std::string key = lower_copy(filter.key);
        const std::string op = filter.op.empty() ? "equals" : lower_copy(filter.op);
        if ((key == "page" || key == "chunk_page") && op == "equals") {
            return page_filter_matches(doc, filter.value);
        }
        if ((key == "row" || key == "chunk_row") && op == "equals") {
            return row_filter_matches(doc, filter.value);
        }

        const std::string actual = metadata_value_for_filter(doc, filter.key);
        if (op == "exists") {
            return !actual.empty();
        }
        if (op == "not_empty") {
            return !trim(actual).empty();
        }
        if (actual.empty() && !filter.value.empty()) {
            return false;
        }

        const std::string actual_lower = lower_copy(actual);
        const std::string expected_lower = lower_copy(filter.value);
        if (op == "contains") {
            return actual_lower.find(expected_lower) != std::string::npos;
        }
        if (op == "prefix") {
            return actual_lower.rfind(expected_lower, 0) == 0;
        }
        if (op == "gte" || op == ">=") {
            double lhs = 0.0;
            double rhs = 0.0;
            return parse_double_value(actual, lhs) && parse_double_value(filter.value, rhs) && lhs >= rhs;
        }
        if (op == "lte" || op == "<=") {
            double lhs = 0.0;
            double rhs = 0.0;
            return parse_double_value(actual, lhs) && parse_double_value(filter.value, rhs) && lhs <= rhs;
        }
        return actual_lower == expected_lower;
    }

    static bool document_matches_filters(const Document& doc, const std::vector<MetadataFilter>& filters) {
        for (const auto& filter : filters) {
            if (!metadata_filter_matches(doc, filter)) {
                return false;
            }
        }
        return true;
    }

    static std::string metadata_term_token(std::string value) {
        value = lower_copy(value);
        for (char& ch : value) {
            if (!std::isalnum(static_cast<unsigned char>(ch))) {
                ch = '_';
            }
        }
        while (!value.empty() && value.front() == '_') value.erase(value.begin());
        while (!value.empty() && value.back() == '_') value.pop_back();
        if (value.size() > 180) {
            value.resize(180);
        }
        return value;
    }

    static std::vector<std::string> split_identifier_terms(const std::string& value) {
        std::vector<std::string> terms;
        std::string current;

        auto flush = [&]() {
            if (current.size() >= 2) {
                terms.push_back(lower_copy(current));
            }
            current.clear();
        };

        for (size_t i = 0; i < value.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(value[i]);
            if (!std::isalnum(ch)) {
                flush();
                continue;
            }

            if (!current.empty() && std::isupper(ch)) {
                const unsigned char prev = static_cast<unsigned char>(value[i - 1]);
                const bool prev_lower_or_digit = std::islower(prev) || std::isdigit(prev);
                const bool next_lower = i + 1 < value.size() &&
                    std::islower(static_cast<unsigned char>(value[i + 1]));
                if (prev_lower_or_digit || next_lower) {
                    flush();
                }
            }
            current.push_back(static_cast<char>(ch));
        }
        flush();
        return terms;
    }

    std::vector<std::string> expand_query_terms(const std::string& query) const {
        std::vector<std::string> expanded;
        std::set<std::string> seen;

        auto add_term = [&](const std::string& raw) {
            auto term = trim(lower_copy(raw));
            if (term.size() < 2 || seen.count(term) > 0) {
                return;
            }
            seen.insert(term);
            expanded.push_back(std::move(term));
        };

        for (const auto& term : vectorizer_.tokenize(query)) {
            add_term(term);
            if (term.size() > 3 && term.back() == 's') {
                add_term(term.substr(0, term.size() - 1));
            }
        }

        for (const auto& term : split_identifier_terms(query)) {
            add_term(term);
        }

        std::istringstream stream(query);
        std::string raw;
        while (stream >> raw) {
            for (const auto& term : split_identifier_terms(raw)) {
                add_term(term);
            }
            std::filesystem::path path(raw);
            add_term(path.stem().string());
            add_term(path.extension().string());
        }

        return expanded;
    }

    double rerank_boost_for_document(const Document& doc,
                                     const std::string& original_query,
                                     const std::vector<std::string>& query_terms) const {
        double boost = 0.0;
        const auto path = lower_copy(doc.relative_path);
        const auto content = lower_copy(doc.content);
        const auto phrase = lower_copy(original_query);

        for (const auto& term : query_terms) {
            if (term.empty()) {
                continue;
            }
            if (path.find(term) != std::string::npos) {
                boost += search_config_.rerank_path_boost;
            }
            for (const auto& [key, value] : doc.metadata) {
                const auto metadata_text = lower_copy(key + " " + value);
                if (metadata_text.find(term) != std::string::npos) {
                    boost += search_config_.rerank_metadata_boost;
                    break;
                }
            }
        }

        if (!phrase.empty() && content.find(phrase) != std::string::npos) {
            boost += search_config_.rerank_exact_content_boost;
        }

        return boost;
    }

    void rerank_results(std::vector<SearchResult>& results,
                        const std::string& original_query,
                        const std::vector<std::string>& query_terms) const {
        if (!search_config_.use_reranking || results.empty()) {
            return;
        }

        for (auto& result : results) {
            const double base = result.fused_score > 0.0 ? result.fused_score : result.score;
            const double boosted = base + rerank_boost_for_document(result.document, original_query, query_terms);
            result.score = boosted;
            result.fused_score = boosted;
        }

        std::stable_sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
            return a.score > b.score;
        });
    }

    static std::string path_stem_or_value(const std::string &path) {
        if (path.empty()) {
            return "default";
        }
        try {
            auto stem = std::filesystem::path(path).stem().string();
            return stem.empty() ? path : stem;
        } catch (...) {
            return path;
        }
    }

    static std::string build_embedding_model_signature(const EmbeddingConfig &config,
                                                       const std::string &effective_backend,
                                                       size_t effective_dimension) {
        std::ostringstream ss;
        ss << effective_backend << "|"
           << config.model_id << "|"
           << config.model_path << "|"
           << config.tokenizer_path << "|"
           << config.tokenizer_type << "|"
           << config.pooling << "|"
           << config.model_version << "|"
           << config.max_seq_len << "|"
           << config.onnx_threads << "|"
           << effective_dimension << "|"
           << (config.normalize_embeddings ? "norm" : "raw") << "|"
           << (config.lowercase_tokens ? "lower" : "case") << "|"
           << config.chunk_token_margin;
        return HashCalculator::compute_md5(ss.str());
    }

    static std::string build_embedding_model_id(const EmbeddingConfig &config,
                                                const std::string &effective_backend,
                                                size_t effective_dimension) {
        if (!config.model_id.empty() && effective_backend == config.backend) {
            return config.model_id;
        }

        std::string base = effective_backend;
        if (effective_backend == "onnx") {
            base += ":" + path_stem_or_value(config.model_path);
            if (!config.model_version.empty()) {
                base += ":" + config.model_version;
            }
            base += ":seq" + std::to_string(config.max_seq_len);
            base += ":" + config.pooling;
            if (effective_dimension > 0) {
                base += ":d" + std::to_string(effective_dimension);
            }
            return base + ":" + HashCalculator::compute_md5(
                config.model_path + "|" + config.tokenizer_path + "|" +
                config.tokenizer_type + "|" + config.pooling + "|" +
                config.model_version + "|" + std::to_string(config.max_seq_len) + "|" +
                std::to_string(effective_dimension) + "|" +
                (config.normalize_embeddings ? "norm" : "raw") + "|" +
                (config.lowercase_tokens ? "lower" : "case") + "|" +
                std::to_string(config.chunk_token_margin)
            );
        }

        return "tfidf:d" + std::to_string(effective_dimension > 0 ? effective_dimension : EMBEDDING_DIM);
    }

    void apply_active_embedding_model() {
        if (embedding_config_.active_model_id.empty()) {
            return;
        }

        auto it = embedding_registry_.models.find(embedding_config_.active_model_id);
        if (it != embedding_registry_.models.end()) {
            apply_embedding_model_definition(embedding_config_, it->second);
        }
    }

    void refresh_embedding_model_info(bool ready, const std::string &status) {
        embedding_model_.backend = get_embedding_backend();
        embedding_model_.id = build_embedding_model_id(embedding_config_, embedding_model_.backend, embedding_dim_);
        embedding_model_.name = embedding_config_.model_name.empty()
            ? path_stem_or_value(embedding_config_.model_path)
            : embedding_config_.model_name;
        embedding_model_.version = embedding_config_.model_version;
        embedding_model_.model_path = embedding_config_.model_path;
        embedding_model_.tokenizer_path = embedding_config_.tokenizer_path;
        embedding_model_.tokenizer_type = embedding_config_.tokenizer_type;
        embedding_model_.pooling = embedding_config_.pooling;
        embedding_model_.dimension = embedding_dim_;
        embedding_model_.max_seq_len = embedding_config_.max_seq_len;
        embedding_model_.tokenizer_vocab_size = tokenizer_vocab_size_;
        embedding_model_.effective_chunk_token_limit = embedding_content_token_limit();
        embedding_model_.ready = ready;
        embedding_model_.persistent_cache_enabled = embedding_config_.persistent_cache_enabled;
        embedding_model_.status = status;
        embedding_model_.tokenizer_status = tokenizer_status_message_;
        embedding_model_.model_signature = build_embedding_model_signature(embedding_config_, embedding_model_.backend, embedding_dim_);
        embedding_model_.active_model_id = embedding_config_.active_model_id;
        embedding_model_.registry_size = embedding_registry_.models.size();
        embedding_model_.registry_model_ids.clear();
        for (const auto& [id, model] : embedding_registry_.models) {
            (void)model;
            embedding_model_.registry_model_ids.push_back(id);
        }
        embedding_model_.registry_warnings = embedding_registry_.warnings;
    }

    static std::string lower_ascii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static std::string json_string_or_empty(const boost::json::object& object, const char* key) {
        auto it = object.find(key);
        if (it == object.end() || !it->value().is_string()) {
            return "";
        }
        return std::string(it->value().as_string().c_str());
    }

    void reset_tokenizer_state() {
        tokenizer_vocab_.clear();
        tokenizer_model_type_ = "basic_wordpiece";
        tokenizer_status_message_ = "not loaded";
        tokenizer_vocab_size_ = 0;
        unk_token_id_ = 100;
        cls_token_id_ = 101;
        sep_token_id_ = 102;
        pad_token_id_ = 0;
    }

    void add_tokenizer_entry(const std::string& token, int64_t id) {
        if (token.empty() || id < 0) {
            return;
        }
        tokenizer_vocab_[token] = id;
    }

    void add_vocab_from_json_value(const boost::json::value& value) {
        if (value.is_object()) {
            const auto& vocab = value.as_object();
            tokenizer_vocab_.reserve(tokenizer_vocab_.size() + vocab.size());
            for (const auto& entry : vocab) {
                if (entry.value().is_int64()) {
                    add_tokenizer_entry(std::string(entry.key()), entry.value().as_int64());
                }
            }
            return;
        }
        if (value.is_array()) {
            const auto& vocab = value.as_array();
            tokenizer_vocab_.reserve(tokenizer_vocab_.size() + vocab.size());
            for (size_t i = 0; i < vocab.size(); ++i) {
                if (vocab[i].is_string()) {
                    add_tokenizer_entry(std::string(vocab[i].as_string().c_str()), static_cast<int64_t>(i));
                    continue;
                }
                if (!vocab[i].is_array()) {
                    continue;
                }
                const auto& item = vocab[i].as_array();
                if (item.empty() || !item[0].is_string()) {
                    continue;
                }
                // Hugging Face Unigram uses [[token, score], ...]. BPE/WordPiece fixtures can
                // also appear as array vocabularies; in both cases the index is the token id.
                add_tokenizer_entry(std::string(item[0].as_string().c_str()), static_cast<int64_t>(i));
            }
        }
    }

    void add_added_tokens(const boost::json::object& root) {
        auto it = root.find("added_tokens");
        if (it == root.end() || !it->value().is_array()) {
            return;
        }
        for (const auto& value : it->value().as_array()) {
            if (!value.is_object()) {
                continue;
            }
            const auto& token = value.as_object();
            const auto content = json_string_or_empty(token, "content");
            auto id_it = token.find("id");
            if (!content.empty() && id_it != token.end() && id_it->value().is_int64()) {
                add_tokenizer_entry(content, id_it->value().as_int64());
            }
        }
    }

    void apply_tokenizer_special_ids(const boost::json::object& model) {
        auto update_by_name = [&](const std::string& token, int64_t& target) {
            if (token.empty()) {
                return;
            }
            auto it = tokenizer_vocab_.find(token);
            if (it != tokenizer_vocab_.end()) {
                target = it->second;
            }
        };

        update_by_name(json_string_or_empty(model, "unk_token"), unk_token_id_);
        update_by_name(json_string_or_empty(model, "cls_token"), cls_token_id_);
        update_by_name(json_string_or_empty(model, "sep_token"), sep_token_id_);
        update_by_name(json_string_or_empty(model, "pad_token"), pad_token_id_);

        update_by_name("[UNK]", unk_token_id_);
        update_by_name("[CLS]", cls_token_id_);
        update_by_name("[SEP]", sep_token_id_);
        update_by_name("[PAD]", pad_token_id_);
        update_by_name("<unk>", unk_token_id_);
        update_by_name("<s>", cls_token_id_);
        update_by_name("</s>", sep_token_id_);
        update_by_name("<pad>", pad_token_id_);
        update_by_name("<|endoftext|>", sep_token_id_);
    }

    bool parse_tokenizer_vocab(const std::string &tokenizer_path) {
        reset_tokenizer_state();

        std::ifstream file(tokenizer_path);
        if (!file.is_open()) {
            onnx_status_message_ = "tokenizer.json not found: " + tokenizer_path;
            tokenizer_status_message_ = onnx_status_message_;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        try {
            auto parsed = boost::json::parse(buffer.str());
            if (!parsed.is_object()) {
                onnx_status_message_ = "tokenizer.json root is not object";
                tokenizer_status_message_ = onnx_status_message_;
                return false;
            }

            const auto &root = parsed.as_object();
            auto model_it = root.find("model");
            if (model_it == root.end() || !model_it->value().is_object()) {
                onnx_status_message_ = "tokenizer.json does not contain object model";
                tokenizer_status_message_ = onnx_status_message_;
                return false;
            }

            const auto &model = model_it->value().as_object();
            const std::string model_type = json_string_or_empty(model, "type");
            if (!model_type.empty()) {
                embedding_config_.tokenizer_type = model_type;
                tokenizer_model_type_ = model_type;
            } else {
                tokenizer_model_type_ = embedding_config_.tokenizer_type.empty()
                    ? "basic_wordpiece"
                    : embedding_config_.tokenizer_type;
            }

            auto vocab_it = model.find("vocab");
            if (vocab_it != model.end()) {
                add_vocab_from_json_value(vocab_it->value());
            }
            // Some minimized tokenizer.json fixtures and hand-written registries put vocab at root.
            auto root_vocab = root.find("vocab");
            if (root_vocab != root.end()) {
                add_vocab_from_json_value(root_vocab->value());
            }
            add_added_tokens(root);

            if (tokenizer_vocab_.empty()) {
                onnx_status_message_ = "tokenizer.json has no supported vocab entries for model type " + tokenizer_model_type_;
                tokenizer_status_message_ = onnx_status_message_;
                return false;
            }

            apply_tokenizer_special_ids(model);

            if (root.contains("truncation") && root.at("truncation").is_object()) {
                auto &truncation = root.at("truncation").as_object();
                if (truncation.contains("max_length") && truncation.at("max_length").is_int64()) {
                    const auto max_length = truncation.at("max_length").as_int64();
                    if (max_length > 0) {
                        embedding_config_.max_seq_len = std::min<size_t>(
                            embedding_config_.max_seq_len == 0 ? static_cast<size_t>(max_length) : embedding_config_.max_seq_len,
                            static_cast<size_t>(max_length)
                        );
                    }
                }
            }

            tokenizer_vocab_size_ = tokenizer_vocab_.size();
            tokenizer_status_message_ = "tokenizer loaded: type=" + tokenizer_model_type_ +
                ", vocab=" + std::to_string(tokenizer_vocab_size_) +
                ", max_seq_len=" + std::to_string(embedding_config_.max_seq_len);
            return true;
        } catch (const std::exception &e) {
            onnx_status_message_ = std::string("tokenizer parse failed: ") + e.what();
            tokenizer_status_message_ = onnx_status_message_;
            return false;
        }
    }

    std::vector<std::string> basic_tokenize_for_onnx(const std::string &text) const {
        std::vector<std::string> tokens;
        std::string current;
        current.reserve(32);

        auto flush = [&]() {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        };

        for (char raw_ch: text) {
            unsigned char ch = static_cast<unsigned char>(raw_ch);
            if (std::isalnum(ch) || ch == '_') {
                current.push_back(embedding_config_.lowercase_tokens
                    ? static_cast<char>(std::tolower(ch))
                    : static_cast<char>(ch));
            } else {
                flush();
                if (!std::isspace(ch)) {
                    tokens.emplace_back(1, static_cast<char>(ch));
                }
            }
        }
        flush();
        return tokens;
    }

    std::optional<int64_t> lookup_token_id(const std::string& token, bool at_word_start) const {
        if (token.empty()) {
            return std::nullopt;
        }
        const std::vector<std::string> candidates = at_word_start
            ? std::vector<std::string>{token, "▁" + token, "Ġ" + token, lower_ascii(token), "▁" + lower_ascii(token), "Ġ" + lower_ascii(token)}
            : std::vector<std::string>{token, "##" + token, lower_ascii(token), "##" + lower_ascii(token)};
        for (const auto& candidate : candidates) {
            auto it = tokenizer_vocab_.find(candidate);
            if (it != tokenizer_vocab_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> wordpiece_tokens_for(const std::string& token) const {
        if (token.empty()) {
            return {};
        }
        if (tokenizer_vocab_.find(token) != tokenizer_vocab_.end()) {
            return {token};
        }

        std::vector<std::string> pieces;
        size_t start = 0;
        while (start < token.size()) {
            size_t end = token.size();
            std::string best;
            size_t best_end = start;
            while (end > start) {
                std::string candidate = token.substr(start, end - start);
                if (start > 0) {
                    candidate = "##" + candidate;
                }
                if (tokenizer_vocab_.find(candidate) != tokenizer_vocab_.end()) {
                    best = candidate;
                    best_end = end;
                    break;
                }
                --end;
            }
            if (best.empty()) {
                return {token};
            }
            pieces.push_back(best);
            start = best_end;
        }
        return pieces;
    }

    bool active_tokenizer_is_wordpiece() const {
        const std::string type = lower_ascii(embedding_config_.tokenizer_type.empty()
            ? tokenizer_model_type_
            : embedding_config_.tokenizer_type);
        return type == "wordpiece" || type == "bertwordpiece" || type == "basic_wordpiece";
    }

    std::vector<int64_t> content_token_ids_for_onnx(const std::string& text, size_t limit_without_specials) const {
        std::vector<int64_t> ids;
        ids.reserve(limit_without_specials == 0 ? 32 : limit_without_specials);
        const bool use_wordpiece = active_tokenizer_is_wordpiece();
        for (const auto &token: basic_tokenize_for_onnx(text)) {
            if (use_wordpiece) {
                for (const auto& piece : wordpiece_tokens_for(token)) {
                    if (ids.size() >= limit_without_specials) {
                        return ids;
                    }
                    auto lookup = lookup_token_id(piece, ids.empty());
                    ids.push_back(lookup.value_or(unk_token_id_));
                }
            } else {
                if (ids.size() >= limit_without_specials) {
                    return ids;
                }
                auto lookup = lookup_token_id(token, true);
                ids.push_back(lookup.value_or(unk_token_id_));
            }
        }
        return ids;
    }

    size_t embedding_content_token_limit() const {
        const size_t seq_len = embedding_config_.max_seq_len == 0 ? 256 : embedding_config_.max_seq_len;
        const size_t margin = std::min(embedding_config_.chunk_token_margin, seq_len > 1 ? seq_len - 1 : 0);
        return std::max<size_t>(1, seq_len > margin ? seq_len - margin : 1);
    }

    size_t estimate_embedding_token_count(const std::string& text) const {
        if (embedding_config_.backend == "onnx" && !tokenizer_vocab_.empty()) {
            return content_token_ids_for_onnx(text, embedding_content_token_limit()).size() + embedding_config_.chunk_token_margin;
        }
        return vectorizer_.tokenize(text).size();
    }

    std::pair<std::vector<int64_t>, std::vector<int64_t>> encode_for_onnx(const std::string &text) const {
        std::vector<int64_t> input_ids;
        std::vector<int64_t> attention_mask;
        input_ids.reserve(embedding_config_.max_seq_len);
        attention_mask.reserve(embedding_config_.max_seq_len);

        input_ids.push_back(cls_token_id_);
        attention_mask.push_back(1);

        const size_t content_limit = embedding_config_.max_seq_len > 1 ? embedding_config_.max_seq_len - 1 : 0;
        for (const auto id : content_token_ids_for_onnx(text, content_limit)) {
            if (input_ids.size() + 1 >= embedding_config_.max_seq_len) {
                break;
            }
            input_ids.push_back(id);
            attention_mask.push_back(1);
        }

        if (input_ids.size() < embedding_config_.max_seq_len) {
            input_ids.push_back(sep_token_id_);
            attention_mask.push_back(1);
        }

        while (input_ids.size() < embedding_config_.max_seq_len) {
            input_ids.push_back(pad_token_id_);
            attention_mask.push_back(0);
        }

        return {input_ids, attention_mask};
    }

    void normalize_l2(std::vector<float> &vec) const {
        if (!embedding_config_.normalize_embeddings || vec.empty()) {
            return;
        }
        float norm = 0.0f;
        for (float v: vec) {
            norm += v * v;
        }
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (float &v: vec) {
                v /= norm;
            }
        }
    }

    bool accept_embedding_dimension(size_t actual_dimension) {
        if (embedding_config_.dimension == 0 || actual_dimension == embedding_config_.dimension) {
            embedding_dim_ = actual_dimension;
            refresh_embedding_model_info(onnx_ready_, onnx_status_message_);
            return true;
        }
        onnx_status_message_ = "onnx output dimension mismatch: configured " +
            std::to_string(embedding_config_.dimension) + ", got " + std::to_string(actual_dimension);
        onnx_ready_ = false;
        if (embedding_config_.enable_fallback) {
            embedding_dim_ = EMBEDDING_DIM;
        }
        refresh_embedding_model_info(false, onnx_status_message_);
        return false;
    }

    void initialize_embedding_backend() {
        onnx_ready_ = false;
        reset_tokenizer_state();
        if (embedding_config_.max_seq_len == 0) {
            embedding_config_.max_seq_len = 256;
        }
        embedding_dim_ = EMBEDDING_DIM;
        onnx_status_message_ = "tfidf backend active";
        tokenizer_status_message_ = "tfidf backend does not use an external tokenizer";

        if (embedding_config_.backend != "onnx") {
            refresh_embedding_model_info(true, onnx_status_message_);
            return;
        }
        embedding_dim_ = embedding_config_.dimension > 0 ? embedding_config_.dimension : EMBEDDING_DIM;

#ifndef QORNIX_HAS_ONNX
        onnx_status_message_ = "onnx backend requested but binary was built without ONNX Runtime";
        if (embedding_config_.enable_fallback) {
            embedding_dim_ = EMBEDDING_DIM;
        }
        refresh_embedding_model_info(false, onnx_status_message_);
        return;
#else
        if (!parse_tokenizer_vocab(embedding_config_.tokenizer_path)) {
            if (embedding_config_.enable_fallback) {
                embedding_dim_ = EMBEDDING_DIM;
            }
            refresh_embedding_model_info(false, onnx_status_message_);
            if (!embedding_config_.enable_fallback) {
                throw std::runtime_error("Failed to load tokenizer for ONNX mode: " + onnx_status_message_);
            }
            return;
        }

        try {
            ort_env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "qornix_rag");
            ort_session_options_ = std::make_unique<Ort::SessionOptions>();
            ort_session_options_->SetIntraOpNumThreads(static_cast<int>(embedding_config_.onnx_threads));
            ort_session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            ort_session_ = std::make_unique<Ort::Session>(
                *ort_env_,
                embedding_config_.model_path.c_str(),
                *ort_session_options_
            );

            Ort::AllocatorWithDefaultOptions allocator;
            ort_input_names_storage_.clear();
            ort_output_names_storage_.clear();
            ort_input_names_.clear();
            ort_output_names_.clear();

            const size_t input_count = ort_session_->GetInputCount();
            for (size_t i = 0; i < input_count; ++i) {
                auto name = ort_session_->GetInputNameAllocated(i, allocator);
                ort_input_names_storage_.push_back(name.get());
            }
            for (const auto &name: ort_input_names_storage_) {
                ort_input_names_.push_back(name.c_str());
            }

            const size_t output_count = ort_session_->GetOutputCount();
            for (size_t i = 0; i < output_count; ++i) {
                auto name = ort_session_->GetOutputNameAllocated(i, allocator);
                ort_output_names_storage_.push_back(name.get());
            }
            for (const auto &name: ort_output_names_storage_) {
                ort_output_names_.push_back(name.c_str());
            }

            onnx_ready_ = true;
            onnx_status_message_ = "onnx model loaded: " + embedding_config_.model_path;
            refresh_embedding_model_info(true, onnx_status_message_);
        } catch (const std::exception &e) {
            onnx_status_message_ = std::string("onnx init failed: ") + e.what();
            if (embedding_config_.enable_fallback) {
                embedding_dim_ = EMBEDDING_DIM;
            }
            refresh_embedding_model_info(false, onnx_status_message_);
            if (!embedding_config_.enable_fallback) {
                throw;
            }
        }
#endif
    }

    std::vector<float> generate_onnx_embedding(const std::string &text) {
#ifndef QORNIX_HAS_ONNX
        (void)text;
        return {};
#else
        if (!onnx_ready_ || !ort_session_) {
            return {};
        }

        auto encoded = encode_for_onnx(text);
        std::vector<int64_t> token_type_ids(embedding_config_.max_seq_len, 0);
        std::vector<int64_t> dims = {1, static_cast<int64_t>(embedding_config_.max_seq_len)};

        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_ids = Ort::Value::CreateTensor<int64_t>(
            mem_info, encoded.first.data(), encoded.first.size(), dims.data(), dims.size()
        );
        Ort::Value attention_mask = Ort::Value::CreateTensor<int64_t>(
            mem_info, encoded.second.data(), encoded.second.size(), dims.data(), dims.size()
        );
        Ort::Value token_types = Ort::Value::CreateTensor<int64_t>(
            mem_info, token_type_ids.data(), token_type_ids.size(), dims.data(), dims.size()
        );

        std::vector<Ort::Value> inputs;
        inputs.emplace_back(std::move(input_ids));
        inputs.emplace_back(std::move(attention_mask));
        if (ort_input_names_.size() >= 3) {
            inputs.emplace_back(std::move(token_types));
        }

        auto outputs = ort_session_->Run(
            Ort::RunOptions{nullptr},
            ort_input_names_.data(),
            inputs.data(),
            inputs.size(),
            ort_output_names_.data(),
            1
        );

        if (outputs.empty() || !outputs[0].IsTensor()) {
            return {};
        }

        auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        float *raw = outputs[0].GetTensorMutableData<float>();

        if (shape.size() == 3) {
            const int64_t seq_len = shape[1];
            const int64_t hidden_dim = shape[2];
            if (seq_len <= 0 || hidden_dim <= 0) {
                return {};
            }
            std::vector<float> embedding(static_cast<size_t>(hidden_dim), 0.0f);

            if (embedding_config_.pooling == "cls") {
                for (int64_t h = 0; h < hidden_dim; ++h) {
                    embedding[static_cast<size_t>(h)] = raw[h];
                }
            } else {
                float token_count = 0.0f;
                for (int64_t t = 0; t < seq_len; ++t) {
                    if (t >= static_cast<int64_t>(encoded.second.size()) || encoded.second[static_cast<size_t>(t)] == 0) {
                        continue;
                    }
                    token_count += 1.0f;
                    for (int64_t h = 0; h < hidden_dim; ++h) {
                        embedding[static_cast<size_t>(h)] += raw[t * hidden_dim + h];
                    }
                }

                if (token_count > 0.0f) {
                    for (float &v: embedding) {
                        v /= token_count;
                    }
                }
            }
            normalize_l2(embedding);
            if (!accept_embedding_dimension(embedding.size())) {
                return {};
            }
            return embedding;
        }

        if (shape.size() == 2) {
            const int64_t hidden_dim = shape[1];
            if (hidden_dim <= 0) {
                return {};
            }
            std::vector<float> embedding(raw, raw + hidden_dim);
            normalize_l2(embedding);
            if (!accept_embedding_dimension(embedding.size())) {
                return {};
            }
            return embedding;
        }

        return {};
#endif
    }

public:

    void build_hybrid_index() {
        if (documents_.empty()) {
            throw std::runtime_error("No documents to index. Call index_project() first.");
        }

        std::cout << "🔨 Построение гибридного индекса..." << std::endl;
        bool vector_ready = false;
        bool xapian_ready = false;
        const auto vector_records = collect_vector_records();

        try {
            vector_store_.reset();
            last_vector_store_status_ = "not built";

            if (vector_store_config_.auto_load && !vector_store_config_.index_path.empty()) {
                auto loaded_store = create_vector_store();
                std::string metadata_reason;
                if (!vector_index_metadata_matches(vector_records, metadata_reason)) {
                    last_vector_store_status_ = "load skipped: " + metadata_reason;
                    std::cout << "⚠️  Persisted vector index ignored: " << metadata_reason << std::endl;
                } else if (loaded_store && loaded_store->load(vector_store_config_.index_path,
                                                              embedding_dim_,
                                                              std::max(vector_records.size(), documents_.size()))) {
                    if (loaded_store->size() == vector_records.size()) {
                        vector_store_ = std::move(loaded_store);
                        vector_ready = true;
                        last_vector_store_status_ = "loaded: " + vector_store_config_.index_path;
                        std::cout << "✅ Vector index loaded from " << vector_store_config_.index_path
                                  << " (" << vector_records.size() << " vectors)" << std::endl;
                    } else {
                        last_vector_store_status_ = "load rejected: vector count mismatch";
                        std::cout << "⚠️  Persisted vector index ignored: vector count mismatch" << std::endl;
                    }
                }
            }

            if (!vector_ready) {
                auto store = create_vector_store();
                if (store && store->build(vector_records, embedding_dim_)) {
                    vector_store_ = std::move(store);
                    vector_ready = true;
                    last_vector_store_status_ = "built";

                    if (vector_store_config_.auto_save && !vector_store_config_.index_path.empty()) {
                        if (vector_store_->save(vector_store_config_.index_path)) {
                            write_vector_index_metadata(vector_records);
                            last_vector_store_status_ = "built and saved: " + vector_store_config_.index_path;
                            std::cout << "✅ Vector index saved to " << vector_store_config_.index_path << std::endl;
                        } else {
                            last_vector_store_status_ = "built; save failed: " + vector_store_config_.index_path;
                        }
                    }
                } else if (store) {
                    last_vector_store_status_ = store->lastError().empty()
                        ? "build failed"
                        : "build failed: " + store->lastError();
                } else {
                    last_vector_store_status_ = "backend unavailable: " + vector_store_config_.backend;
                }
            }

            if (vector_ready) {
                std::cout << "✅ Vector index ready: " << get_vector_store_backend()
                          << " (" << vector_records.size() << " vectors)" << std::endl;
            }
        } catch (const std::exception& e) {
            last_vector_store_status_ = std::string("error: ") + e.what();
            std::cerr << "❌ Error building vector index: " << e.what() << std::endl;
        }


        xapian_diagnostics_ = XapianSearchDiagnostics{};
        xapian_diagnostics_.enabled = search_config_.xapian_enabled;
        xapian_diagnostics_.language = search_config_.xapian_language;
        xapian_diagnostics_.stemming = search_config_.xapian_stemming;
        xapian_diagnostics_.stemming_strategy = search_config_.xapian_stemming_strategy;
        xapian_diagnostics_.cjk_ngrams = search_config_.xapian_cjk_ngrams;
        xapian_diagnostics_.word_breaks = search_config_.xapian_word_breaks;
        xapian_diagnostics_.spelling = search_config_.xapian_spelling;
        xapian_diagnostics_.metadata_prefixes = search_config_.xapian_metadata_prefixes;

        if (!search_config_.xapian_enabled) {
            xapian_db_.reset();
            xapian_diagnostics_.status = "disabled";
            xapian_diagnostics_.detail = "Xapian lexical index disabled by search.xapian_enabled=false";
            std::cout << "ℹ️  Xapian индекс отключён настройкой search.xapian_enabled=false" << std::endl;
        } else {
            try {
                // Release previous handle before opening new DB to avoid self-lock.
                xapian_db_.reset();
                xapian_db_ = std::make_unique<Xapian::WritableDatabase>("xapian_index", Xapian::DB_CREATE_OR_OVERWRITE);

                std::string first_effective_language;
                for (size_t i = 0; i < documents_.size(); ++i) {
                    const auto &doc = documents_[i];
                    const auto effective_language = qornix_rag_xapian_detail::detectLanguage(
                        search_config_.xapian_language,
                        doc.content,
                        doc.language);
                    if (first_effective_language.empty()) {
                        first_effective_language = effective_language;
                    }

                    Xapian::Document xdoc;
                    xdoc.set_data(std::to_string(i));

                    Xapian::TermGenerator termgen;
                    try {
                        qornix_rag_xapian_detail::configureTermGenerator(termgen, search_config_, effective_language);
                    } catch (const Xapian::Error& e) {
                        std::cerr << "⚠️  Xapian stemmer unavailable for language '" << effective_language
                                  << "': " << e.get_description() << ". Indexing without stemming." << std::endl;
                    }
                    termgen.set_document(xdoc);

                    termgen.index_text(doc.relative_path, 2, "QPATH");
                    termgen.index_text(source_path_for_document(doc), 2, "QSOURCE");
                    termgen.index_text(doc.type, 2, "QTYPE");
                    termgen.index_text(effective_language, 1, "QLANG");
                    termgen.index_text(doc.content);

                    qornix_rag_xapian_detail::addExactTerm(xdoc, "QPATH", doc.relative_path, 2);
                    qornix_rag_xapian_detail::addExactTerm(xdoc, "QSOURCE", source_path_for_document(doc), 2);
                    qornix_rag_xapian_detail::addExactTerm(xdoc, "QTYPE", doc.type, 2);
                    qornix_rag_xapian_detail::addExactTerm(xdoc, "QLANG", effective_language, 1);
                    if (search_config_.xapian_word_breaks) {
                        qornix_rag_xapian_detail::addWordBreakTerms(xdoc, doc.relative_path, "QB");
                        qornix_rag_xapian_detail::addWordBreakTerms(xdoc, source_path_for_document(doc), "QB");
                    }

                    if (search_config_.xapian_metadata_prefixes) {
                        for (const auto& [meta_key, meta_value] : doc.metadata) {
                            if (meta_key.empty() || meta_value.empty()) {
                                continue;
                            }
                            termgen.index_text(meta_key + " " + meta_value, 1, "QMETA");
                            qornix_rag_xapian_detail::addExactTerm(xdoc, "QMETA", meta_key, 1);
                            qornix_rag_xapian_detail::addExactTerm(xdoc, "QMETA", meta_key + ":" + meta_value, 1);
                            if (search_config_.xapian_word_breaks) {
                                qornix_rag_xapian_detail::addWordBreakTerms(xdoc, meta_key, "QB");
                                qornix_rag_xapian_detail::addWordBreakTerms(xdoc, meta_value, "QB");
                            }

                            // Keep the legacy exact metadata terms too, so older metadata-aware
                            // queries and tests remain compatible.
                            const auto key_token = metadata_term_token(meta_key);
                            const auto value_token = metadata_term_token(meta_value);
                            if (!key_token.empty()) {
                                xdoc.add_term("XMETAK_" + key_token);
                            }
                            if (!key_token.empty() && !value_token.empty()) {
                                xdoc.add_term("XMETAV_" + key_token + "_" + value_token);
                            }
                        }

                        const auto symbol = doc.metadata.find("chunk_symbol_name");
                        if (symbol != doc.metadata.end()) {
                            termgen.index_text(symbol->second, 4, "QSYMBOL");
                            qornix_rag_xapian_detail::addExactTerm(xdoc, "QSYMBOL", symbol->second, 3);
                        }
                        const auto page = doc.metadata.find("chunk_page");
                        if (page != doc.metadata.end()) {
                            qornix_rag_xapian_detail::addExactTerm(xdoc, "QPAGE", page->second, 2);
                        }
                        const auto sheet = doc.metadata.find("chunk_sheet");
                        if (sheet != doc.metadata.end()) {
                            termgen.index_text(sheet->second, 3, "QSHEET");
                            qornix_rag_xapian_detail::addExactTerm(xdoc, "QSHEET", sheet->second, 2);
                        }
                        const auto slide = doc.metadata.find("chunk_slide");
                        if (slide != doc.metadata.end()) {
                            qornix_rag_xapian_detail::addExactTerm(xdoc, "QSLIDE", slide->second, 2);
                        }
                    }

                    if (search_config_.xapian_cjk_ngrams) {
                        qornix_rag_xapian_detail::addCjkTerms(xdoc, doc.content, "QCJK");
                    }

                    xapian_db_->add_document(xdoc);
                }

                xapian_db_->commit();
                xapian_diagnostics_.ready = true;
                xapian_diagnostics_.status = "ready";
                xapian_diagnostics_.detail = "Xapian lexical index built with language-aware settings";
                xapian_diagnostics_.effective_index_language = first_effective_language.empty() ? "none" : first_effective_language;
                xapian_diagnostics_.documents_indexed = documents_.size();
                std::cout << "✅ Xapian индекс построен (" << documents_.size() << " документов)"
                          << " [language=" << xapian_diagnostics_.language
                          << ", stemming=" << (xapian_diagnostics_.stemming ? "on" : "off")
                          << ", strategy=" << xapian_diagnostics_.stemming_strategy << "]" << std::endl;
                xapian_ready = true;
            } catch (const Xapian::Error &e) {
                xapian_db_.reset();
                xapian_diagnostics_.ready = false;
                xapian_diagnostics_.status = "error";
                xapian_diagnostics_.detail = e.get_description();
                std::cerr << "❌ Error building Xapian index: " << e.get_description() << std::endl;
            }
        }

        is_hybrid_indexed_ = vector_ready;
        if (vector_ready && xapian_ready) {
            std::cout << "✅ Гибридный индекс построен: HNSW + Xapian" << std::endl;
        } else if (vector_ready) {
            std::cout << "⚠️  Гибридный индекс частично готов: только HNSW" << std::endl;
        } else {
            std::cout << "❌ Гибридный индекс не построен" << std::endl;
        }
    }

    size_t get_document_id_from_data(const std::string &data) const {
        try {
            return std::stoul(data);
        } catch (...) {
            return 0;
        }
    }

    std::vector<SearchResult> hybrid_search(
        const std::string &query,
        const std::vector<float> &query_embedding,
        size_t top_k = 10
    ) {
        SearchOptions options;
        options.top_k = top_k;
        return hybrid_search(query, query_embedding, options);
    }

    std::vector<SearchResult> hybrid_search(
        const std::string &query,
        const std::vector<float> &query_embedding,
        const SearchOptions &options
    ) {
        if (!is_hybrid_indexed_) {
            std::cout << "⚠️  Hybrid index not ready, falling back to BM25" << std::endl;
            return bm25_search(query, options);
        }

        const std::string expanded_query = expand_query(query);
        const size_t requested_top_k = std::max<size_t>(1, options.top_k);
        const size_t filter_multiplier = options.filters.empty() ? 1 : 4;
        const size_t first_stage_top_k = requested_top_k * std::max<size_t>(1, search_config_.rerank_input_multiplier) * filter_multiplier;

        std::unordered_map<size_t, double> hnsw_scores;
        std::unordered_map<size_t, double> xapian_scores;

        try {
            if (vector_store_ && vector_store_->isReady() && !query_embedding.empty()) {
                auto hits = vector_store_->search(query_embedding, first_stage_top_k * 2);
                for (const auto& hit : hits) {
                    if (hit.score > 0.0) {
                        hnsw_scores[hit.label] = hit.score;
                    }
                }

                std::cout << "📊 HNSW нашел: " << hnsw_scores.size() << " результатов" << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "⚠️  HNSW search error: " << e.what() << std::endl;
        }

        try {
            if (search_config_.xapian_enabled && xapian_db_) {
                Xapian::Enquire enquire(*xapian_db_);

                Xapian::QueryParser parser;
                parser.set_database(*xapian_db_);
                parser.set_default_op(Xapian::Query::op::OP_OR);
                qornix_rag_xapian_detail::configurePrefixes(parser);

                const auto query_language = qornix_rag_xapian_detail::detectLanguage(
                    search_config_.xapian_language,
                    expanded_query);
                xapian_diagnostics_.effective_query_language = query_language;
                try {
                    qornix_rag_xapian_detail::configureQueryParser(parser, search_config_, query_language);
                } catch (const Xapian::Error& e) {
                    std::cerr << "⚠️  Xapian query stemmer unavailable for language '" << query_language
                              << "': " << e.get_description() << ". Searching without stemming." << std::endl;
                }

                unsigned flags = Xapian::QueryParser::FLAG_DEFAULT;
                if (search_config_.xapian_spelling) {
                    flags |= Xapian::QueryParser::FLAG_SPELLING_CORRECTION;
                }

                Xapian::Query query_obj;
                try {
                    query_obj = parser.parse_query(expanded_query, flags);
                } catch (const Xapian::Error&) {
                    // Code/config queries often contain punctuation that the query parser treats
                    // as syntax. Keep retrieval robust by falling back to exact token OR search.
                    std::vector<std::string> query_terms = vectorizer_.tokenize(expanded_query);
                    for (const auto& term : query_terms) {
                        query_obj = qornix_rag_xapian_detail::orWithTerm(query_obj, term);
                        query_obj = qornix_rag_xapian_detail::orWithTerm(query_obj, "QPATH" + qornix_rag_xapian_detail::safeTermValue(term));
                        query_obj = qornix_rag_xapian_detail::orWithTerm(query_obj, "QMETA" + qornix_rag_xapian_detail::safeTermValue(term));
                    }
                }

                if (search_config_.xapian_word_breaks) {
                    for (const auto& token : qornix_rag_xapian_detail::splitWordBreakTokens(expanded_query)) {
                        query_obj = qornix_rag_xapian_detail::orWithTerm(query_obj, "QB" + qornix_rag_xapian_detail::safeTermValue(token));
                    }
                }

                if (search_config_.xapian_cjk_ngrams) {
                    for (const auto& gram : qornix_rag_xapian_detail::extractCjkNgrams(expanded_query)) {
                        query_obj = qornix_rag_xapian_detail::orWithTerm(query_obj, "QCJK" + gram);
                    }
                }

                if (!query_obj.empty()) {
                    enquire.set_query(query_obj);
                    auto matches = enquire.get_mset(0, first_stage_top_k * 2);

                    for (auto it = matches.begin(); it != matches.end(); ++it) {
                        std::string data = it.get_document().get_data();
                        size_t doc_id = get_document_id_from_data(data);
                        double score = static_cast<double>(it.get_percent()) / 100.0;
                        xapian_scores[doc_id] = score;
                    }

                    std::cout << "📊 Xapian нашел: " << matches.size() << " результатов" << std::endl;
                } else {
                    std::cout << "📊 Xapian пропущен: пустой запрос после парсинга" << std::endl;
                }
            }
        } catch (const Xapian::Error& e) {
            std::cerr << "⚠️  Xapian search error: " << e.get_description() << std::endl;
        }

        auto normalize_scores = [](std::unordered_map<size_t, double> &scores) {
            if (scores.empty()) return;

            double max_score = 0.0;
            for (const auto &[id, score]: scores) {
                max_score = std::max(max_score, score);
            }

            if (max_score > 0.0) {
                for (auto &[id, score]: scores) {
                    score /= max_score;
                }
            }
        };

        normalize_scores(hnsw_scores);
        normalize_scores(xapian_scores);

        std::unordered_map<size_t, double> fused_scores;

        for (const auto &[id, score]: hnsw_scores) {
            fused_scores[id] += search_config_.vector_weight * score;
        }

        for (const auto &[id, score]: xapian_scores) {
            fused_scores[id] += search_config_.text_weight * score;
        }

        std::vector<std::pair<size_t, double>> sorted_results(
            fused_scores.begin(), fused_scores.end()
        );
        std::sort(sorted_results.begin(), sorted_results.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });

        std::vector<SearchResult> final_results;
        auto query_terms = vectorizer_.tokenize(expanded_query);

        for (const auto &[doc_idx, score]: sorted_results) {
            if (final_results.size() >= first_stage_top_k) break;
            if (score < search_config_.min_score_threshold) continue;

            if (doc_idx < documents_.size()) {
                if (!document_matches_filters(documents_[doc_idx], options.filters)) {
                    continue;
                }
                SearchResult result;
                result.document = documents_[doc_idx];
                result.score = score;
                auto it_vec = hnsw_scores.find(doc_idx);
                if (it_vec != hnsw_scores.end()) {
                    result.vector_score = it_vec->second;
                }
                auto it_text = xapian_scores.find(doc_idx);
                if (it_text != xapian_scores.end()) {
                    result.text_score = it_text->second;
                }
                result.fused_score = score;
                result.snippet = extract_snippet(documents_[doc_idx], query_terms);
                result.match_locations = find_match_locations(documents_[doc_idx], query_terms);
                final_results.push_back(result);
            }
        }

        rerank_results(final_results, query, query_terms);
        if (final_results.size() > requested_top_k) {
            final_results.resize(requested_top_k);
        }

        std::cout << "✅ Гибридный поиск вернул: " << final_results.size() << " результатов" << std::endl;
        return final_results;
    }

    std::vector<SearchResult> bm25_search(const std::string &query, const SearchOptions& options) {
        if (!is_indexed_) {
            throw std::runtime_error("Project not indexed. Call index_project() first.");
        }

        const std::string expanded_query = expand_query(query);
        std::vector<std::string> query_terms = vectorizer_.tokenize(expanded_query);
        std::vector<SearchResult> results;

        for (const auto &doc: documents_) {
            if (!document_matches_filters(doc, options.filters)) {
                continue;
            }

            double score = vectorizer_.compute_bm25_score(doc.content, query_terms);

            std::string filename = std::filesystem::path(doc.relative_path).filename().string();
            for (const auto &term: query_terms) {
                if (filename.find(term) != std::string::npos) {
                    score *= 2.0;
                }
            }

            if (doc.relative_path.find("include") != std::string::npos ||
                doc.relative_path.find("src") != std::string::npos) {
                score *= 1.2;
            }

            // Keep metadata-only queries discoverable even when the content score is low.
            if (score <= 0.1 && !options.filters.empty()) {
                score = 0.12;
            }

            if (score > 0.1) {
                SearchResult result;
                result.document = doc;
                result.score = score;
                result.vector_score = 0.0;
                result.text_score = score;
                result.fused_score = score;
                result.snippet = extract_snippet(doc, query_terms);
                result.match_locations = find_match_locations(doc, query_terms);
                results.push_back(result);
            }
        }

        std::sort(results.begin(), results.end(),
                  [](const SearchResult &a, const SearchResult &b) {
                      return a.score > b.score;
                  });

        rerank_results(results, query, query_terms);

        if (results.size() > options.top_k) {
            results.resize(options.top_k);
        }

        return results;
    }

    std::vector<SearchResult> search(const std::string &query, const SearchOptions& options) {
        if (!is_indexed_) {
            throw std::runtime_error("Project not indexed. Call index_project() first.");
        }

        if (search_config_.use_hybrid && is_hybrid_indexed_) {
            auto query_embedding = generate_embedding(query);
            return hybrid_search(query, query_embedding, options);
        }

        return bm25_search(query, options);
    }

    std::vector<SearchResult> search(const std::string &query, size_t top_k = 10) {
        SearchOptions options;
        options.top_k = top_k;
        return search(query, options);
    }

    std::string build_context(const std::string &query, size_t max_length = 8000) {
        auto query_embedding = generate_embedding(query);

        auto results = hybrid_search(query, query_embedding, 15);

        std::stringstream context;
        context << "Ты эксперт по C++ разработке, помогаешь работать с кодовой базой проекта.\n\n";
        context << "=== КОНТЕКСТ ПРОЕКТА ===\n";
        context << "Запрос: " << query << "\n\n";

        size_t current_length = 0;
        size_t files_added = 0;

        for (const auto &result: results) {
            std::string header = "\n" + std::string(60, '=') + "\n";
            const auto chunk_of = result.document.metadata.find("chunk_of");
            const std::string source_path = chunk_of != result.document.metadata.end()
                ? chunk_of->second
                : result.document.relative_path;
            header += "ФАЙЛ: " + source_path + "\n";
            const auto chunk_index = result.document.metadata.find("chunk_index");
            if (chunk_index != result.document.metadata.end()) {
                header += "Фрагмент: " + chunk_index->second + "\n";
            }
            auto add_locator = [&](const std::string& label, const std::string& key) {
                const auto it = result.document.metadata.find(key);
                if (it != result.document.metadata.end() && !it->second.empty()) {
                    header += label + ": " + it->second + "\n";
                }
            };
            add_locator("Страница", "chunk_page");
            add_locator("Слайд", "chunk_slide");
            add_locator("Лист", "chunk_sheet");
            add_locator("Заголовок", "chunk_heading");
            add_locator("Символ", "chunk_symbol_name");
            add_locator("OCR-регион", "chunk_ocr_region");
            header += "Тип: " + result.document.type + ", Язык: " + result.document.language + "\n";
            std::ostringstream score_stream;
            score_stream << std::fixed << std::setprecision(2) << result.score;
            header += "Релевантность: " + score_stream.str() + "\n";
            header += std::string(60, '=') + "\n\n";

            if (current_length + header.length() + result.document.content.length() > max_length) {
                break;
            }

            context << header;

            if (result.document.content.length() < 2000) {
                context << result.document.content;
            } else {
                context << result.snippet;
                context << "\n...\n";
            }

            context << "\n";
            current_length += header.length() + result.document.content.length();
            files_added++;
        }

        context << "\n" + std::string(60, '=') + "\n";
        context << "Всего файлов в контексте: " << files_added << "\n";
        context << "Размер контекста: " << current_length << " символов\n";
        context << std::string(60, '=') + "\n";

        return context.str();
    }

    void set_stop_flag(const std::atomic<bool>* stop_flag) {
        stop_flag_ = stop_flag;
    }

    ProjectStats get_statistics() {
        ProjectStats stats;
        std::set<std::string> unique_files;
        stats.total_lines = 0;
        stats.total_size_bytes = 0;
        stats.index_duration_ms = last_index_duration_ms_;

        for (const auto &doc: documents_) {
            const auto chunk_of = doc.metadata.find("chunk_of");
            const std::string source_path = chunk_of != doc.metadata.end()
                ? chunk_of->second
                : doc.relative_path;
            if (unique_files.insert(source_path).second) {
                stats.total_lines += doc.lines_count;
                stats.total_size_bytes += doc.size_bytes;
            }
            stats.files_by_type[doc.type]++;

            std::string dir = std::filesystem::path(source_path).parent_path().string();
            if (dir.empty()) dir = ".";
            stats.files_by_directory[dir]++;
        }
        stats.total_files = unique_files.size();
        stats.indexed_chunks = documents_.size();
        stats.reused_embeddings = last_reused_embeddings_;
        stats.generated_embeddings = last_generated_embeddings_;
        stats.stale_embeddings = last_stale_embeddings_;

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        stats.last_indexed = std::ctime(&time);

        return stats;
    }

    bool is_indexed() const {
        return is_indexed_;
    }

    std::string get_indexed_project_root() const {
        return indexed_project_root_;
    }

    const Document *get_document(const std::string &relative_path) const {
        auto it = path_to_index_.find(relative_path);
        if (it != path_to_index_.end()) {
            return &documents_[it->second];
        }
        return nullptr;
    }

    std::vector<Document> get_documents_snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return documents_;
    }

    qornix::rag::IngestionJobResult get_last_ingestion_result() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_ingestion_result_;
    }

    // ============================================
    // Phase 4: Data Source Management
    // ============================================

    /**
     * Add a data source to the engine.
     * Documents from this source will be indexed when indexSources() is called.
     *
     * Usage:
     *   auto qa_source = std::make_shared<QASource>(qa_config);
     *   engine.addDataSource(qa_source);
     */
    void addDataSource(std::shared_ptr<DataSource> source) {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        if (source) {
            data_sources_.push_back(source);
        }
    }

    /**
     * Remove a data source by ID.
     */
    void removeDataSource(const std::string& source_id) {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        data_sources_.erase(
            std::remove_if(data_sources_.begin(), data_sources_.end(),
                [&source_id](const std::shared_ptr<DataSource>& src) {
                    return src->getId() == source_id;
                }),
            data_sources_.end()
        );
    }

    /**
     * Get all registered data sources.
     */
    std::vector<std::shared_ptr<DataSource>> getDataSources() const {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        return data_sources_;
    }

    /**
     * Get total document count across all data sources (before indexing).
     */
    size_t getTotalDataSourceCount() const;

    /**
     * Index all registered data sources.
     * This is the Phase 4 replacement for index_project().
     *
     * Usage:
     *   engine.addDataSource(std::make_shared<QASource>(qa_config));
     *   engine.addDataSource(std::make_shared<FileSource>(file_config));
     *   engine.indexSources();  // Index all sources
     */
    void indexSources() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto index_start = std::chrono::steady_clock::now();
        auto reusable_embeddings = (force_reembed_next_index_ || !embedding_config_.persistent_cache_enabled)
            ? std::unordered_map<std::string, std::vector<float>>{}
            : collect_reusable_embeddings();
        reset_incremental_stats(reusable_embeddings.size());
        force_reembed_next_index_ = false;

        documents_.clear();
        path_to_index_.clear();
        vectorizer_ = TfidfVectorizer(); // Reset TF-IDF

        std::lock_guard<std::mutex> src_lock(sources_mutex_);

        if (data_sources_.empty()) {
            std::cerr << "Warning: No data sources registered. Call addDataSource() first." << std::endl;
            is_indexed_ = false;
            return;
        }

        size_t total_docs = 0;

        for (const auto& source : data_sources_) {
            if (!source->initialize()) {
                std::cerr << "Warning: Failed to initialize source " << source->getId() << std::endl;
                continue;
            }

            auto docs = source->getDocuments();
            std::cout << "📄 Loaded " << docs.size() << " documents from source: " << source->getId() << std::endl;

            DocumentChunker chunker;
            for (auto& doc : docs) {
                if (stop_flag_ && !stop_flag_->load()) {
                    std::cout << "🛑 Indexing interrupted by stop signal" << std::endl;
                    is_indexed_ = false;
                    auto index_end = std::chrono::steady_clock::now();
                    last_index_duration_ms_ = static_cast<size_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
                    );
                    return;
                }

                auto chunks = chunker.chunkDocument(doc);
                if (chunks.empty()) {
                    chunks.push_back(doc);
                }

                for (auto& chunk : chunks) {
                    assign_incremental_embedding(chunk, reusable_embeddings);

                    if (stop_flag_ && !stop_flag_->load()) {
                        std::cout << "🛑 Indexing interrupted by stop signal" << std::endl;
                        is_indexed_ = false;
                        auto index_end = std::chrono::steady_clock::now();
                        last_index_duration_ms_ = static_cast<size_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
                        );
                        return;
                    }

                    documents_.push_back(std::move(chunk));
                    path_to_index_[documents_.back().relative_path] = documents_.size() - 1;
                    vectorizer_.index_document(documents_.back().relative_path, documents_.back().content);
                }

                total_docs++;
            }

            source->cleanup();
        }

        // Build hybrid index
        build_hybrid_index();

        is_indexed_ = true;

        auto stats = get_statistics();
        std::cout << "✅ Indexed " << total_docs << " documents from " << data_sources_.size() << " sources" << std::endl;
        std::cout << "   Total lines: " << stats.total_lines << std::endl;
        std::cout << "   Chunks: " << stats.indexed_chunks
                  << ", reused embeddings: " << stats.reused_embeddings
                  << ", generated embeddings: " << stats.generated_embeddings
                  << ", stale embeddings: " << stats.stale_embeddings << std::endl;
        std::cout << "   Index duration: " << last_index_duration_ms_ << " ms" << std::endl;
    }

    /**
     * Get list of registered source IDs.
     */
    std::vector<std::string> getSourceIds() const {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        std::vector<std::string> ids;
        ids.reserve(data_sources_.size());
        for (const auto& source : data_sources_) {
            ids.push_back(source->getId());
        }
        return ids;
    }

    /**
     * Get list of registered source types.
     */
    std::vector<DataSourceType> getSourceTypes() const;
};
