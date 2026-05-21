#pragma once

#include "data_source.h"
#include "qa_source.h"

using qornix::rag::QASource;

#if QORNIX_HAS_SQLITE
#include <sqlite3.h>
#endif

#include <string>
#include <vector>
#include <optional>
#include <map>
#include <mutex>

/**
 * SQLite-backed data source for persistent QA pairs storage.
 * All QA pairs are stored in SQLite database and survive restarts.
 *
 * Schema:
 *   qa_pairs(id, question, answer, category, aliases, metadata, version,
 *            created_at, updated_at, source_id, hash)
 *   search_log(id, query, result_count, has_answer, response_time_ms,
 *              client_ip, timestamp, top_result_path)
 *   import_history(id, file_path, documents_imported, duplicates_found,
 *                  status, error_message, imported_at)
 */
class SQLiteSource : public DataSource {
public:
    struct Config {
        std::string db_path;                      // Path to SQLite database
        std::string source_id;                    // Source identifier
        std::string name = "SQLite Knowledge Base"; // Source name
        bool auto_migrate = true;                 // Auto-create tables
    };

    explicit SQLiteSource(Config config);
    ~SQLiteSource() override;

    // Disable copy
    SQLiteSource(const SQLiteSource&) = delete;
    SQLiteSource& operator=(const SQLiteSource&) = delete;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::DATABASE; }
    size_t count() const override;
    std::string getId() const override { return config_.source_id; }
    std::string getName() const override { return config_.name; }
    bool initialize() override;
    void cleanup() override;

    // QA-specific API (extends QASource)
    bool addQAPair(const std::string& id, const std::string& question,
                   const std::string& answer, const std::string& category = "general",
                   const std::string& aliases = "[]", const std::string& metadata = "{}");
    bool updateQAPair(const std::string& id, const std::string& answer = "",
                      const std::string& category = "", const std::string& aliases = "");
    bool deleteQAPair(const std::string& id);
    std::optional<QASource::QAPair> findQAPair(const std::string& id) const;
    std::vector<QASource::QAPair> searchByCategory(const std::string& category) const;
    std::vector<QASource::QAPair> searchByQuestion(const std::string& query) const;
    std::vector<QASource::QAPair> getAllPairs(size_t page = 1, size_t per_page = 20) const;

    // Migration
    bool migrate();

    // Database info
    std::string getDbPath() const { return config_.db_path; }
    std::string getSourceId() const { return config_.source_id; }

private:
    Config config_;
#if QORNIX_HAS_SQLITE
    sqlite3* db_ = nullptr;
#endif
    mutable std::mutex mutex_;

    // Internal helpers
    bool connect();
    bool executeStatement(const std::string& sql);
    bool executePreparedStatement(const std::string& sql,
                                   const std::vector<std::string>& bind_values);
    std::string computeHash(const std::string& id, const std::string& question,
                            const std::string& answer) const;
    QASource::QAPair rowToQAPair(sqlite3_stmt* stmt) const;
    Document qaPairToDocument(const QASource::QAPair& pair) const;
};
