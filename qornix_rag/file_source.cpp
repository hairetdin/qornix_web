/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "file_source.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace qornix {
namespace rag {

FileSource::FileSource(Config config) : config_(std::move(config)) {
    // Set default extensions if not specified
    if (config_.include_extensions.empty()) {
        config_.include_extensions = {
            ".cpp", ".c", ".cc", ".cxx", ".c++",
            ".h", ".hpp", ".hxx", ".h++",
            ".java", ".cs", ".py", ".js", ".ts",
            ".go", ".rs", ".swift", ".kt", ".scala",
            ".txt", ".md", ".rst", ".adoc",
            ".yaml", ".yml", ".json", ".xml", ".toml", ".ini", ".cfg", ".conf"
        };
    }

    // Set default exclude directories if not specified
    if (config_.exclude_directories.empty()) {
        config_.exclude_directories = {
            "cmake-build", ".git", "build", "__pycache__",
            "node_modules", ".venv", "dist", "bin", "obj",
            "qornix_rag/models", "/models/"
        };
    }
}

bool FileSource::initialize() {
    if (initialized_) return true;

    // Validate root path exists
    if (!std::filesystem::exists(config_.root_path)) {
        std::cerr << "FileSource: Root path does not exist: " << config_.root_path << std::endl;
        return false;
    }

    if (!std::filesystem::is_directory(config_.root_path)) {
        std::cerr << "FileSource: Root path is not a directory: " << config_.root_path << std::endl;
        return false;
    }

    initialized_ = true;
    return true;
}

void FileSource::cleanup() {
    cached_docs_.clear();
    initialized_ = false;
}

void FileSource::setRootPath(const std::string& path) {
    config_.root_path = path;
    cached_docs_.clear();
    initialized_ = false;
}

void FileSource::addExcludeDirectory(const std::string& dir) {
    config_.exclude_directories.push_back(dir);
}

void FileSource::removeExcludeDirectory(const std::string& dir) {
    config_.exclude_directories.erase(
        std::remove(config_.exclude_directories.begin(), config_.exclude_directories.end(), dir),
        config_.exclude_directories.end()
    );
}

bool FileSource::shouldSkipDirectory(const std::string& path) const {
    for (const auto& skip : config_.exclude_directories) {
        if (path.find(skip) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool FileSource::isAllowedExtension(const std::string& ext) const {
    return std::find(config_.include_extensions.begin(), config_.include_extensions.end(), ext)
           != config_.include_extensions.end();
}

std::string FileSource::getLanguageFromExtension(const std::string& ext) const {
    static const std::map<std::string, std::string> lang_map = {
        {".cpp", "C++"}, {".c", "C"}, {".h", "C/C++ Header"},
        {".hpp", "C++ Header"}, {".java", "Java"}, {".py", "Python"},
        {".js", "JavaScript"}, {".ts", "TypeScript"}, {".go", "Go"},
        {".rs", "Rust"}, {".swift", "Swift"}, {".kt", "Kotlin"},
        {".cs", "C#"}, {".cc", "C++"}, {".cxx", "C++"},
        {".md", "Markdown"}, {".rst", "reStructuredText"},
        {".txt", "Text"}, {".yaml", "YAML"}, {".yml", "YAML"},
        {".json", "JSON"}, {".xml", "XML"}, {".toml", "TOML"}
    };

    auto it = lang_map.find(ext);
    return it != lang_map.end() ? it->second : "text";
}

IngestionPipeline::Config FileSource::makeIngestionConfig() const {
    IngestionPipeline::Config config;
    config.root_path = config_.root_path;
    config.include_extensions = config_.include_extensions;
    config.exclude_directories = config_.exclude_directories;
    config.max_file_size_kb = config_.max_file_size_kb;
    config.recursive = config_.recursive;
    return config;
}

Document FileSource::fromIngestedDocument(const IngestedDocument& ingested) const {
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
    return doc;
}

Document FileSource::readFile(const std::string& path, const std::string& type,
                              const std::string& language) const {
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

        doc.relative_path = determineRelativePath(path);

        auto file_time = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        doc.last_modified = sctp;
    } catch (const std::exception& e) {
        std::cerr << "FileSource: Error reading file " << path << ": " << e.what() << std::endl;
    }

    return doc;
}

std::string FileSource::determineRelativePath(const std::string& full_path) const {
    // Try to find common path patterns
    if (full_path.find("/src/") != std::string::npos) {
        auto pos = full_path.find("/src/");
        return full_path.substr(pos + 5);
    } else if (full_path.find("/include/") != std::string::npos) {
        auto pos = full_path.find("/include/");
        return full_path.substr(pos + 9);
    }

    try {
        return std::filesystem::relative(full_path, config_.root_path).string();
    } catch (...) {
        return full_path;
    }
}

std::vector<Document> FileSource::getDocuments() {
    if (!initialized_) {
        if (!initialize()) {
            return {};
        }
    }

    cached_docs_.clear();

    std::cout << "📄 FileSource: Scanning directory: " << config_.root_path << std::endl;

    try {
        IngestionPipeline pipeline(makeIngestionConfig());
        auto result = pipeline.ingestRoot();
        cached_docs_.reserve(result.documents.size());
        for (const auto& ingested : result.documents) {
            if (progress_callback_) {
                progress_callback_(cached_docs_.size() + 1, result.documents.size());
            }
            cached_docs_.push_back(fromIngestedDocument(ingested));
        }
        if (result.errors > 0 || result.skipped > 0) {
            std::cout << "ℹ️ FileSource ingestion: seen=" << result.files_seen
                      << ", skipped=" << result.skipped
                      << ", duplicates=" << result.duplicates_found
                      << ", errors=" << result.errors << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "FileSource: Error scanning directory: " << e.what() << std::endl;
        return {};
    }

    std::cout << "✅ FileSource: Loaded " << cached_docs_.size() << " documents" << std::endl;
    return cached_docs_;
}

void FileSource::addDocument(const Document& doc) {
    cached_docs_.push_back(doc);
}

void FileSource::removeDocument(const std::string& id) {
    cached_docs_.erase(
        std::remove_if(cached_docs_.begin(), cached_docs_.end(),
            [&id](const Document& doc) { return doc.relative_path == id; }),
        cached_docs_.end()
    );
}

size_t FileSource::count() const {
    return cached_docs_.size();
}

const std::map<std::string, std::string>& FileSource::getLangMap() {
    static const std::map<std::string, std::string> lang_map = {
        {".cpp", "C++"}, {".c", "C"}, {".h", "C/C++ Header"},
        {".hpp", "C++ Header"}, {".java", "Java"}, {".py", "Python"},
        {".js", "JavaScript"}, {".ts", "TypeScript"}
    };
    return lang_map;
}

} // namespace rag
} // namespace qornix
