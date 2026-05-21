#include "sqlite_source.h"
#include "core.h"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

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

#if !QORNIX_HAS_SQLITE
#error "SQLiteSource requires QORNIX_HAS_SQLITE to be defined"
#endif

// ============================================================================
// SQLiteSource implementation
// ============================================================================

SQLiteSource::SQLiteSource(Config config)
    : config_(std::move(config)) {
}

SQLiteSource::~SQLiteSource() {
    cleanup();
}

bool SQLiteSource::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    std::cerr << "SQLiteSource: SQLite3 not available" << std::endl;
    return false;
#endif

    if (db_) {
        return true; // Already connected
    }

    if (!connect()) {
        return false;
    }

    if (config_.auto_migrate) {
        return migrate();
    }

    return true;
}

void SQLiteSource::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

#if QORNIX_HAS_SQLITE
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
#endif
}

bool SQLiteSource::connect() {
#if QORNIX_HAS_SQLITE
    int rc = sqlite3_open_v2(config_.db_path.c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQLiteSource: Failed to open database: "
                  << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrency
    executeStatement("PRAGMA journal_mode=WAL");

    // Enable foreign keys
    executeStatement("PRAGMA foreign_keys=ON");

    // Set busy timeout (5 seconds)
    sqlite3_busy_timeout(db_, 5000);

    return true;
#else
    return false;
#endif
}

bool SQLiteSource::migrate() {
    if (!db_ && !connect()) {
        return false;
    }

    const char* create_tables = R"(
        -- QA Pairs table
        CREATE TABLE IF NOT EXISTS qa_pairs (
            id TEXT PRIMARY KEY,
            question TEXT NOT NULL,
            answer TEXT NOT NULL,
            category TEXT DEFAULT 'general',
            aliases TEXT DEFAULT '[]',
            metadata TEXT DEFAULT '{}',
            version INTEGER DEFAULT 1,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            source_id TEXT NOT NULL,
            hash TEXT NOT NULL,
            UNIQUE(source_id, hash)
        );

        -- Search Log table
        CREATE TABLE IF NOT EXISTS search_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            query TEXT NOT NULL,
            result_count INTEGER DEFAULT 0,
            has_answer INTEGER DEFAULT 0,
            response_time_ms INTEGER,
            client_ip TEXT,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            top_result_path TEXT
        );

        -- Import History table
        CREATE TABLE IF NOT EXISTS import_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT NOT NULL,
            documents_imported INTEGER DEFAULT 0,
            duplicates_found INTEGER DEFAULT 0,
            status TEXT DEFAULT 'completed',
            error_message TEXT,
            imported_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        -- Indexes for performance
        CREATE INDEX IF NOT EXISTS idx_qa_pairs_source ON qa_pairs(source_id);
        CREATE INDEX IF NOT EXISTS idx_qa_pairs_category ON qa_pairs(category);
        CREATE INDEX IF NOT EXISTS idx_qa_pairs_hash ON qa_pairs(hash);
        CREATE INDEX IF NOT EXISTS idx_search_log_timestamp ON search_log(timestamp);
        CREATE INDEX IF NOT EXISTS idx_search_log_query ON search_log(query);
        CREATE INDEX IF NOT EXISTS idx_import_history_file ON import_history(file_path);
    )";

    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, create_tables, nullptr, nullptr, &error_msg);

    if (rc != SQLITE_OK) {
        std::cerr << "SQLiteSource: Migration failed: "
                  << (error_msg ? error_msg : "unknown error") << std::endl;
        if (error_msg) {
            sqlite3_free(error_msg);
        }
        return false;
    }

    std::cout << "✅ SQLiteSource: Migration completed successfully" << std::endl;
    return true;
}

size_t SQLiteSource::count() const {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return 0;
#endif

    if (!db_) {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM qa_pairs WHERE source_id = '"
                      + config_.source_id + "'";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return 0;
    }

    rc = sqlite3_step(stmt);
    size_t result = 0;
    if (rc == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Document> SQLiteSource::getDocuments() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Document> docs;

#if !QORNIX_HAS_SQLITE
    return docs;
#endif

    if (!db_) {
        return docs;
    }

    std::string sql = "SELECT id, question, answer, category, aliases, metadata "
                      "FROM qa_pairs WHERE source_id = '" + config_.source_id + "'";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return docs;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        QASource::QAPair pair = rowToQAPair(stmt);
        Document doc = qaPairToDocument(pair);
        docs.push_back(doc);
    }

    sqlite3_finalize(stmt);
    return docs;
}

void SQLiteSource::addDocument(const Document& doc) {
    // Extract QA pair data from document metadata
    std::string id = doc.metadata.count("qa_id") ? doc.metadata.at("qa_id") : doc.path;
    std::string question, answer, category = "general";
    std::string aliases = "[]";

    // Parse content for question/answer
    size_t question_pos = doc.content.find("Вопрос: ");
    size_t answer_pos = doc.content.find("\n\nОтвет: ");

    if (question_pos != std::string::npos && answer_pos != std::string::npos) {
        question = doc.content.substr(question_pos + 8, answer_pos - question_pos - 8);
        answer = doc.content.substr(answer_pos + 6);
    }

    if (doc.metadata.count("category")) {
        category = doc.metadata.at("category");
    }

    addQAPair(id, question, answer, category, aliases);
}

void SQLiteSource::removeDocument(const std::string& id) {
    deleteQAPair(id);
}

bool SQLiteSource::addQAPair(const std::string& id, const std::string& question,
                              const std::string& answer, const std::string& category,
                              const std::string& aliases, const std::string& metadata) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return false;
#endif

    if (!db_) {
        return false;
    }

    std::string hash = computeHash(id, question, answer);

    // Check for duplicate hash
    std::string check_sql = "SELECT COUNT(*) FROM qa_pairs WHERE source_id = ? AND hash = ?";
    sqlite3_stmt* check_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, check_sql.c_str(), -1, &check_stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, config_.source_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(check_stmt, 2, hash.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(check_stmt);
        if (rc == SQLITE_ROW && sqlite3_column_int(check_stmt, 0) > 0) {
            sqlite3_finalize(check_stmt);
            return false;
        }
        sqlite3_finalize(check_stmt);
    }

    // Insert new pair
    auto now_time = std::chrono::system_clock::now()
                    .time_since_epoch()
                    .count();
    std::string now_str = std::to_string(now_time);
    std::string insert_sql = "INSERT OR IGNORE INTO qa_pairs "
                             "(id, question, answer, category, aliases, metadata, "
                             "created_at, updated_at, source_id, hash) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    return executePreparedStatement(insert_sql, {
        id, question, answer, category, aliases, metadata,
        now_str, now_str, config_.source_id, hash
    });
}

bool SQLiteSource::updateQAPair(const std::string& id, const std::string& answer,
                                 const std::string& category, const std::string& aliases) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return false;
#endif

    if (!db_) {
        return false;
    }

    std::string update_sql = "UPDATE qa_pairs SET ";
    std::vector<std::string> set_clauses;
    std::vector<std::string> bind_values;

    if (!answer.empty()) {
        set_clauses.push_back("answer = ?");
        bind_values.push_back(answer);
    }
    if (!category.empty()) {
        set_clauses.push_back("category = ?");
        bind_values.push_back(category);
    }
    if (!aliases.empty()) {
        set_clauses.push_back("aliases = ?");
        bind_values.push_back(aliases);
    }

    if (set_clauses.empty()) {
        return true; // Nothing to update
    }

    set_clauses.push_back("updated_at = CURRENT_TIMESTAMP");
    set_clauses.push_back("version = version + 1");

    update_sql += join_strings(set_clauses, ", ");
    update_sql += " WHERE id = ? AND source_id = ?";

    bind_values.push_back(id);
    bind_values.push_back(config_.source_id);

    return executePreparedStatement(update_sql, bind_values);
}

bool SQLiteSource::deleteQAPair(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return false;
#endif

    if (!db_) {
        return false;
    }

    std::string delete_sql = "DELETE FROM qa_pairs WHERE id = ? AND source_id = ?";
    return executePreparedStatement(delete_sql, {id, config_.source_id});
}

std::optional<QASource::QAPair> SQLiteSource::findQAPair(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return std::nullopt;
#endif

    if (!db_) {
        return std::nullopt;
    }

    std::string sql = "SELECT id, question, answer, category, aliases, metadata "
                      "FROM qa_pairs WHERE id = ? AND source_id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, config_.source_id.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    std::optional<QASource::QAPair> result = std::nullopt;

    if (rc == SQLITE_ROW) {
        result = rowToQAPair(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<QASource::QAPair> SQLiteSource::searchByCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<QASource::QAPair> pairs;

#if !QORNIX_HAS_SQLITE
    return pairs;
#endif

    if (!db_) {
        return pairs;
    }

    std::string sql = "SELECT id, question, answer, category, aliases, metadata "
                      "FROM qa_pairs WHERE source_id = ? AND category = ? "
                      "ORDER BY updated_at DESC";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return pairs;
    }

    sqlite3_bind_text(stmt, 1, config_.source_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, category.c_str(), -1, SQLITE_STATIC);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        pairs.push_back(rowToQAPair(stmt));
    }

    sqlite3_finalize(stmt);
    return pairs;
}

std::vector<QASource::QAPair> SQLiteSource::searchByQuestion(const std::string& query) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<QASource::QAPair> pairs;

#if !QORNIX_HAS_SQLITE
    return pairs;
#endif

    if (!db_) {
        return pairs;
    }

    // Use SQLite FTS-like search with LIKE
    std::string sql = "SELECT id, question, answer, category, aliases, metadata "
                      "FROM qa_pairs WHERE source_id = ? AND "
                      "(question LIKE ? OR answer LIKE ?) "
                      "ORDER BY updated_at DESC LIMIT 50";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return pairs;
    }

    std::string like_query = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, config_.source_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, like_query.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, like_query.c_str(), -1, SQLITE_STATIC);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        pairs.push_back(rowToQAPair(stmt));
    }

    sqlite3_finalize(stmt);
    return pairs;
}

std::vector<QASource::QAPair> SQLiteSource::getAllPairs(size_t page, size_t per_page) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<QASource::QAPair> pairs;

#if !QORNIX_HAS_SQLITE
    return pairs;
#endif

    if (!db_) {
        return pairs;
    }

    int offset = (page - 1) * per_page;

    std::string sql = "SELECT id, question, answer, category, aliases, metadata "
                      "FROM qa_pairs WHERE source_id = ? "
                      "ORDER BY updated_at DESC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return pairs;
    }

    sqlite3_bind_text(stmt, 1, config_.source_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, static_cast<int>(per_page));
    sqlite3_bind_int(stmt, 3, offset);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        pairs.push_back(rowToQAPair(stmt));
    }

    sqlite3_finalize(stmt);
    return pairs;
}

// ============================================================================
// Internal helpers
// ============================================================================

bool SQLiteSource::executeStatement(const std::string& sql) {
#if !QORNIX_HAS_SQLITE
    return false;
#endif

    if (!db_) {
        return false;
    }

    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);

    if (rc != SQLITE_OK) {
        std::cerr << "SQLiteSource: Statement failed: "
                  << (error_msg ? error_msg : "unknown error") << std::endl;
        if (error_msg) {
            sqlite3_free(error_msg);
        }
        return false;
    }

    return true;
}

bool SQLiteSource::executePreparedStatement(const std::string& sql,
                                              const std::vector<std::string>& bind_values) {
#if !QORNIX_HAS_SQLITE
    return false;
#endif

    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQLiteSource: Prepare failed: "
                  << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    // Bind values
    for (size_t i = 0; i < bind_values.size() && i < sqlite3_bind_parameter_count(stmt); ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1),
                          bind_values[i].c_str(), -1, SQLITE_STATIC);
    }

    rc = sqlite3_step(stmt);
    bool result = (rc == SQLITE_OK || rc == SQLITE_DONE);

    if (!result) {
        std::cerr << "SQLiteSource: Execute failed: "
                  << sqlite3_errmsg(db_) << std::endl;
    }

    sqlite3_finalize(stmt);
    return result;
}

std::string SQLiteSource::computeHash(const std::string& id, const std::string& question,
                                       const std::string& answer) const {
    (void)id;
    std::string combined = question + "|" + answer;
    return HashCalculator::compute_md5(combined);
}

QASource::QAPair SQLiteSource::rowToQAPair(sqlite3_stmt* stmt) const {
    QASource::QAPair pair;

    pair.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    pair.question = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    pair.answer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    pair.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

    // Parse aliases JSON array
    const char* aliases_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    if (aliases_json) {
        // Simple JSON array parsing (extract strings between quotes)
        std::string aliases_str(aliases_json);
        // TODO: Use proper JSON parser when available
        // For now, store as empty vector
        pair.aliases = {};
    }

    // Parse metadata JSON
    const char* metadata_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    if (metadata_json) {
        std::string metadata_str(metadata_json);
        // TODO: Use proper JSON parser when available
    }

    return pair;
}

Document SQLiteSource::qaPairToDocument(const QASource::QAPair& pair) const {
    Document doc;
    doc.path = "sqlite://" + pair.id;
    doc.relative_path = "sqlite://" + pair.id;
    doc.content = "Вопрос: " + pair.question + "\n\nОтвет: " + pair.answer;
    doc.type = "qa_pair";
    doc.language = "text";
    doc.size_bytes = doc.content.size();
    doc.lines_count = 3;
    doc.hash = computeHash(pair.id, pair.question, pair.answer);
    doc.last_modified = std::chrono::system_clock::now();

    // Store metadata
    doc.metadata["qa_id"] = pair.id;
    doc.metadata["category"] = pair.category;

    return doc;
}
