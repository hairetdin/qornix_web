#pragma once

#include "data_source.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

/**
 * Markdown file importer for knowledge base.
 * Parses Markdown files with YAML frontmatter and imports as documents.
 *
 * Supported format:
 * ---
 * title: "Question or Topic"
 * category: "setup"
 * aliases: ["alias1", "alias2"]
 * ---
 *
 * Answer content here...
 */
class MarkdownSource : public DataSource {
public:
    struct Config {
        std::string directory_path;             // Directory to scan
        std::vector<std::string> file_patterns; // Glob patterns (e.g., "*.md")
        bool recursive = true;
        size_t max_file_size_kb = 1024;
    };

    struct ParsedDocument {
        std::string file_path;
        std::string title;
        std::string category;
        std::vector<std::string> aliases;
        std::string content;
        std::map<std::string, std::string> metadata;
    };

    struct ImportResult {
        size_t files_imported;
        size_t documents_created;
        size_t duplicates_skipped;
        std::vector<std::string> errors;
    };

    explicit MarkdownSource(Config config);
    ~MarkdownSource() override = default;

    // Disable copy
    MarkdownSource(const MarkdownSource&) = delete;
    MarkdownSource& operator=(const MarkdownSource&) = delete;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::TEXT_DOCS; }
    size_t count() const override;
    std::string getId() const override { return "markdown_" + config_.directory_path; }
    std::string getName() const override { return "Markdown Knowledge Base"; }
    bool initialize() override;
    void cleanup() override;

    // Import API
    ImportResult importFiles();
    bool importFile(const std::string& file_path);
    std::vector<std::string> getImportedFiles() const;

    // Configuration
    void setDirectoryPath(const std::string& path);
    void setFilePatterns(const std::vector<std::string>& patterns);

private:
    Config config_;
    std::vector<std::string> imported_files_;
    std::vector<ParsedDocument> parsed_docs_;
    mutable std::recursive_mutex mutex_;

    // Internal helpers
    bool parseFrontmatter(const std::string& content, std::map<std::string, std::string>& frontmatter, std::string& body);
    std::string extractBody(const std::string& content);
    std::string generateId(const std::string& file_path, const std::string& title);
    std::vector<std::string> scanDirectory(const std::string& dir_path);
    std::string readFile(const std::string& path);
    std::string trim(const std::string& str);
    std::string toLower(const std::string& str);
    Document parsedToDocument(const ParsedDocument& parsed, size_t index) const;
};
