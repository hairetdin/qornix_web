/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "text_source.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace qornix {
namespace rag {

TextSource::TextSource(Config config) : config_(std::move(config)) {
    // Set default extensions if not specified
    if (config_.extensions.empty()) {
        config_.extensions = {".md", ".txt", ".rst", ".adoc", ".text", ".markdown"};
    }
}

bool TextSource::initialize() {
    if (initialized_) return true;

    // Validate directory path if specified
    if (!config_.directory_path.empty()) {
        if (!std::filesystem::exists(config_.directory_path)) {
            std::cerr << "TextSource: Directory does not exist: " << config_.directory_path << std::endl;
            return false;
        }
    }

    initialized_ = true;
    return true;
}

void TextSource::cleanup() {
    cached_docs_.clear();
    initialized_ = false;
}

bool TextSource::isAllowedExtension(const std::string& ext) const {
    return std::find(config_.extensions.begin(), config_.extensions.end(), ext)
           != config_.extensions.end();
}

Document TextSource::readFile(const std::string& path) const {
    Document doc;
    doc.path = path;

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
        doc.relative_path = path;
        doc.type = "text_doc";
        doc.language = "text";

        auto file_time = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        doc.last_modified = sctp;
    } catch (const std::exception& e) {
        std::cerr << "TextSource: Error reading file " << path << ": " << e.what() << std::endl;
    }

    return doc;
}

std::vector<Document> TextSource::getDocuments() {
    if (!initialized_) {
        if (!initialize()) {
            return {};
        }
    }

    cached_docs_.clear();
    const size_t max_bytes = config_.max_file_size_kb * 1024;

    // Load from explicit file paths
    for (const auto& file_path : config_.file_paths) {
        std::string ext = std::filesystem::path(file_path).extension().string();

        if (!isAllowedExtension(ext)) {
            continue;
        }

        try {
            if (std::filesystem::file_size(file_path) > max_bytes) {
                continue;
            }
        } catch (...) {
            continue;
        }

        Document doc = readFile(file_path);
        if (!doc.content.empty()) {
            cached_docs_.push_back(std::move(doc));
        }
    }

    // Load from directory
    if (!config_.directory_path.empty()) {
        try {
            if (config_.recursive) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(config_.directory_path)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }

                    std::string path = entry.path().string();
                    std::string ext = entry.path().extension().string();

                    if (!isAllowedExtension(ext)) {
                        continue;
                    }

                    try {
                        if (entry.file_size() > max_bytes) {
                            continue;
                        }
                    } catch (...) {
                        continue;
                    }

                    Document doc = readFile(path);
                    if (!doc.content.empty()) {
                        cached_docs_.push_back(std::move(doc));
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(config_.directory_path)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }

                    std::string path = entry.path().string();
                    std::string ext = entry.path().extension().string();

                    if (!isAllowedExtension(ext)) {
                        continue;
                    }

                    Document doc = readFile(path);
                    if (!doc.content.empty()) {
                        cached_docs_.push_back(std::move(doc));
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "TextSource: Error scanning directory: " << e.what() << std::endl;
        }
    }

    std::cout << "✅ TextSource: Loaded " << cached_docs_.size() << " documents" << std::endl;
    return cached_docs_;
}

void TextSource::addDocument(const Document& doc) {
    cached_docs_.push_back(doc);
}

void TextSource::removeDocument(const std::string& id) {
    cached_docs_.erase(
        std::remove_if(cached_docs_.begin(), cached_docs_.end(),
            [&id](const Document& doc) { return doc.relative_path == id; }),
        cached_docs_.end()
    );
}

size_t TextSource::count() const {
    return cached_docs_.size();
}

void TextSource::addFile(const std::string& path) {
    config_.file_paths.push_back(path);
}

void TextSource::removeFile(const std::string& path) {
    config_.file_paths.erase(
        std::remove(config_.file_paths.begin(), config_.file_paths.end(), path),
        config_.file_paths.end()
    );
}

void TextSource::addFiles(const std::vector<std::string>& paths) {
    config_.file_paths.insert(config_.file_paths.end(), paths.begin(), paths.end());
}

void TextSource::setDirectoryPath(const std::string& path) {
    config_.directory_path = path;
}

void TextSource::setExtensions(const std::vector<std::string>& extensions) {
    config_.extensions = extensions;
}

} // namespace rag
} // namespace qornix
