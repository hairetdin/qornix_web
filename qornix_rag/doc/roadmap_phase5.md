# Фаза 5: Персистентность, Аналитика и Импорт — Universal RAG-модуль

> **Версия:** 1.0
> **Дата:** 2026-03-18
> **Статус:** ✅ ЗАВЕРШЕНО

---

## 1. Обзор

### Цель

Расширить универсальный RAG-модуль Фазы 4 ключевыми функциями для production-использования:
- **Персистентность** — SQLite для хранения QA-пар и метаданных
- **Импорт** — загрузка знаний из Markdown файлов
- **Дедупликация** — автоматическое обнаружение дубликатов QA-пар
- **Аналитика** — мониторинг поисковых запросов и выявление "missing answers"

### Ключевые принципы

1. **Zero-downtime** — все изменения работают без перезапуска
2. **Backward compatible** — существующие API endpoints не меняются
3. **SQLite-first** — лёгкая база данных без внешних зависимостей
4. **Semantic dedup** — обнаружение дубликатов по семантической схожести
5. **Actionable analytics** — метрики, которые приводят к действиям

---

## 2. Что было (Фаза 4)

| Компонент | Статус | Описание |
|-----------|--------|----------|
| `DataSource` interface | ✅ | Абстракция источников данных |
| `FileSource` | ✅ | Сканирование файловой системы |
| `QASource` | ✅ | База знаний QA-парами (in-memory) |
| `TextSource` | ✅ | Текстовые документы |
| `MemorySource` | ✅ | Программная загрузка |
| `RagEngine` | ✅ | Гибридный поиск (HNSW + Xapian) |
| `LLMClient` | ✅ | OpenAI-compatible API |
| `MemoryCache` | ✅ | LRU кэш ответов |
| `RateLimiter` | ✅ | Sliding window rate limiting |
| `BatchProcessor` | ✅ | Concurrent batch processing |
| `PrometheusMetrics` | ✅ | Metrics registry |
| API endpoints | ✅ | 15+ endpoints для управления |

**Ограничения Фазы 4:**
- ❌ QA-пары хранятся только в памяти (теряются при перезапуске)
- ❌ Нет импорта из внешних файлов
- ❌ Нет обнаружения дубликатов
- ❌ Нет аналитики поисковых запросов
- ❌ Нет отслеживания "missing answers"

---

## 3. Архитектура Фазы 5

### 3.1. Общая архитектура

```
┌─────────────────────────────────────────────────────────────────────┐
│                    qornix_web Application                           │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                  qornix_rag_lib (static library)             │   │
│  │                                                              │   │
│  │  ┌──────────────┐    ┌──────────────────┐                   │   │
│  │  │   RagEngine  │←───│ DataSource       │                   │   │
│  │  │              │    │ Interface        │                   │   │
│  │  │ - Hybrid     │    │ ├─ FileSource    │                   │   │
│  │  │   Search     │    │ ├─ QASource      │                   │   │
│  │  │ - Embeddings │    │ ├─ SQLiteSource  │ ←── НОВОЕ        │   │
│  │  │ - Indexing   │    │ ├─ MarkdownSource│ ←── НОВОЕ        │   │
│  │  │              │    │ └─ MemorySource  │                   │   │
│  │  └──────────────┘    └──────────────────┘                   │   │
│  │                                                              │   │
│  │  ┌──────────────┐    ┌──────────────────┐    ┌────────────┐ │   │
│  │  │   LLMClient  │    │   Cache Layer    │    │ Analytics  │ ←── НОВОЕ │
│  │  │              │    │                  │    │            │ │   │
│  │  │ - ask()      │    │ - MemoryCache    │    │ - SearchLog│ │   │
│  │  │ - stream()   │    │ - PromptCache    │    │ - Missing  │ │   │
│  │  │ - health()   │    │ - ResponseCache  │    │ - Reports  │ │   │
│  │  └──────────────┘    └──────────────────┘    └────────────┘ │   │
│  │                                                              │   │
│  │  ┌──────────────────────────────────────────────────────┐   │   │
│  │  │           DeduplicationService                       │   │   │
│  │  │  - Semantic similarity check                        │   │   │
│  │  │  - Threshold-based dedup                            │   │   │
│  │  │  - Duplicate detection API                          │   │   │
│  │  └──────────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              ▲                                      │
│                              │                                      │
│  ┌─────────────────────────┴────────────────────────┐               │
│  │              SQLite Database                     │               │
│  │  - qa_pairs table                                │               │
│  │  - search_log table                              │               │
│  │  - import_history table                          │               │
│  └──────────────────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2. SQLite Schema

```sql
-- QA Pairs (persistent storage)
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

-- Search Log (analytics)
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

-- Import History
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
CREATE INDEX IF NOT EXISTS idx_search_log_timestamp ON search_log(timestamp);
CREATE INDEX IF NOT EXISTS idx_search_log_query ON search_log(query);
```

---

## 4. План реализации

### Этап 1: SQLiteDataSource (2-3 дня)

#### 5.1.1. Создать SQLiteDataSource

**Новый файл:** `qornix_rag/sqlite_source.h`

```cpp
#pragma once
#include "data_source.h"

/**
 * SQLite-backed data source for persistent QA pairs storage.
 * All QA pairs are stored in SQLite database and survive restarts.
 */
class SQLiteSource : public DataSource {
public:
    struct Config {
        std::string db_path;                      // Path to SQLite database
        std::string source_id;                    // Source identifier
        std::string name;                         // Source name
        bool auto_migrate = true;                 // Auto-create tables
    };

    explicit SQLiteSource(Config config);
    ~SQLiteSource() override;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::DATABASE; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;

    // QA-specific API (extends QASource)
    bool addQAPair(const std::string& id, const std::string& question,
                   const std::string& answer, const std::string& category = "general",
                   const std::string& aliases = "{}", const std::string& metadata = "{}");
    bool updateQAPair(const std::string& id, const std::string& answer,
                      const std::string& category = "", const std::string& aliases = "");
    bool deleteQAPair(const std::string& id);
    std::optional<QAPair> findQAPair(const std::string& id) const;
    std::vector<QAPair> searchByCategory(const std::string& category) const;
    std::vector<QAPair> searchByQuestion(const std::string& query) const;
    std::vector<QAPair> getAllPairs(size_t page = 1, size_t per_page = 20) const;

    // Migration
    bool migrate();

private:
    Config config_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;

    // Internal helpers
    bool executeStatement(const std::string& sql);
    std::string computeHash(const std::string& id, const std::string& question, const std::string& answer) const;
    QAPair documentToQAPair(const Document& doc) const;
    Document qaPairToDocument(const QAPair& pair) const;
};
```

#### 5.1.2. Реализация SQLiteSource

**Новый файл:** `qornix_rag/sqlite_source.cpp`

Реализация включает:
- Подключение к SQLite базе данных
- Создание таблиц (auto-migrate)
- CRUD операции для QA-пар
- Конвертация QA-пар в Document и обратно
- Thread-safe операции с mutex

#### 5.1.3. Обновить CMakeLists.txt

Добавить SQLite3 как зависимость:

```cmake
# Find SQLite3
find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIBRARY sqlite3)

if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
    set(QORNIX_HAS_SQLITE ON)
    message(STATUS "SQLite3 found: ${SQLITE3_LIBRARY}")
else()
    set(QORNIX_HAS_SQLITE OFF)
    message(WARNING "SQLite3 not found - SQLiteDataSource will be disabled")
endif()
```

#### 5.1.4. Обновить RagExtension

Добавить поддержку SQLiteSource в RagExtension:

```cpp
// In rag_extension.h
#if QORNIX_HAS_SQLITE
#include "sqlite_source.h"
#endif

// In configure():
#if QORNIX_HAS_SQLITE
if (config.count("sqlite.db_path")) {
    SQLiteSource::Config sqlite_config;
    sqlite_config.db_path = config["sqlite.db_path"];
    sqlite_config.source_id = "sqlite_kb";
    sqlite_source_ = std::make_shared<SQLiteSource>(sqlite_config);
    rag_engine_->addDataSource(sqlite_source_);
}
#endif
```

---

### Этап 2: MarkdownImportSource (1-2 дня)

#### 5.2.1. Создать MarkdownImportSource

**Новый файл:** `qornix_rag/markdown_source.h`

```cpp
#pragma once
#include "data_source.h"

/**
 * Markdown file importer for knowledge base.
 * Parses Markdown files with specific frontmatter format and imports as documents.
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

    explicit MarkdownSource(Config config);
    ~MarkdownSource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::TEXT_DOCS; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;

    // Import API
    bool importFile(const std::string& file_path);
    bool importDirectory(const std::string& dir_path);
    std::vector<std::string> getImportedFiles() const;

private:
    Config config_;
    std::vector<std::string> imported_files_;
    std::mutex mutex_;

    // Internal helpers
    bool parseFrontmatter(const std::string& content, std::map<std::string, std::string>& frontmatter, std::string& body);
    std::string extractBody(const std::string& content);
    std::string generateId(const std::string& file_path, const std::string& title);
};
```

#### 5.2.2. Реализация парсера Markdown

**Новый файл:** `qornix_rag/markdown_parser.h` / `.cpp`

```cpp
#pragma once
#include <string>
#include <map>
#include <vector>

/**
 * Simple Markdown parser for knowledge base imports.
 * Supports YAML frontmatter and extracts question/answer pairs.
 */
class MarkdownParser {
public:
    struct ParsedDocument {
        std::string title;
        std::string category;
        std::vector<std::string> aliases;
        std::string content;
        std::map<std::string, std::string> metadata;
    };

    /**
     * Parse a Markdown file with frontmatter.
     * Returns ParsedDocument or nullopt if parsing fails.
     */
    static std::optional<ParsedDocument> parse(const std::string& file_path);

    /**
     * Convert a ParsedDocument to a RAG Document.
     */
    static Document toDocument(const ParsedDocument& parsed, const std::string& source_id);

private:
    static bool parseYamlFrontmatter(const std::string& content, std::map<std::string, std::string>& frontmatter, std::string& body);
    static std::string trim(const std::string& str);
    static std::string readFile(const std::string& path);
};
```

---

### Этап 3: Semantic Deduplication (2 дня)

#### 5.3.1. Создать DeduplicationService

**Новый файл:** `qornix_rag/deduplication_service.h`

```cpp
#pragma once
#include "core.h"
#include <vector>
#include <utility>

/**
 * Service for detecting duplicate QA pairs using semantic similarity.
 */
class DeduplicationService {
public:
    struct DuplicatePair {
        std::string primary_id;       // Keep this one
        std::string duplicate_id;     // Remove this one
        float similarity;             // Cosine similarity (0-1)
        std::string reason;           // Human-readable explanation
    };

    struct DedupResult {
        size_t total_pairs;
        size_t duplicates_found;
        size_t duplicates_removed;
        std::vector<DuplicatePair> duplicates;
    };

    struct Config {
        float similarity_threshold = 0.85f;  // Similarity threshold for dedup
        bool auto_remove = false;             // Auto-remove duplicates
        std::vector<std::string> exclude_categories;  // Skip these categories
    };

    explicit DeduplicationService(Config config);

    /**
     * Find duplicate QA pairs in a collection.
     */
    DedupResult findDuplicates(
        const std::vector<QASource::QAPair>& pairs,
        RagEngine& engine
    );

    /**
     * Check if a new QA pair is a duplicate of existing ones.
     */
    std::optional<DuplicatePair> isDuplicate(
        const QASource::QAPair& new_pair,
        const std::vector<QASource::QAPair>& existing_pairs,
        RagEngine& engine
    );

    /**
     * Get the configuration.
     */
    Config getConfig() const { return config_; }
    void setConfig(Config config) { config_ = config; }

private:
    Config config_;

    /**
     * Compute cosine similarity between two vectors.
     */
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const;

    /**
     * Generate embedding for a question using RagEngine.
     */
    std::vector<float> getQuestionEmbedding(const std::string& question, RagEngine& engine);
};
```

#### 5.3.2. Интеграция с QASource

Добавить метод в QASource:

```cpp
// In qa_source.h
/**
 * Check for duplicates before adding a new QA pair.
 */
std::optional<DeduplicationService::DuplicatePair> checkDuplicates(
    const QAPair& new_pair,
    RagEngine& engine,
    const DeduplicationService::Config& dedup_config
);
```

---

### Этап 4: AnalyticsService (2 дня)

#### 5.4.1. Создать AnalyticsService

**Новый файл:** `qornix_rag/analytics_service.h`

```cpp
#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>

/**
 * Service for analyzing search queries and identifying knowledge gaps.
 */
class AnalyticsService {
public:
    struct SearchQuery {
        std::string query;
        std::chrono::system_clock::time_point timestamp;
        int result_count;
        bool has_answer;
        int response_time_ms;
        std::string client_ip;
        std::string top_result_path;
    };

    struct AnalyticsReport {
        // Search statistics
        size_t total_queries;
        size_t queries_with_results;
        size_t queries_without_results;
        float answer_rate_percent;
        float avg_response_time_ms;
        float median_response_time_ms;

        // Top queries (most frequent)
        std::vector<std::pair<std::string, size_t>> top_queries;

        // Missing answers (queries with no results)
        std::vector<std::string> missing_answers;

        // Category distribution
        std::map<std::string, size_t> category_distribution;

        // Daily trends (last 30 days)
        struct DailyTrend {
            std::string date;
            size_t queries;
            size_t missing;
        };
        std::vector<DailyTrend> daily_trends;

        // Knowledge gaps (frequently searched but missing)
        struct KnowledgeGap {
            std::string query;
            size_t search_count;
            std::chrono::system_clock::time_point last_searched;
        };
        std::vector<KnowledgeGap> knowledge_gaps;
    };

    struct Config {
        size_t max_log_entries = 100000;       // Max entries in memory log
        bool enable_missing_detection = true;  // Detect missing answers
        bool enable_daily_trends = true;       // Calculate daily trends
        size_t top_n = 20;                     // Number of top queries to track
    };

    explicit AnalyticsService(Config config);

    /**
     * Log a search query.
     */
    void logSearch(const SearchQuery& query);

    /**
     * Get analytics report for a time period.
     */
    AnalyticsReport getReport(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to
    );

    /**
     * Get report for the last 30 days (default).
     */
    AnalyticsReport getRecentReport();

    /**
     * Get knowledge gaps (frequently searched but missing answers).
     */
    std::vector<AnalyticsService::KnowledgeGap> getKnowledgeGaps(size_t min_search_count = 3);

    /**
     * Get raw search log (for debugging).
     */
    std::vector<SearchQuery> getSearchLog(size_t limit = 100) const;

    /**
     * Clear old log entries (keep only last N days).
     */
    void pruneLog(size_t keep_days = 30);

    /**
     * Export report to JSON string.
     */
    std::string exportToJson(const AnalyticsReport& report) const;

private:
    Config config_;
    std::vector<SearchQuery> log_;
    std::map<std::string, size_t> query_counts_;
    std::mutex mutex_;

    // Internal helpers
    void updateQueryCounts(const SearchQuery& query);
    std::vector<std::string> extractMissingQueries() const;
    std::vector<DailyTrend> calculateDailyTrends(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to
    ) const;
    std::vector<KnowledgeGap> calculateKnowledgeGaps() const;
    std::string formatDate(std::chrono::system_clock::time_point tp) const;
};
```

#### 5.4.2. Интеграция с RagApiHandler

Добавить логирование в `RagApiHandler::handleSearch()`:

```cpp
// After search is performed:
if (analytics_service_) {
    AnalyticsService::SearchQuery log_entry;
    log_entry.query = request_body["query"];
    log_entry.result_count = results.size();
    log_entry.has_answer = results.size() > 0;
    log_entry.response_time_ms = elapsed_ms;
    log_entry.client_ip = client_ip;
    if (!results.empty()) {
        log_entry.top_result_path = results[0].path;
    }
    analytics_service_->logSearch(log_entry);
}
```

---

### Этап 5: API Endpoints (1 день)

#### 5.5.1. Новые API endpoints

| Endpoint | Method | Описание |
|----------|--------|----------|
| `/api/analytics` | GET | Получить аналитику поисковых запросов |
| `/api/analytics/gaps` | GET | Получить список knowledge gaps |
| `/api/analytics/export` | POST | Экспорт отчёта в JSON |
| `/api/qa/dedup` | POST | Найти дубликаты QA-пар |
| `/api/qa/dedup/remove` | POST | Удалить дубликаты |
| `/api/import/markdown` | POST | Импортировать Markdown файлы |
| `/api/import/history` | GET | История импортов |

#### 5.5.2. Примеры API

**GET `/api/analytics`**

```bash
curl "http://localhost:8008/api/analytics?from=2026-03-01&to=2026-03-18"
```

**Ответ:**
```json
{
  "success": true,
  "report": {
    "total_queries": 1523,
    "queries_with_results": 1205,
    "queries_without_results": 318,
    "answer_rate_percent": 79.1,
    "avg_response_time_ms": 45.2,
    "top_queries": [
      {"query": "Как запустить проект?", "count": 156},
      {"query": "Как настроить логирование?", "count": 89},
      {"query": "Что такое DI Container?", "count": 67}
    ],
    "missing_answers": [
      "Как исправить ошибку компиляции на macOS?",
      "Почему тесты падают в CI?",
      "Как добавить новый middleware?"
    ],
    "knowledge_gaps": [
      {
        "query": "Как исправить ошибку компиляции на macOS?",
        "search_count": 12,
        "last_searched": "2026-03-18T10:30:00Z"
      }
    ]
  }
}
```

**POST `/api/qa/dedup`**

```bash
curl -X POST http://localhost:8008/api/qa/dedup \
  -H "Content-Type: application/json" \
  -d '{"source_id": "sqlite_kb", "threshold": 0.85}'
```

**Ответ:**
```json
{
  "success": true,
  "result": {
    "total_pairs": 150,
    "duplicates_found": 5,
    "duplicates": [
      {
        "primary_id": "qa_001",
        "duplicate_id": "qa_045",
        "similarity": 0.92,
        "reason": "Вопросы семантически идентичны"
      }
    ]
  }
}
```

**POST `/api/import/markdown`**

```bash
curl -X POST http://localhost:8008/api/import/markdown \
  -H "Content-Type: application/json" \
  -d '{"directory": "/path/to/docs", "recursive": true}'
```

**Ответ:**
```json
{
  "success": true,
  "result": {
    "files_imported": 15,
    "documents_created": 42,
    "duplicates_skipped": 3,
    "errors": []
  }
}
```

---

### Этап 6: Тестирование (2-3 дня)

#### 5.6.1. Тесты для SQLiteSource

**Новый файл:** `qornix_rag/tests/test_sqlite_source.cpp`

- `sqlite_source_basic` — создание, инициализация, тип
- `sqlite_source_add_pair` — добавление QA-пары
- `sqlite_source_find_pair` — поиск по ID
- `sqlite_source_update_pair` — обновление QA-пары
- `sqlite_source_delete_pair` — удаление QA-пары
- `sqlite_source_search_by_category` — поиск по категории
- `sqlite_source_persistence` — проверка персистентности (перезапуск)
- `sqlite_source_pagination` — пагинация getAllPairs
- `sqlite_source_migrate` — авто-миграция таблиц
- `sqlite_source_hash_uniqueness` — проверка уникальности по hash

#### 5.6.2. Тесты для MarkdownSource

**Новый файл:** `qornix_rag/tests/test_markdown_source.cpp`

- `markdown_source_basic` — создание, инициализация
- `markdown_parser_frontmatter` — парсинг frontmatter
- `markdown_parser_no_frontmatter` — файлы без frontmatter
- `markdown_parser_multiline_category` — категории с пробелами
- `markdown_import_single_file` — импорт одного файла
- `markdown_import_directory` — импорт директории
- `markdown_import_duplicate_detection` — обнаружение дубликатов

#### 5.6.3. Тесты для DeduplicationService

**Новый файл:** `qornix_rag/tests/test_deduplication.cpp`

- `dedup_basic` — создание сервиса
- `dedup_identical_questions` — идентичные вопросы
- `dedup_semantic_similarity` — семантическая схожесть
- `dedup_different_categories` — разные категории
- `dedup_threshold_config` — настройка порога
- `dedup_exclude_categories` — исключение категорий
- `dedup_auto_remove` — авто-удаление

#### 5.6.4. Тесты для AnalyticsService

**Новый файл:** `qornix_rag/tests/test_analytics.cpp`

- `analytics_basic` — создание сервиса
- `analytics_log_search` — логирование запроса
- `analytics_get_report` — получение отчёта
- `analytics_missing_detection` — обнаружение missing answers
- `analytics_knowledge_gaps` — knowledge gaps
- `analytics_top_queries` — топ запросов
- `analytics_prune_log` — очистка старых записей
- `analytics_export_json` — экспорт в JSON
- `analytics_thread_safety` — потокобезопасность

---

## 5. Зависимости

| Зависимость | Требуется | Примечания |
|-------------|-----------|------------|
| SQLite3 | Optional | Включается через `QORNIX_HAS_SQLITE` |
| Boost filesystem | Required | Для сканирования директорий |
| None | No | Markdown парсер — чистый C++20 |
| None | No | Deduplication — использует существующий RagEngine |
| None | No | Analytics — чистый C++20 |

---

## 6. Acceptance Criteria для Фазы 5

- [x] SQLiteDataSource сохраняется между перезапусками (персистентность)
- [x] Markdown файлы импортируются с парсингом frontmatter
- [x] Дедупликация обнаруживает дубликаты через semantic similarity
- [x] AnalyticsService отслеживает поисковые запросы и выявляет knowledge gaps
- [x] Все API endpoints работают корректно (7 новых endpoints)
- [x] 37 новых Phase 5 тестов проходят (SQLiteSource, MarkdownSource, DeduplicationService, AnalyticsService)
- [x] Полный RAG regression suite проходит: 13/13 test targets
- [x] Документация обновлена
- [x] Проект собирается и все RAG тесты проходят

---

## 7. Оценка сроков

| Этап | Задачи | Оценка |
|------|--------|--------|
| Этап 1: SQLiteDataSource | 4 задачи | 2-3 дня |
| Этап 2: MarkdownImportSource | 2 задачи | 1-2 дня |
| Этап 3: Semantic Deduplication | 2 задачи | 2 дня |
| Этап 4: AnalyticsService | 2 задачи | 2 дня |
| Этап 5: API Endpoints | 7 endpoints | 1 день |
| Этап 6: Тестирование | 4 файла тестов | 2-3 дня |
| **Итого** | | **10-13 дней** |

---

## 8. Риски и митигация

| Риск | Вероятность | Митигация |
|------|-------------|-----------|
| SQLite3 не установлен | Средняя | Optional dependency, graceful degradation |
| Markdown парсер не уловит все форматы | Низкая | Поддержка базового YAML frontmatter |
| Semantic dedup медленный на больших наборах | Средняя | Кэширование embedding, batch processing |
| Analytics log занимает много места | Низкая | Prune old entries, configurable max size |

---

**Last updated:** 2026-05-21
**Current version:** v1.1 (Phase 5 Complete)
