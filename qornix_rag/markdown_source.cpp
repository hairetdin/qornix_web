/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "markdown_source.h"
#include "core.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// Simple join implementation (replacement for boost::algorithm::join)
template<typename T>
std::string join_strings(const std::vector<T>& items, const std::string& delimiter) {
    std::ostringstream oss;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) oss << delimiter;
        oss << items[i];
    }
    return oss.str();
}

// ============================================================================
// MarkdownSource implementation
// ============================================================================

MarkdownSource::MarkdownSource(Config config)
    : config_(std::move(config)) {
    if (config_.file_patterns.empty()) {
        config_.file_patterns = {"*.md", "*.markdown", "*.txt", "*.rst"};
    }
}

bool MarkdownSource::initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (imported_files_.empty()) {
        importFiles();
    }

    return true;
}

void MarkdownSource::cleanup() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    imported_files_.clear();
    parsed_docs_.clear();
}

size_t MarkdownSource::count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return parsed_docs_.size();
}

std::vector<Document> MarkdownSource::getDocuments() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::vector<Document> docs;
    for (size_t i = 0; i < parsed_docs_.size(); ++i) {
        docs.push_back(parsedToDocument(parsed_docs_[i], i));
    }
    return docs;
}

void MarkdownSource::addDocument(const Document& doc) {
    // MarkdownSource is import-only; addDocument is not supported
    std::cerr << "MarkdownSource: addDocument not supported, use importFile instead" << std::endl;
}

void MarkdownSource::removeDocument(const std::string& id) {
    // MarkdownSource is import-only; removeDocument is not supported
    std::cerr << "MarkdownSource: removeDocument not supported" << std::endl;
}

MarkdownSource::ImportResult MarkdownSource::importFiles() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    ImportResult result;
    result.files_imported = 0;
    result.documents_created = 0;
    result.duplicates_skipped = 0;

    // Scan directory for files
    auto files = scanDirectory(config_.directory_path);

    for (const auto& file_path : files) {
        if (importFile(file_path)) {
            result.files_imported++;
        } else {
            result.errors.push_back("Failed to import: " + file_path);
        }
    }

    std::cout << "✅ MarkdownSource: Imported " << result.files_imported
              << " files, " << result.documents_created << " documents, "
              << result.duplicates_skipped << " duplicates skipped" << std::endl;

    return result;
}

bool MarkdownSource::importFile(const std::string& file_path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Check if already imported
    if (std::find(imported_files_.begin(), imported_files_.end(), file_path) != imported_files_.end()) {
        return true; // Already imported
    }

    // Read file
    std::string content = readFile(file_path);
    if (content.empty()) {
        std::cerr << "MarkdownSource: Empty file: " << file_path << std::endl;
        return false;
    }

    // Check file size
    if (content.size() > config_.max_file_size_kb * 1024) {
        std::cerr << "MarkdownSource: File too large: " << file_path << std::endl;
        return false;
    }

    // Parse frontmatter and body
    std::map<std::string, std::string> frontmatter;
    std::string body;
    if (!parseFrontmatter(content, frontmatter, body)) {
        // No frontmatter, use file content as body
        body = content;
    }

    // Create parsed document
    ParsedDocument parsed;
    parsed.file_path = file_path;
    parsed.title = frontmatter.count("title") ? frontmatter.at("title") : fs::path(file_path).filename().string();
    parsed.category = frontmatter.count("category") ? frontmatter.at("category") : "general";

    // Parse aliases
    if (frontmatter.count("aliases")) {
        std::string aliases_str = frontmatter.at("aliases");
        // Simple parsing: split by comma
        std::stringstream ss(aliases_str);
        std::string alias;
        while (std::getline(ss, alias, ',')) {
            alias = trim(alias);
            if (!alias.empty()) {
                parsed.aliases.push_back(alias);
            }
        }
    }

    parsed.content = body;
    parsed.metadata = frontmatter;

    // Check for duplicate title
    for (const auto& existing : parsed_docs_) {
        if (toLower(existing.title) == toLower(parsed.title)) {
            // Duplicate found
            return true; // Don't count as error
        }
    }

    parsed_docs_.push_back(parsed);
    imported_files_.push_back(file_path);
    return true;
}

std::vector<std::string> MarkdownSource::getImportedFiles() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return imported_files_;
}

void MarkdownSource::setDirectoryPath(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_.directory_path = path;
}

void MarkdownSource::setFilePatterns(const std::vector<std::string>& patterns) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_.file_patterns = patterns;
}

// ============================================================================
// Internal helpers
// ============================================================================

bool MarkdownSource::parseFrontmatter(const std::string& content,
                                       std::map<std::string, std::string>& frontmatter,
                                       std::string& body) {
    // Check for YAML frontmatter delimiters
    if (content.substr(0, 3) != "---") {
        return false;
    }

    size_t first_end = content.find("---", 3);
    if (first_end == std::string::npos) {
        return false;
    }

    // Find second delimiter
    size_t second_start = content.find("\n---", first_end + 3);
    if (second_start == std::string::npos) {
        second_start = content.find("---", first_end + 3);
        if (second_start == first_end + 3) {
            second_start = content.find("\n---", second_start);
        }
    }

    if (second_start == std::string::npos) {
        return false;
    }

    // Extract frontmatter
    std::string fm_content = content.substr(4, first_end - 4);

    // Parse YAML-like frontmatter (simple key: value parsing)
    std::stringstream ss(fm_content);
    std::string line;
    while (std::getline(ss, line)) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));

            // Remove quotes from value
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            } else if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }

            frontmatter[key] = value;
        }
    }

    // Extract body
    body = content.substr(second_start + 4);
    // Skip leading newline
    if (!body.empty() && body[0] == '\n') {
        body = body.substr(1);
    }

    return true;
}

std::string MarkdownSource::extractBody(const std::string& content) {
    size_t first_end = content.find("---", 3);
    if (first_end == std::string::npos) {
        return content;
    }

    size_t second_start = content.find("---", first_end + 3);
    if (second_start == std::string::npos) {
        return content;
    }

    std::string body = content.substr(second_start + 3);
    if (!body.empty() && body[0] == '\n') {
        body = body.substr(1);
    }
    return body;
}

std::string MarkdownSource::generateId(const std::string& file_path, const std::string& title) {
    // Simple ID generation: hash of file_path + title
    std::string combined = file_path + "|" + title;
    return "md_" + HashCalculator::compute_md5(combined).substr(0, 12);
}

std::vector<std::string> MarkdownSource::scanDirectory(const std::string& dir_path) {
    std::vector<std::string> files;

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        std::cerr << "MarkdownSource: Directory does not exist: " << dir_path << std::endl;
        return files;
    }

    if (config_.recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                std::string file_path = entry.path().string();

                // Check if file matches any pattern
                std::string ext = entry.path().extension().string();
                for (const auto& pattern : config_.file_patterns) {
                    std::string pattern_ext = pattern;
                    if (pattern_ext.substr(0, 1) == "*") {
                        pattern_ext = pattern_ext.substr(1); // Remove '*'
                    }
                    if (ext == pattern_ext) {
                        files.push_back(file_path);
                        break;
                    }
                }
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                std::string file_path = entry.path().string();
                std::string ext = entry.path().extension().string();

                for (const auto& pattern : config_.file_patterns) {
                    std::string pattern_ext = pattern;
                    if (pattern_ext.substr(0, 1) == "*") {
                        pattern_ext = pattern_ext.substr(1);
                    }
                    if (ext == pattern_ext) {
                        files.push_back(file_path);
                        break;
                    }
                }
            }
        }
    }

    return files;
}

std::string MarkdownSource::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string MarkdownSource::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::string MarkdownSource::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

Document MarkdownSource::parsedToDocument(const ParsedDocument& parsed, size_t index) const {
    Document doc;
    doc.path = "markdown://" + parsed.file_path;
    doc.relative_path = "markdown://" + parsed.file_path;
    doc.content = "Вопрос: " + parsed.title + "\n\nОтвет: " + parsed.content;
    doc.type = "markdown_document";
    doc.language = "text";
    doc.size_bytes = doc.content.size();
    doc.lines_count = 1 + parsed.content.size() / 50; // Approximate
    doc.hash = HashCalculator::compute_md5(parsed.file_path + parsed.title);
    doc.last_modified = std::chrono::system_clock::now();

    // Store metadata
    doc.metadata["md_file_path"] = parsed.file_path;
    doc.metadata["md_title"] = parsed.title;
    doc.metadata["md_category"] = parsed.category;

    if (!parsed.aliases.empty()) {
        doc.metadata["md_aliases"] = join_strings(parsed.aliases, ", ");
    }

    return doc;
}
