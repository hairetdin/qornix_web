#include "sqlite_source.h"
#include "core.h"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <boost/json.hpp>

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

        -- Persistent indexed document metadata.
        CREATE TABLE IF NOT EXISTS rag_documents (
            id TEXT PRIMARY KEY,
            source_id TEXT NOT NULL,
            path TEXT NOT NULL,
            relative_path TEXT NOT NULL,
            type TEXT,
            language TEXT,
            content_hash TEXT NOT NULL,
            size_bytes INTEGER DEFAULT 0,
            lines_count INTEGER DEFAULT 0,
            metadata TEXT DEFAULT '{}',
            last_modified INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(source_id, relative_path)
        );

        -- Persistent chunks derived from indexed documents.
        CREATE TABLE IF NOT EXISTS rag_chunks (
            id TEXT PRIMARY KEY,
            document_id TEXT NOT NULL,
            source_id TEXT NOT NULL,
            chunk_index INTEGER NOT NULL,
            content TEXT NOT NULL,
            content_hash TEXT NOT NULL,
            char_start INTEGER DEFAULT 0,
            char_end INTEGER DEFAULT 0,
            token_count INTEGER DEFAULT 0,
            metadata TEXT DEFAULT '{}',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY(document_id) REFERENCES rag_documents(id) ON DELETE CASCADE,
            UNIQUE(document_id, chunk_index)
        );

        -- Persistent embedding vectors for chunks.
        CREATE TABLE IF NOT EXISTS rag_embeddings (
            chunk_id TEXT NOT NULL,
            model_id TEXT NOT NULL,
            source_id TEXT NOT NULL,
            backend TEXT NOT NULL,
            dimension INTEGER NOT NULL,
            vector BLOB NOT NULL,
            content_hash TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY(chunk_id, model_id),
            FOREIGN KEY(chunk_id) REFERENCES rag_chunks(id) ON DELETE CASCADE
        );

        -- Embedding model metadata. Kept minimal until the model registry expands.
        CREATE TABLE IF NOT EXISTS rag_embedding_models (
            model_id TEXT PRIMARY KEY,
            backend TEXT NOT NULL,
            dimension INTEGER NOT NULL,
            metadata TEXT DEFAULT '{}',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        -- Durable ingestion job history for filesystem/project ingestion.
        CREATE TABLE IF NOT EXISTS rag_ingestion_jobs (
            id TEXT PRIMARY KEY,
            source_id TEXT NOT NULL,
            root_path TEXT NOT NULL,
            status TEXT NOT NULL,
            files_seen INTEGER DEFAULT 0,
            documents_imported INTEGER DEFAULT 0,
            duplicates_found INTEGER DEFAULT 0,
            skipped INTEGER DEFAULT 0,
            errors INTEGER DEFAULT 0,
            error_message TEXT DEFAULT '',
            started_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            finished_at TIMESTAMP
        );

        -- Indexes for performance
        CREATE INDEX IF NOT EXISTS idx_qa_pairs_source ON qa_pairs(source_id);
        CREATE INDEX IF NOT EXISTS idx_qa_pairs_category ON qa_pairs(category);
        CREATE INDEX IF NOT EXISTS idx_qa_pairs_hash ON qa_pairs(hash);
        CREATE INDEX IF NOT EXISTS idx_search_log_timestamp ON search_log(timestamp);
        CREATE INDEX IF NOT EXISTS idx_search_log_query ON search_log(query);
        CREATE INDEX IF NOT EXISTS idx_import_history_file ON import_history(file_path);
        CREATE INDEX IF NOT EXISTS idx_rag_documents_source ON rag_documents(source_id);
        CREATE INDEX IF NOT EXISTS idx_rag_documents_path ON rag_documents(source_id, relative_path);
        CREATE INDEX IF NOT EXISTS idx_rag_chunks_document ON rag_chunks(document_id);
        CREATE INDEX IF NOT EXISTS idx_rag_chunks_source ON rag_chunks(source_id);
        CREATE INDEX IF NOT EXISTS idx_rag_embeddings_model ON rag_embeddings(model_id);
        CREATE INDEX IF NOT EXISTS idx_rag_ingestion_jobs_started ON rag_ingestion_jobs(started_at);
        CREATE INDEX IF NOT EXISTS idx_rag_ingestion_jobs_source ON rag_ingestion_jobs(source_id);
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
                                 const std::string& category, const std::string& aliases,
                                 const std::string& question) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return false;
#endif

    if (!db_) {
        return false;
    }

    std::string current_question;
    std::string current_answer;
    {
        std::string select_sql = "SELECT question, answer FROM qa_pairs WHERE id = ? AND source_id = ?";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, select_sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, config_.source_id.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return false;
        }

        const unsigned char* q = sqlite3_column_text(stmt, 0);
        const unsigned char* a = sqlite3_column_text(stmt, 1);
        current_question = q ? reinterpret_cast<const char*>(q) : "";
        current_answer = a ? reinterpret_cast<const char*>(a) : "";
        sqlite3_finalize(stmt);
    }

    std::string update_sql = "UPDATE qa_pairs SET ";
    std::vector<std::string> set_clauses;
    std::vector<std::string> bind_values;

    if (!question.empty()) {
        set_clauses.push_back("question = ?");
        bind_values.push_back(question);
    }
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

    if (!question.empty() || !answer.empty()) {
        const std::string final_question = question.empty() ? current_question : question;
        const std::string final_answer = answer.empty() ? current_answer : answer;
        set_clauses.push_back("hash = ?");
        bind_values.push_back(computeHash(id, final_question, final_answer));
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

    std::vector<std::string> terms;
    std::string current;
    for (unsigned char ch : query) {
        if (std::isalnum(ch) || ch == '_' || ch >= 0x80) {
            current.push_back(static_cast<char>(ch));
        } else if (!current.empty()) {
            if (current.size() >= 3) {
                terms.push_back(current);
            }
            current.clear();
        }
    }
    if (!current.empty() && current.size() >= 3) {
        terms.push_back(current);
    }

    if (terms.empty() && !query.empty()) {
        terms.push_back(query);
    }
    if (terms.size() > 8) {
        terms.resize(8);
    }

    std::string sql = "SELECT id, question, answer, category, aliases, metadata "
                      "FROM qa_pairs WHERE source_id = ?";

    if (!terms.empty()) {
        sql += " AND (";
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i > 0) {
                sql += " OR ";
            }
            sql += "question LIKE ? OR answer LIKE ? OR category LIKE ?";
        }
        sql += ")";
    }
    sql += " ORDER BY updated_at DESC LIMIT 50";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return pairs;
    }

    int bind_index = 1;
    sqlite3_bind_text(stmt, bind_index++, config_.source_id.c_str(), -1, SQLITE_STATIC);
    for (const auto& term : terms) {
        std::string like_query = "%" + term + "%";
        sqlite3_bind_text(stmt, bind_index++, like_query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bind_index++, like_query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bind_index++, like_query.c_str(), -1, SQLITE_TRANSIENT);
    }

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

PersistedIndexStats SQLiteSource::persistIndexedDocuments(
    const std::vector<Document>& documents,
    const std::string& source_id,
    const std::string& embedding_model_id,
    const std::string& embedding_backend) {
    std::lock_guard<std::mutex> lock(mutex_);

    PersistedIndexStats stats;

#if !QORNIX_HAS_SQLITE
    return stats;
#endif

    if (!db_) {
        return stats;
    }

    const std::string effective_source = source_id.empty() ? config_.source_id : source_id;
    const std::string effective_backend = embedding_backend.empty() ? "unknown" : embedding_backend;

    if (!executeStatement("BEGIN IMMEDIATE TRANSACTION")) {
        return stats;
    }

    bool ok = true;
    auto fail = [&]() {
        ok = false;
        executeStatement("ROLLBACK");
    };

    if (!executeStatement("CREATE TEMP TABLE IF NOT EXISTS rag_current_snapshot_ids (id TEXT PRIMARY KEY)")) {
        fail();
        return stats;
    }
    if (!executeStatement("DELETE FROM rag_current_snapshot_ids")) {
        fail();
        return stats;
    }

    for (const auto& doc : documents) {
        if (!ok) {
            break;
        }

        const std::string relative_path = doc.relative_path.empty() ? doc.path : doc.relative_path;
        const std::string document_id = computeDocumentId(effective_source, relative_path);
        const std::string chunk_id = document_id + "#0";
        const std::string content_hash = doc.hash.empty() ? HashCalculator::compute_md5(doc.content) : doc.hash;
        const std::string metadata_json = metadataToJson(doc.metadata);
        const auto modified_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            doc.last_modified.time_since_epoch()
        ).count();

        {
            sqlite3_stmt* stmt = nullptr;
            const std::string sql = "INSERT OR IGNORE INTO rag_current_snapshot_ids (id) VALUES (?)";
            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail();
                break;
            }
            bindText(stmt, 1, document_id);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                fail();
                break;
            }
            sqlite3_finalize(stmt);
        }

        {
            sqlite3_stmt* stmt = nullptr;
            const std::string sql =
                "INSERT INTO rag_documents "
                "(id, source_id, path, relative_path, type, language, content_hash, "
                "size_bytes, lines_count, metadata, last_modified, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
                "ON CONFLICT(id) DO UPDATE SET "
                "path = excluded.path, relative_path = excluded.relative_path, "
                "type = excluded.type, language = excluded.language, "
                "content_hash = excluded.content_hash, size_bytes = excluded.size_bytes, "
                "lines_count = excluded.lines_count, metadata = excluded.metadata, "
                "last_modified = excluded.last_modified, updated_at = CURRENT_TIMESTAMP";

            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail();
                break;
            }

            bindText(stmt, 1, document_id);
            bindText(stmt, 2, effective_source);
            bindText(stmt, 3, doc.path);
            bindText(stmt, 4, relative_path);
            bindText(stmt, 5, doc.type);
            bindText(stmt, 6, doc.language);
            bindText(stmt, 7, content_hash);
            bindInt64(stmt, 8, static_cast<std::int64_t>(doc.size_bytes));
            bindInt64(stmt, 9, static_cast<std::int64_t>(doc.lines_count));
            bindText(stmt, 10, metadata_json);
            bindInt64(stmt, 11, static_cast<std::int64_t>(modified_seconds));

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                fail();
                break;
            }
            sqlite3_finalize(stmt);
            stats.documents++;
        }

        {
            sqlite3_stmt* stmt = nullptr;
            const std::string sql =
                "INSERT INTO rag_chunks "
                "(id, document_id, source_id, chunk_index, content, content_hash, "
                "char_start, char_end, token_count, metadata, updated_at) "
                "VALUES (?, ?, ?, 0, ?, ?, 0, ?, ?, ?, CURRENT_TIMESTAMP) "
                "ON CONFLICT(id) DO UPDATE SET "
                "content = excluded.content, content_hash = excluded.content_hash, "
                "char_start = excluded.char_start, char_end = excluded.char_end, "
                "token_count = excluded.token_count, metadata = excluded.metadata, "
                "updated_at = CURRENT_TIMESTAMP";

            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail();
                break;
            }

            bindText(stmt, 1, chunk_id);
            bindText(stmt, 2, document_id);
            bindText(stmt, 3, effective_source);
            bindText(stmt, 4, doc.content);
            bindText(stmt, 5, content_hash);
            bindInt64(stmt, 6, static_cast<std::int64_t>(doc.content.size()));
            bindInt64(stmt, 7, static_cast<std::int64_t>(doc.content.empty() ? 0 : doc.content.size()));
            bindText(stmt, 8, metadata_json);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                fail();
                break;
            }
            sqlite3_finalize(stmt);
            stats.chunks++;
        }

        if (!doc.embedding.empty()) {
            const std::string model_id = embedding_model_id.empty()
                ? effective_backend + ":" + std::to_string(doc.embedding.size())
                : embedding_model_id;

            {
                sqlite3_stmt* stmt = nullptr;
                const std::string sql =
                    "INSERT INTO rag_embedding_models "
                    "(model_id, backend, dimension, metadata, updated_at) "
                    "VALUES (?, ?, ?, '{}', CURRENT_TIMESTAMP) "
                    "ON CONFLICT(model_id) DO UPDATE SET "
                    "backend = excluded.backend, dimension = excluded.dimension, updated_at = CURRENT_TIMESTAMP";

                if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                    fail();
                    break;
                }
                bindText(stmt, 1, model_id);
                bindText(stmt, 2, effective_backend);
                bindInt64(stmt, 3, static_cast<std::int64_t>(doc.embedding.size()));
                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    sqlite3_finalize(stmt);
                    fail();
                    break;
                }
                sqlite3_finalize(stmt);
            }

            sqlite3_stmt* stmt = nullptr;
            const std::string sql =
                "INSERT INTO rag_embeddings "
                "(chunk_id, model_id, source_id, backend, dimension, vector, content_hash, created_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
                "ON CONFLICT(chunk_id, model_id) DO UPDATE SET "
                "source_id = excluded.source_id, backend = excluded.backend, dimension = excluded.dimension, "
                "vector = excluded.vector, content_hash = excluded.content_hash, "
                "created_at = CURRENT_TIMESTAMP";

            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                fail();
                break;
            }
            bindText(stmt, 1, chunk_id);
            bindText(stmt, 2, model_id);
            bindText(stmt, 3, effective_source);
            bindText(stmt, 4, effective_backend);
            bindInt64(stmt, 5, static_cast<std::int64_t>(doc.embedding.size()));
            bindFloatVector(stmt, 6, doc.embedding);
            bindText(stmt, 7, content_hash);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                fail();
                break;
            }
            sqlite3_finalize(stmt);
            stats.embeddings++;
        }
    }

    if (!ok) {
        return {};
    }

    {
        sqlite3_stmt* stmt = nullptr;
        const std::string sql =
            "DELETE FROM rag_documents "
            "WHERE source_id = ? "
            "AND id NOT IN (SELECT id FROM rag_current_snapshot_ids)";
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            fail();
            return {};
        }
        bindText(stmt, 1, effective_source);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            fail();
            return {};
        }
        stats.deleted_documents = static_cast<size_t>(std::max(0, sqlite3_changes(db_)));
        sqlite3_finalize(stmt);
    }

    if (!executeStatement("COMMIT")) {
        executeStatement("ROLLBACK");
        return {};
    }

    return stats;
}

size_t SQLiteSource::countPersistedDocuments(const std::string& source_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return countTableRows("rag_documents", source_id.empty() ? config_.source_id : source_id);
}

size_t SQLiteSource::countPersistedChunks(const std::string& source_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return countTableRows("rag_chunks", source_id.empty() ? config_.source_id : source_id);
}

size_t SQLiteSource::countPersistedEmbeddings(const std::string& source_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return countTableRows("rag_embeddings", source_id.empty() ? config_.source_id : source_id);
}

std::optional<Document> SQLiteSource::findPersistedDocument(const std::string& relative_path,
                                                            const std::string& source_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    return std::nullopt;
#endif

    if (!db_) {
        return std::nullopt;
    }

    const std::string effective_source = source_id.empty() ? config_.source_id : source_id;
    const std::string sql =
        "SELECT path, relative_path, type, language, content_hash, size_bytes, "
        "lines_count, metadata, last_modified "
        "FROM rag_documents WHERE source_id = ? AND relative_path = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    bindText(stmt, 1, effective_source);
    bindText(stmt, 2, relative_path);

    std::optional<Document> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Document doc;
        const unsigned char* path = sqlite3_column_text(stmt, 0);
        const unsigned char* rel = sqlite3_column_text(stmt, 1);
        const unsigned char* type = sqlite3_column_text(stmt, 2);
        const unsigned char* language = sqlite3_column_text(stmt, 3);
        const unsigned char* hash = sqlite3_column_text(stmt, 4);
        const unsigned char* metadata = sqlite3_column_text(stmt, 7);

        doc.path = path ? reinterpret_cast<const char*>(path) : "";
        doc.relative_path = rel ? reinterpret_cast<const char*>(rel) : "";
        doc.type = type ? reinterpret_cast<const char*>(type) : "";
        doc.language = language ? reinterpret_cast<const char*>(language) : "";
        doc.hash = hash ? reinterpret_cast<const char*>(hash) : "";
        doc.size_bytes = static_cast<size_t>(sqlite3_column_int64(stmt, 5));
        doc.lines_count = static_cast<size_t>(sqlite3_column_int64(stmt, 6));
        doc.metadata = metadataFromJson(metadata ? reinterpret_cast<const char*>(metadata) : "{}");
        const auto modified = sqlite3_column_int64(stmt, 8);
        doc.last_modified = std::chrono::system_clock::time_point(std::chrono::seconds(modified));
        result = std::move(doc);
    }

    sqlite3_finalize(stmt);
    return result;
}

bool SQLiteSource::deletePersistedDocument(const std::string& relative_path,
                                           const std::string& source_id) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    (void)relative_path;
    (void)source_id;
    return false;
#endif

    if (!db_ || relative_path.empty()) {
        return false;
    }

    const std::string effective_source = source_id.empty() ? config_.source_id : source_id;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "DELETE FROM rag_documents WHERE source_id = ? AND relative_path = ?";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bindText(stmt, 1, effective_source);
    bindText(stmt, 2, relative_path);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    const int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return ok && changes > 0;
}

bool SQLiteSource::recordIngestionJobStarted(const std::string& job_id,
                                             const std::string& source_id,
                                             const std::string& root_path) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    (void)job_id;
    (void)source_id;
    (void)root_path;
    return false;
#endif

    if (!db_ || job_id.empty()) {
        return false;
    }

    const std::string effective_source = source_id.empty() ? config_.source_id : source_id;
    const std::string sql =
        "INSERT INTO rag_ingestion_jobs "
        "(id, source_id, root_path, status, started_at) "
        "VALUES (?, ?, ?, 'running', CURRENT_TIMESTAMP) "
        "ON CONFLICT(id) DO UPDATE SET "
        "source_id = excluded.source_id, root_path = excluded.root_path, "
        "status = 'running', files_seen = 0, documents_imported = 0, "
        "duplicates_found = 0, skipped = 0, errors = 0, error_message = '', "
        "started_at = CURRENT_TIMESTAMP, finished_at = NULL";
    return executePreparedStatement(sql, {job_id, effective_source, root_path});
}

bool SQLiteSource::recordIngestionJobFinished(const std::string& job_id,
                                              const std::string& status,
                                              const qornix::rag::IngestionJobResult& result,
                                              const std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    (void)job_id;
    (void)status;
    (void)result;
    (void)error_message;
    return false;
#endif

    if (!db_ || job_id.empty()) {
        return false;
    }

    const std::string sql =
        "UPDATE rag_ingestion_jobs SET "
        "status = ?, files_seen = ?, documents_imported = ?, duplicates_found = ?, "
        "skipped = ?, errors = ?, error_message = ?, finished_at = CURRENT_TIMESTAMP "
        "WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bindText(stmt, 1, status);
    bindInt64(stmt, 2, static_cast<std::int64_t>(result.files_seen));
    bindInt64(stmt, 3, static_cast<std::int64_t>(result.documents_imported));
    bindInt64(stmt, 4, static_cast<std::int64_t>(result.duplicates_found));
    bindInt64(stmt, 5, static_cast<std::int64_t>(result.skipped));
    bindInt64(stmt, 6, static_cast<std::int64_t>(result.errors));
    bindText(stmt, 7, error_message);
    bindText(stmt, 8, job_id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<SQLiteSource::IngestionJobRecord> SQLiteSource::findIngestionJob(
    const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

#if !QORNIX_HAS_SQLITE
    (void)job_id;
    return std::nullopt;
#endif

    if (!db_ || job_id.empty()) {
        return std::nullopt;
    }

    const std::string sql =
        "SELECT id, source_id, root_path, status, files_seen, documents_imported, "
        "duplicates_found, skipped, errors, error_message, started_at, finished_at "
        "FROM rag_ingestion_jobs WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    bindText(stmt, 1, job_id);

    std::optional<IngestionJobRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToIngestionJob(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<SQLiteSource::IngestionJobRecord> SQLiteSource::listIngestionJobs(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<IngestionJobRecord> jobs;

#if !QORNIX_HAS_SQLITE
    (void)limit;
    return jobs;
#endif

    if (!db_) {
        return jobs;
    }

    const std::string sql =
        "SELECT id, source_id, root_path, status, files_seen, documents_imported, "
        "duplicates_found, skipped, errors, error_message, started_at, finished_at "
        "FROM rag_ingestion_jobs ORDER BY started_at DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return jobs;
    }
    bindInt64(stmt, 1, static_cast<std::int64_t>(limit == 0 ? 20 : limit));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        jobs.push_back(rowToIngestionJob(stmt));
    }
    sqlite3_finalize(stmt);
    return jobs;
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
    const auto parameter_count = static_cast<size_t>(sqlite3_bind_parameter_count(stmt));
    for (size_t i = 0; i < bind_values.size() && i < parameter_count; ++i) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1),
                          bind_values[i].c_str(), -1, SQLITE_TRANSIENT);
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

std::string SQLiteSource::computeDocumentId(const std::string& source_id,
                                            const std::string& relative_path) const {
    return source_id + ":" + HashCalculator::compute_md5(relative_path);
}

std::string SQLiteSource::metadataToJson(const std::map<std::string, std::string>& metadata) const {
    boost::json::object object;
    for (const auto& [key, value] : metadata) {
        object[key] = value;
    }
    return boost::json::serialize(object);
}

std::map<std::string, std::string> SQLiteSource::metadataFromJson(const std::string& json) const {
    std::map<std::string, std::string> metadata;
    if (json.empty()) {
        return metadata;
    }

    try {
        auto parsed = boost::json::parse(json);
        if (!parsed.is_object()) {
            return metadata;
        }
        for (const auto& entry : parsed.as_object()) {
            if (entry.value().is_string()) {
                metadata[std::string(entry.key())] = entry.value().as_string().c_str();
            }
        }
    } catch (...) {
        return {};
    }

    return metadata;
}

bool SQLiteSource::bindText(sqlite3_stmt* stmt, int index, const std::string& value) const {
#if !QORNIX_HAS_SQLITE
    (void)stmt;
    (void)index;
    (void)value;
    return false;
#else
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
#endif
}

bool SQLiteSource::bindInt64(sqlite3_stmt* stmt, int index, std::int64_t value) const {
#if !QORNIX_HAS_SQLITE
    (void)stmt;
    (void)index;
    (void)value;
    return false;
#else
    return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
#endif
}

bool SQLiteSource::bindFloatVector(sqlite3_stmt* stmt, int index, const std::vector<float>& values) const {
#if !QORNIX_HAS_SQLITE
    (void)stmt;
    (void)index;
    (void)values;
    return false;
#else
    if (values.empty()) {
        return sqlite3_bind_blob(stmt, index, "", 0, SQLITE_STATIC) == SQLITE_OK;
    }
    const auto byte_size = static_cast<int>(values.size() * sizeof(float));
    return sqlite3_bind_blob(stmt, index, values.data(), byte_size, SQLITE_TRANSIENT) == SQLITE_OK;
#endif
}

size_t SQLiteSource::countTableRows(const std::string& table, const std::string& source_id) const {
#if !QORNIX_HAS_SQLITE
    (void)table;
    (void)source_id;
    return 0;
#endif

    if (!db_) {
        return 0;
    }

    if (table != "rag_documents" && table != "rag_chunks" && table != "rag_embeddings") {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM " + table;
    if (!source_id.empty()) {
        sql += " WHERE source_id = ?";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    if (!source_id.empty()) {
        bindText(stmt, 1, source_id);
    }

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return count;
}

std::string SQLiteSource::computeHash(const std::string& id, const std::string& question,
                                       const std::string& answer) const {
    (void)id;
    std::string combined = question + "|" + answer;
    return HashCalculator::compute_md5(combined);
}

SQLiteSource::IngestionJobRecord SQLiteSource::rowToIngestionJob(sqlite3_stmt* stmt) const {
    IngestionJobRecord job;
    const auto text = [&](int column) -> std::string {
        const unsigned char* value = sqlite3_column_text(stmt, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    };

    job.id = text(0);
    job.source_id = text(1);
    job.root_path = text(2);
    job.status = text(3);
    job.files_seen = static_cast<size_t>(sqlite3_column_int64(stmt, 4));
    job.documents_imported = static_cast<size_t>(sqlite3_column_int64(stmt, 5));
    job.duplicates_found = static_cast<size_t>(sqlite3_column_int64(stmt, 6));
    job.skipped = static_cast<size_t>(sqlite3_column_int64(stmt, 7));
    job.errors = static_cast<size_t>(sqlite3_column_int64(stmt, 8));
    job.error_message = text(9);
    job.started_at = text(10);
    job.finished_at = text(11);
    return job;
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
