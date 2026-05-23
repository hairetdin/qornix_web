/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "core.h"
#include "document_chunker.h"
#include <fstream>
#include <algorithm>
#include <regex>
#include <sstream>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <functional>

// ============================================================================
// HashCalculator
// ============================================================================

std::string HashCalculator::compute_md5(const std::string &data) {
    unsigned long hash = 5381;
    for (char c: data) {
        hash = ((hash << 5) + hash) + c;
    }
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

// ============================================================================
// TfidfVectorizer
// ============================================================================

int TfidfVectorizer::get_document_frequency(const std::string& term) const {
    auto it = document_frequency_.find(term);
    return it != document_frequency_.end() ? it->second : 0;
}

size_t TfidfVectorizer::get_num_documents() const {
    return num_documents_;
}

std::vector<std::string> TfidfVectorizer::tokenize(const std::string &text) const {
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

std::vector<std::string> TfidfVectorizer::extract_code_identifiers(const std::string &content) const {
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

void TfidfVectorizer::index_document(const std::string &doc_id, const std::string &content) {
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

// ============================================================================
// RagEngine - Private helper methods
// ============================================================================

bool RagEngine::should_skip_directory(const std::string &path) const {
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

bool RagEngine::is_source_file(const std::string &ext) const {
    return std::find(source_extensions_.begin(), source_extensions_.end(), ext)
           != source_extensions_.end();
}

bool RagEngine::is_config_file(const std::string &ext) const {
    return std::find(config_extensions_.begin(), config_extensions_.end(), ext)
           != config_extensions_.end();
}

std::string RagEngine::get_language_from_extension(const std::string &ext) const {
    static const std::map<std::string, std::string> lang_map = {
        {".cpp", "C++"}, {".c", "C"}, {".h", "C/C++ Header"},
        {".hpp", "C++ Header"}, {".java", "Java"}, {".py", "Python"},
        {".js", "JavaScript"}, {".ts", "TypeScript"}
    };

    auto it = lang_map.find(ext);
    return it != lang_map.end() ? it->second : "Unknown";
}

Document RagEngine::read_file(const std::string &path, const std::string &type,
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

std::string RagEngine::extract_snippet(const Document &doc,
                                       const std::vector<std::string> &query_terms,
                                       size_t context_lines) const {
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

std::vector<std::pair<size_t, size_t>> RagEngine::find_match_locations(
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

std::vector<std::string> RagEngine::split_lines(const std::string &text) const {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    return lines;
}

// ============================================================================
// RagEngine - Public methods
// ============================================================================

RagEngine::RagEngine(const RagEngineConfig &config)
    : search_config_(config.search),
      embedding_config_(config.embedding),
      max_file_size_bytes_(config.max_file_size_kb * 1024) {
    initialize_embedding_backend();
}

RagEngine::~RagEngine() {
    if (hnsw_index_) {
        delete hnsw_index_;
    }
    if (space_) {
        delete space_;
    }
}

std::vector<float> RagEngine::generate_tfidf_embedding(const std::string &content) {
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

void RagEngine::set_search_config(const HybridSearchConfig &config) {
    search_config_ = config;
}

void RagEngine::set_embedding_config(const EmbeddingConfig &config) {
    embedding_config_ = config;
    initialize_embedding_backend();
}

const EmbeddingConfig& RagEngine::get_embedding_config() const {
    return embedding_config_;
}

std::string RagEngine::get_embedding_backend() const {
    if (embedding_config_.backend == "onnx" && onnx_ready_) {
        return "onnx";
    }
    return "tfidf";
}

size_t RagEngine::get_embedding_dim() const {
    return embedding_dim_;
}

bool RagEngine::is_onnx_ready() const {
    return onnx_ready_;
}

std::string RagEngine::get_onnx_status_message() const {
    return onnx_status_message_;
}

std::vector<float> RagEngine::generate_embedding(const std::string &query) {
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

void RagEngine::index_project(const std::string &project_root) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto index_start = std::chrono::steady_clock::now();
    indexed_project_root_ = project_root;

    documents_.clear();
    path_to_index_.clear();
    vectorizer_ = TfidfVectorizer();

    std::cout << "🔍 Сканирование проекта: " << project_root << std::endl;

    try {
        for (const auto &entry: std::filesystem::recursive_directory_iterator(project_root)) {
            if (stop_flag_ && !stop_flag_->load()) {
                std::cout << "🛑 Индексация прервана сигналом остановки" << std::endl;
                is_indexed_ = false;
                auto index_end = std::chrono::steady_clock::now();
                last_index_duration_ms_ = static_cast<size_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
                );
                return;
            }
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string path = entry.path().string();
            std::string ext = entry.path().extension().string();

            if (should_skip_directory(entry.path().parent_path().string())) {
                continue;
            }

            std::string type;
            std::string language;

            if (is_source_file(ext)) {
                type = "source";
                language = get_language_from_extension(ext);
            } else if (is_config_file(ext)) {
                type = "config";
                language = "text";
            } else {
                continue;
            }

            try {
                if (entry.file_size() > max_file_size_bytes_) {
                    continue;
                }
            } catch (...) {
                continue;
            }

            Document doc = read_file(path, type, language);
            if (doc.content.empty()) {
                continue;
            }
            if (stop_flag_ && !stop_flag_->load()) {
                std::cout << "🛑 Индексация прервана сигналом остановки" << std::endl;
                is_indexed_ = false;
                auto index_end = std::chrono::steady_clock::now();
                last_index_duration_ms_ = static_cast<size_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
                );
                return;
            }

            DocumentChunker chunker;
            auto chunks = chunker.chunkDocument(doc);
            if (chunks.empty()) {
                chunks.push_back(doc);
            }

            for (auto& chunk : chunks) {
                chunk.embedding = generate_embedding(chunk.content);
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
    } catch (const std::exception &e) {
        std::cerr << "Error scanning project: " << e.what() << std::endl;
    }

    is_indexed_ = true;

    auto stats = get_statistics();
    std::cout << "✅ Проиндексировано файлов: " << stats.total_files << std::endl;
    std::cout << "   Всего строк: " << stats.total_lines << std::endl;
    std::cout << "   Размер: " << (stats.total_size_bytes / 1024) << " KB" << std::endl;

    build_hybrid_index();
    auto index_end = std::chrono::steady_clock::now();
    last_index_duration_ms_ = static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count()
    );
    std::cout << "   Время индексации: " << last_index_duration_ms_ << " ms" << std::endl;
}

// ============================================================================
// RagEngine - Private ONNX-related methods
// ============================================================================

std::string RagEngine::trim(const std::string &value) {
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

bool RagEngine::parse_tokenizer_vocab(const std::string &tokenizer_path) {
    tokenizer_vocab_.clear();

    std::ifstream file(tokenizer_path);
    if (!file.is_open()) {
        onnx_status_message_ = "tokenizer.json not found: " + tokenizer_path;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    try {
        auto parsed = boost::json::parse(buffer.str());
        if (!parsed.is_object()) {
            onnx_status_message_ = "tokenizer.json root is not object";
            return false;
        }

        auto &root = parsed.as_object();
        if (!root.contains("model")) {
            onnx_status_message_ = "tokenizer.json does not contain model";
            return false;
        }

        auto &model = root["model"].as_object();
        if (!model.contains("vocab")) {
            onnx_status_message_ = "tokenizer.json does not contain model.vocab";
            return false;
        }

        auto &vocab_value = model["vocab"];
        if (vocab_value.is_object()) {
            auto &vocab = vocab_value.as_object();
            tokenizer_vocab_.reserve(vocab.size());
            for (const auto &entry: vocab) {
                if (!entry.value().is_int64()) {
                    continue;
                }
                tokenizer_vocab_[std::string(entry.key())] = entry.value().as_int64();
            }
        } else if (vocab_value.is_array()) {
            auto &vocab = vocab_value.as_array();
            tokenizer_vocab_.reserve(vocab.size());
            for (size_t i = 0; i < vocab.size(); ++i) {
                if (!vocab[i].is_array()) {
                    continue;
                }
                auto &item = vocab[i].as_array();
                if (item.empty() || !item[0].is_string()) {
                    continue;
                }
                tokenizer_vocab_[std::string(item[0].as_string().c_str())] = static_cast<int64_t>(i);
            }
        } else {
            onnx_status_message_ = "tokenizer.json model.vocab has unsupported type";
            return false;
        }

        auto it_unk = tokenizer_vocab_.find("[UNK]");
        if (it_unk != tokenizer_vocab_.end()) unk_token_id_ = it_unk->second;
        auto it_cls = tokenizer_vocab_.find("[CLS]");
        if (it_cls != tokenizer_vocab_.end()) cls_token_id_ = it_cls->second;
        auto it_sep = tokenizer_vocab_.find("[SEP]");
        if (it_sep != tokenizer_vocab_.end()) sep_token_id_ = it_sep->second;
        auto it_pad = tokenizer_vocab_.find("[PAD]");
        if (it_pad != tokenizer_vocab_.end()) pad_token_id_ = it_pad->second;

        auto it_unk_alt = tokenizer_vocab_.find("<unk>");
        if (it_unk_alt != tokenizer_vocab_.end()) unk_token_id_ = it_unk_alt->second;
        auto it_cls_alt = tokenizer_vocab_.find("<s>");
        if (it_cls_alt != tokenizer_vocab_.end()) cls_token_id_ = it_cls_alt->second;
        auto it_sep_alt = tokenizer_vocab_.find("</s>");
        if (it_sep_alt != tokenizer_vocab_.end()) sep_token_id_ = it_sep_alt->second;
        auto it_pad_alt = tokenizer_vocab_.find("<pad>");
        if (it_pad_alt != tokenizer_vocab_.end()) pad_token_id_ = it_pad_alt->second;

        return !tokenizer_vocab_.empty();
    } catch (const std::exception &e) {
        onnx_status_message_ = std::string("tokenizer parse failed: ") + e.what();
        return false;
    }
}

std::vector<std::string> RagEngine::basic_tokenize_for_onnx(const std::string &text) const {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(32);

    for (char raw_ch: text) {
        unsigned char ch = static_cast<unsigned char>(raw_ch);
        if (std::isalnum(ch) || ch == '_') {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

std::pair<std::vector<int64_t>, std::vector<int64_t>> RagEngine::encode_for_onnx(const std::string &text) const {
    std::vector<int64_t> input_ids;
    std::vector<int64_t> attention_mask;
    input_ids.reserve(embedding_config_.max_seq_len);
    attention_mask.reserve(embedding_config_.max_seq_len);

    input_ids.push_back(cls_token_id_);
    attention_mask.push_back(1);

    for (const auto &token: basic_tokenize_for_onnx(text)) {
        if (input_ids.size() + 1 >= embedding_config_.max_seq_len) {
            break;
        }
        auto it = tokenizer_vocab_.find(token);
        input_ids.push_back(it != tokenizer_vocab_.end() ? it->second : unk_token_id_);
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

void RagEngine::normalize_l2(std::vector<float> &vec) const {
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

void RagEngine::initialize_embedding_backend() {
    onnx_ready_ = false;
    embedding_dim_ = EMBEDDING_DIM;
    onnx_status_message_ = "tfidf backend active";

    if (embedding_config_.backend != "onnx") {
        return;
    }

#ifndef QORNIX_HAS_ONNX
    onnx_status_message_ = "onnx backend requested but binary was built without ONNX Runtime";
    return;
#else
    if (!parse_tokenizer_vocab(embedding_config_.tokenizer_path)) {
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
    } catch (const std::exception &e) {
        onnx_status_message_ = std::string("onnx init failed: ") + e.what();
        if (!embedding_config_.enable_fallback) {
            throw;
        }
    }
#endif
}

std::vector<float> RagEngine::generate_onnx_embedding(const std::string &text) {
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
        normalize_l2(embedding);
        embedding_dim_ = embedding.size();
        return embedding;
    }

    if (shape.size() == 2) {
        const int64_t hidden_dim = shape[1];
        if (hidden_dim <= 0) {
            return {};
        }
        std::vector<float> embedding(raw, raw + hidden_dim);
        normalize_l2(embedding);
        embedding_dim_ = embedding.size();
        return embedding;
    }

    return {};
#endif
}

// ============================================================================
// RagEngine - Hybrid index and search methods
// ============================================================================

void RagEngine::build_hybrid_index() {
    if (documents_.empty()) {
        throw std::runtime_error("No documents to index. Call index_project() first.");
    }

    std::cout << "🔨 Построение гибридного индекса..." << std::endl;
    bool hnsw_ready = false;
    bool xapian_ready = false;

    try {
        if (hnsw_index_) {
            delete hnsw_index_;
            hnsw_index_ = nullptr;
        }

        if (space_) {
            delete space_;
            space_ = nullptr;
        }

        space_ = new hnswlib::L2Space(embedding_dim_);

        hnsw_index_ = new hnswlib::HierarchicalNSW<float>(
            space_,
            documents_.size(),
            16,
            200
        );

        for (size_t i = 0; i < documents_.size(); ++i) {
            const auto& doc = documents_[i];
            if (!doc.embedding.empty() && doc.embedding.size() == embedding_dim_) {
                hnsw_index_->addPoint(doc.embedding.data(), static_cast<hnswlib::labeltype>(i));
            }
        }

        std::cout << "✅ HNSW индекс построен (" << documents_.size() << " векторов)" << std::endl;
        hnsw_ready = true;
    } catch (const std::exception& e) {
        std::cerr << "❌ Error building HNSW index: " << e.what() << std::endl;
    }


    try {
        // Release previous handle before opening new DB to avoid self-lock.
        xapian_db_.reset();
        xapian_db_ = std::make_unique<Xapian::WritableDatabase>("xapian_index", Xapian::DB_CREATE_OR_OVERWRITE);

        Xapian::TermGenerator termgen;
        Xapian::Stem stemmer("english");
        termgen.set_stemmer(stemmer);

        for (size_t i = 0; i < documents_.size(); ++i) {
            const auto &doc = documents_[i];

            Xapian::Document xdoc;
            xdoc.set_data(std::to_string(i));

            termgen.set_document(xdoc);

            termgen.index_text(doc.relative_path, 2.0, "QPATH");
            xdoc.add_term("QPATH" + doc.relative_path, 2.0);

            termgen.index_text(doc.content);

            std::istringstream path_stream(doc.relative_path);
            std::string path_token;
            while (path_stream >> path_token) {
                xdoc.add_posting("QPATH" + path_token, i, 2.0);
            }

            xapian_db_->add_document(xdoc);
        }

        xapian_db_->commit();
        std::cout << "✅ Xapian индекс построен (" << documents_.size() << " документов)" << std::endl;
        xapian_ready = true;
    } catch (const Xapian::Error &e) {
        xapian_db_.reset();
        std::cerr << "❌ Error building Xapian index: " << e.get_description() << std::endl;
    }

    is_hybrid_indexed_ = hnsw_ready;
    if (hnsw_ready && xapian_ready) {
        std::cout << "✅ Гибридный индекс построен: HNSW + Xapian" << std::endl;
    } else if (hnsw_ready) {
        std::cout << "⚠️  Гибридный индекс частично готов: только HNSW" << std::endl;
    } else {
        std::cout << "❌ Гибридный индекс не построен" << std::endl;
    }
}

size_t RagEngine::get_document_id_from_data(const std::string &data) const {
    try {
        return std::stoul(data);
    } catch (...) {
        return 0;
    }
}

std::vector<SearchResult> RagEngine::hybrid_search(
    const std::string &query,
    const std::vector<float> &query_embedding,
    size_t top_k
) {
    if (!is_hybrid_indexed_) {
        std::cout << "⚠️  Hybrid index not ready, falling back to BM25" << std::endl;
        return search(query, top_k);
    }

    std::unordered_map<size_t, double> combined_scores;
    std::unordered_map<size_t, double> hnsw_scores;
    std::unordered_map<size_t, double> xapian_scores;

    try {
        if (hnsw_index_ && !query_embedding.empty()) {
            auto result = hnsw_index_->searchKnn(query_embedding.data(), top_k * 2);

            while (!result.empty()) {
                auto top = result.top();
                size_t doc_id = static_cast<size_t>(top.second);
                float distance = top.first;

                double score = 1.0 / (1.0 + distance);
                if (score > 0.0) {
                    hnsw_scores[doc_id] = score;
                }

                result.pop();
            }

            std::cout << "📊 HNSW нашел: " << hnsw_scores.size() << " результатов" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "⚠️  HNSW search error: " << e.what() << std::endl;
    }

    try {
        if (xapian_db_) {
            Xapian::Enquire enquire(*xapian_db_);

            std::vector<std::string> query_terms = vectorizer_.tokenize(query);
            Xapian::QueryParser parser;
            parser.set_database(*xapian_db_);
            parser.set_default_op(Xapian::Query::op::OP_OR);

            Xapian::Query query_obj;
            for (const auto& term : query_terms) {
                query_obj = Xapian::Query(Xapian::Query::op::OP_OR, query_obj, Xapian::Query(term));
            }

            enquire.set_query(query_obj);
            auto matches = enquire.get_mset(0, top_k * 2);

            for (auto it = matches.begin(); it != matches.end(); ++it) {
                std::string data = it.get_document().get_data();
                size_t doc_id = get_document_id_from_data(data);
                double score = static_cast<double>(it.get_percent()) / 100.0;
                xapian_scores[doc_id] = score;
            }

            std::cout << "📊 Xapian нашел: " << matches.size() << " результатов" << std::endl;
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
    auto query_terms = vectorizer_.tokenize(query);

    for (const auto &[doc_idx, score]: sorted_results) {
        if (final_results.size() >= top_k) break;
        if (score < search_config_.min_score_threshold) continue;

        if (doc_idx < documents_.size()) {
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

    std::cout << "✅ Гибридный поиск вернул: " << final_results.size() << " результатов" << std::endl;
    return final_results;
}

std::vector<SearchResult> RagEngine::search(const std::string &query, size_t top_k) {
    if (!is_indexed_) {
        throw std::runtime_error("Project not indexed. Call index_project() first.");
    }

    if (search_config_.use_hybrid && is_hybrid_indexed_) {
        auto query_embedding = generate_embedding(query);
        return hybrid_search(query, query_embedding, top_k);
    }

    std::vector<std::string> query_terms = vectorizer_.tokenize(query);
    std::vector<SearchResult> results;

    for (const auto &doc: documents_) {
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

    if (results.size() > top_k) {
        results.resize(top_k);
    }

    return results;
}

std::string RagEngine::build_context(const std::string &query, size_t max_length) {
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

void RagEngine::set_stop_flag(const std::atomic<bool>* stop_flag) {
    stop_flag_ = stop_flag;
}

ProjectStats RagEngine::get_statistics() {
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

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    stats.last_indexed = std::ctime(&time);

    return stats;
}

bool RagEngine::is_indexed() const {
    return is_indexed_;
}

std::string RagEngine::get_indexed_project_root() const {
    return indexed_project_root_;
}

const Document *RagEngine::get_document(const std::string &relative_path) const {
    auto it = path_to_index_.find(relative_path);
    if (it != path_to_index_.end()) {
        return &documents_[it->second];
    }
    return nullptr;
}

// ============================================================================
// Phase 4: Data Source Management
// ============================================================================

void RagEngine::addDataSource(std::shared_ptr<DataSource> source) {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    data_sources_.push_back(std::move(source));
}

void RagEngine::removeDataSource(const std::string& source_id) {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    data_sources_.erase(
        std::remove_if(data_sources_.begin(), data_sources_.end(),
            [&source_id](const std::shared_ptr<DataSource>& src) {
                return src->getId() == source_id;
            }),
        data_sources_.end()
    );
}

std::vector<std::shared_ptr<DataSource>> RagEngine::getDataSources() const {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    return data_sources_;
}

size_t RagEngine::getTotalDataSourceCount() const {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    size_t total = 0;
    for (const auto& source : data_sources_) {
        total += source->count();
    }
    return total;
}

void RagEngine::indexSources() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto index_start = std::chrono::steady_clock::now();

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
                chunk.embedding = generate_embedding(chunk.content);

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
    std::cout << "   Index duration: " << last_index_duration_ms_ << " ms" << std::endl;
}

std::vector<std::string> RagEngine::getSourceIds() const {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    std::vector<std::string> ids;
    ids.reserve(data_sources_.size());
    for (const auto& source : data_sources_) {
        ids.push_back(source->getId());
    }
    return ids;
}

std::vector<DataSourceType> RagEngine::getSourceTypes() const {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    std::vector<DataSourceType> types;
    types.reserve(data_sources_.size());
    for (const auto& source : data_sources_) {
        types.push_back(source->getType());
    }
    return types;
}
