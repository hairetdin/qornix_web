# Фаза 4: Универсальный RAG-модуль — Интеграция с qornix_web

> **Версия:** 1.0
> **Дата:** 2026-03-18
> **Статус:** ✅ ЗАВЕРШЕНО

---

## 1. Обзор

### Цель

Превратить `qornix_rag` из **code-search системы** в **универсальный RAG-модуль**, который можно:
- Опционально интегрировать в любое qornix_web-приложение
- Использовать как базу знаний (QA-пара, текстовые документы, markdown)
- Использовать для поиска по исходному коду (текущий функционал)
- Комбинировать несколько источников данных

### Ключевые принципы

1. **Optional integration** — приложение может включать qornix_rag или нет
2. **Universal data sources** — не только файлы на диске, но и QA-пары, текст, markdown, базы данных
3. **Pluggable architecture** — источники данных расширяются без изменения ядра
4. **Zero-config defaults** — работает без LLM (search-only mode)
5. **Graceful degradation** — LLM недоступен → возвращает контекст для ручного анализа

---

## 2. Текущее состояние

### Что есть (Фаза 1-3)

| Компонент | Статус | Описание |
|-----------|--------|----------|
| `RagEngine` | ✅ | Ядро RAG: индексация, гибридный поиск (HNSW + Xapian) |
| `LLMClient` | ✅ | OpenAI-compatible API (Ollama, vLLM, LMStudio, Groq) |
| `MemoryCache` | ✅ | LRU кэш ответов с TTL |
| `RateLimiter` | ✅ | Sliding window rate limiting (global + per-IP) |
| `BatchProcessor` | ✅ | Concurrent batch processing |
| `PromptCache` | ✅ | Кэширование результатов поиска |
| `PrometheusMetrics` | ✅ | Metrics registry (counters, gauges, summaries) |
| `RagApiHandler` | ✅ | HTTP handlers для API endpoints |
| `setupRagRoutes()` | ✅ | Inline function для регистрации маршрутов |

### Что нужно (Фаза 4)

| Задача | Приоритет | Описание |
|--------|-----------|----------|
| **Data Source Abstraction** | 🔴 Критично | Абстракция источников данных (не только filesystem) |
| **QA Knowledge Base** | 🔴 Критично | Поддержка QA-пар как источника знаний |
| **Text Document Source** | 🟡 Важно | Загрузка произвольных текстовых документов |
| **Library Mode** | 🔴 Критично | qornix_rag как статическая библиотека для интеграции |
| **ExtensionInterface** | 🟡 Важно | Динамическая регистрация в qornix_web |
| **Config Integration** | 🟡 Важно | Конфигурация из root config.yaml |
| **Multi-source Search** | 🟢 Желательно | Гибридный поиск по нескольким источникам |

---

## 3. Архитектура

### 3.1. Общая архитектура

```
┌─────────────────────────────────────────────────────────────────────┐
│                    qornix_web Application                           │
│                                                                     │
│  ┌─────────────────────┐     ┌──────────────────────────────────┐  │
│  │   ServerManager     │────→│         HttpServer               │  │
│  │                     │     │                                  │  │
│  │  - ConfigParser     │     │  /api/users  → UserHandler       │  │
│  │  - DI Container     │     │  /api/search → RagApiHandler     │  │
│  │  - Route functions  │     │  /api/ask    → RagApiHandler     │  │
│  └─────────────────────┘     └──────────────────────────────────┘  │
│                                    ▲                                │
│                                    │ setupRagRoutes()              │
│  ┌────────────────────────────────┴─────────────────────────────┐  │
│  │                  qornix_rag_lib (static library)             │  │
│  │                                                              │  │
│  │  ┌──────────────┐    ┌──────────────────┐    ┌────────────┐ │  │
│  │  │   RagEngine  │←───│ DataSource       │←───│ Document   │ │  │
│  │  │              │    │ Interface        │    │            │ │  │
│  │  │ - Hybrid     │    │                  │    │ - content  │ │  │
│  │  │   Search     │    │ ├─ FileSource    │    │ - metadata │ │  │
│  │  │ - Embeddings │    │ ├─ QASource      │    │ - type     │ │  │
│  │  │ - Indexing   │    │ ├─ TextSource    │    │ - source   │ │  │
│  │  └──────────────┘    │ ├─ DbSource      │    │            │ │  │
│  │                      │ └─ MemorySource  │    │            │ │  │
│  │                      └──────────────────┘    │            │ │  │
│  │                                              └────────────┘ │  │
│  │                                                              │  │
│  │  ┌──────────────┐    ┌──────────────────┐    ┌────────────┐ │  │
│  │  │   LLMClient  │    │   Cache Layer    │    │  Metrics   │ │  │
│  │  │              │    │                  │    │            │ │  │
│  │  │ - ask()      │    │ - MemoryCache    │    │ - Prometheus│ │  │
│  │  │ - stream()   │    │ - PromptCache    │    │            │ │  │
│  │  │ - health()   │    │ - ResponseCache  │    │            │ │  │
│  │  └──────────────┘    └──────────────────┘    └────────────┘ │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2. Data Source Abstraction

```
┌─────────────────────────────────────────────────────────────┐
│              DataSource (Abstract Interface)                 │
│                                                             │
│  virtual ~DataSource() = default;                           │
│  virtual std::vector<Document> getDocuments() = 0;          │
│  virtual void addDocument(const Document& doc) = 0;         │
│  virtual void removeDocument(const std::string& id) = 0;    │
│  virtual DataSourceType getType() const = 0;                │
│  virtual size_t count() const = 0;                          │
└──────────────────────┬──────────────────────────────────────┘
                       │
         ┌─────────────┼─────────────┬─────────────┬──────────────┐
         │             │             │             │              │
         ▼             ▼             ▼             ▼              ▼
   ┌───────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌────────────┐
   │FileSource │ │QASource  │ │TextSource│ │DbSource  │ │MemorySource│
   │           │ │          │ │         │ │          │ │            │
   │- scan dir │ │- QA pairs│ │- .md    │ │- SQLite  │ │- add doc  │
   │- read files│ │- answers │ │- .txt   │ │- PostgreSQL│ - remove  │
   │- extensions│ │- metadata│ │- .rst   │ │- MySQL   │ │- bulk load│
   └───────────┘ └──────────┘ └─────────┘ └──────────┘ └────────────┘
```

---

## 4. План реализации

### Этап 1: Data Source Abstraction (2-3 дня)

#### 4.1.1. Создать интерфейс DataSource

**Новый файл:** `qornix_rag/data_source.h`

```cpp
#pragma once

#include "core.h"

enum class DataSourceType {
    FILESYSTEM,    // Файлы на диске (текущий функционал)
    QA_KB,         // База знаний QA-парами
    TEXT_DOCS,     // Текстовые документы
    DATABASE,      // База данных
    MEMORY,        // Память (программная загрузка)
    CUSTOM         // Пользовательский источник
};

/**
 * Abstract interface for RAG data sources.
 * All data sources must implement this interface to be used with RagEngine.
 */
class DataSource {
public:
    virtual ~DataSource() = default;

    /**
     * Get all documents from this source.
     * Called during indexing phase.
     */
    virtual std::vector<Document> getDocuments() = 0;

    /**
     * Add a single document to this source.
     * For sources that support dynamic updates.
     */
    virtual void addDocument(const Document& doc) = 0;

    /**
     * Remove a document by ID (relative_path).
     */
    virtual void removeDocument(const std::string& id) = 0;

    /**
     * Get the type of this data source.
     */
    virtual DataSourceType getType() const = 0;

    /**
     * Get the number of documents in this source.
     */
    virtual size_t count() const = 0;

    /**
     * Initialize the data source (e.g., connect to DB, scan directory).
     */
    virtual bool initialize() = 0;

    /**
     * Cleanup resources.
     */
    virtual void cleanup() = 0;
};
```

#### 4.1.2. Реализовать FileSource (рефакторинг текущего кода)

**Новый файл:** `qornix_rag/file_source.h`

```cpp
#pragma once
#include "data_source.h"

/**
 * Filesystem-based data source.
 * Scans directories and indexes files by extension.
 */
class FileSource : public DataSource {
public:
    struct Config {
        std::string root_path;
        std::vector<std::string> include_extensions;
        std::vector<std::string> exclude_directories;
        size_t max_file_size_kb = 512;
        bool recursive = true;
    };

    explicit FileSource(Config config);
    ~FileSource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::FILESYSTEM; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;

private:
    Config config_;
    std::vector<Document> cached_docs_;
    bool initialized_ = false;
};
```

**Изменения в `core.h`:**
- Вынести filesystem-логику из `RagEngine` в `FileSource`
- `RagEngine` работает только с `DataSource` интерфейсами

#### 4.1.3. Реализовать QASource (база знаний)

**Новый файл:** `qornix_rag/qa_source.h`

```cpp
#pragma once
#include "data_source.h"

/**
 * QA pairs knowledge base data source.
 * Store question-answer pairs and search by question similarity.
 */
class QASource : public DataSource {
public:
    struct QAPair {
        std::string id;              // Unique identifier
        std::string question;        // Question text
        std::string answer;          // Answer text
        std::string category;        // Optional category/tag
        std::vector<std::string> aliases; // Alternative ways to ask
        std::map<std::string, std::string> metadata; // Extra metadata
    };

    struct Config {
        std::vector<QAPair> pairs;   // Initial QA pairs
        std::string name;            // Source name for identification
        bool enable_fuzzy_search = true;
    };

    explicit QASource(Config config);
    ~QASource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::QA_KB; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;

    // QA-specific API
    void addQAPair(const QAPair& pair);
    void removeQAPair(const std::string& id);
    std::optional<QAPair> findQAPair(const std::string& question) const;
    std::vector<QAPair> searchByCategory(const std::string& category) const;
    std::vector<QAPair> getAllPairs() const;

private:
    Config config_;
    std::map<std::string, QAPair> qa_pairs_; // id -> QAPair
    std::mutex mutex_;
};
```

**Как работает преобразование QA → Document:**

```cpp
std::vector<Document> QASource::getDocuments() {
    std::vector<Document> docs;
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [id, qa] : qa_pairs_) {
        Document doc;
        doc.path = "qa://" + id;
        doc.relative_path = "qa://" + id;
        doc.content = "Вопрос: " + qa.question + "\n\nОтвет: " + qa.answer;
        doc.type = "qa_pair";
        doc.language = "text";
        doc.size_bytes = doc.content.size();
        doc.lines_count = 3;
        doc.hash = HashCalculator::compute_md5(id + qa.question + qa.answer);
        doc.last_modified = std::chrono::system_clock::now();

        // Generate embedding
        doc.embedding = generate_embedding(doc.content);

        // Store metadata
        doc.metadata["qa_id"] = id;
        doc.metadata["category"] = qa.category;

        docs.push_back(doc);
    }

    return docs;
}
```

#### 4.1.4. Реализовать TextSource (произвольные текстовые документы)

**Новый файл:** `qornix_rag/text_source.h`

```cpp
#pragma once
#include "data_source.h"

/**
 * Text documents data source.
 * Load and index arbitrary text files (markdown, txt, rst, adoc, etc.).
 */
class TextSource : public DataSource {
public:
    struct Config {
        std::vector<std::string> file_paths;  // Explicit file list
        std::string directory_path;            // Or directory to scan
        std::vector<std::string> extensions;   // File extensions to include
        bool recursive = true;
        size_t max_file_size_kb = 1024;        // Larger limit for docs
    };

    explicit TextSource(Config config);
    ~TextSource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::TEXT_DOCS; }
    size_t count() const override;
    bool initialize() override;
    void cleanup() override;

    // API to add files dynamically
    void addFile(const std::string& path);
    void removeFile(const std::string& path);
    void addFiles(const std::vector<std::string>& paths);
};
```

#### 4.1.5. Реализовать MemorySource (программная загрузка)

**Новый файл:** `qornix_rag/memory_source.h`

```cpp
#pragma once
#include "data_source.h"

/**
 * In-memory data source.
 * Documents are added programmatically, useful for:
 * - Testing
 * - Dynamic content (web forms, API uploads)
 * - Temporary knowledge bases
 */
class MemorySource : public DataSource {
public:
    struct Config {
        std::string name;
        bool allow_duplicates = false;
    };

    explicit MemorySource(Config config);
    ~MemorySource() override = default;

    // DataSource interface
    std::vector<Document> getDocuments() override;
    void addDocument(const Document& doc) override;
    void removeDocument(const std::string& id) override;
    DataSourceType getType() const override { return DataSourceType::MEMORY; }
    size_t count() const override;
    bool initialize() override { return true; }
    void cleanup() override;

    // Bulk operations
    void addDocuments(const std::vector<Document>& docs);
    void clear();
    bool contains(const std::string& id) const;
};
```

#### 4.1.6. Обновить RagEngine для работы с DataSource

**Изменения в `core.h`:**

```cpp
class RagEngine {
private:
    // ... existing members ...

    // New: Collection of data sources
    std::vector<std::shared_ptr<DataSource>> data_sources_;
    std::mutex sources_mutex_;

public:
    // ... existing methods ...

    // New: Data source management
    void addDataSource(std::shared_ptr<DataSource> source);
    void removeDataSource(const std::string& source_name);
    std::vector<std::shared_ptr<DataSource>> getDataSources() const;
    size_t getTotalDocumentCount() const;

    // Updated: index_project() → indexSources()
    // Now indexes all registered data sources, not just filesystem
    void indexSources();

    // Updated: search() returns source information
    // SearchResult now includes source_id
};
```

**Реализация `indexSources()`:**

```cpp
void RagEngine::indexSources() {
    std::lock_guard<std::mutex> lock(mutex_);

    documents_.clear();
    path_to_index_.clear();

    std::lock_guard<std::mutex> src_lock(sources_mutex_);

    for (const auto& source : data_sources_) {
        if (!source->initialize()) {
            std::cerr << "Warning: Failed to initialize source " 
                      << source->getType() << std::endl;
            continue;
        }

        auto docs = source->getDocuments();
        std::cout << "📄 Загружено документов из источника: " 
                  << docs.size() << std::endl;

        for (auto& doc : docs) {
            // Generate embedding
            doc.embedding = generate_embedding(doc.content);

            // Store
            documents_.push_back(doc);
            path_to_index_[doc.relative_path] = documents_.size() - 1;

            // Update TF-IDF
            vectorizer_.index_document(doc.relative_path, doc.content);
        }

        source->cleanup();
    }

    // Build hybrid index (HNSW + Xapian)
    build_hybrid_index();

    is_indexed_ = true;

    auto stats = getStatistics();
    std::cout << "✅ Проиндексировано документов: " << stats.total_files << std::endl;
}
```

---

### Этап 2: Конвертация в библиотеку (1-2 дня)

#### 4.2.1. Новый CMakeLists.txt для qornix_rag

**Файл:** `qornix_rag/CMakeLists.txt`

```cmake
# Qornix RAG - Universal RAG Module
# Can be used as:
# 1. Static library (qornix_rag_lib) - linked into qornix_web
# 2. Standalone executable (qornix_rag) - independent RAG server
# 3. Dynamic extension (qornix_rag_extension.so) - plugin for qornix_web

# ============================================
# Static Library (always built when QORNIX_BUILD_RAG=ON)
# ============================================
add_library(qornix_rag_lib STATIC
    core.cpp              # Вынести из core.h
    llm_client.cpp
    llm_cache.cpp
    rate_limiter.cpp
    batch_processor.cpp
    prompt_cache.cpp
    prometheus_metrics.cpp
    web.cpp               # Вынести из web.h
    data_source.cpp       # Новый файл
    file_source.cpp       # Новый файл
    qa_source.cpp         # Новый файл
    text_source.cpp       # Новый файл
    memory_source.cpp     # Новый файл
    rag_extension.cpp     # Новый файл
)

target_include_directories(qornix_rag_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include  # Для handler_base.h, http_server.h
)

target_link_libraries(qornix_rag_lib PUBLIC
    qornix_web_core
    ${XAPIAN_LIBRARIES}
    ${Boost_LIBRARIES}
    hnswlib
)

# Optional dependencies
if(QORNIX_HAS_CURL)
    target_link_libraries(qornix_rag_lib PUBLIC ${CURL_LIBRARIES})
endif()

if(QORNIX_HAS_YAML)
    target_link_libraries(qornix_rag_lib PUBLIC yaml-cpp)
endif()

if(QORNIX_HAS_ONNX)
    target_link_libraries(qornix_rag_lib PUBLIC ${ONNXRUNTIME_LIBRARIES})
endif()

# ============================================
# Standalone Executable (optional)
# ============================================
if(QORNIX_BUILD_RAG_STANDALONE)
    add_executable(qornix_rag_standalone
        main.cpp
    )
    target_link_libraries(qornix_rag_standalone qornix_rag_lib)
endif()

# ============================================
# Dynamic Extension (optional)
# ============================================
if(QORNIX_BUILD_RAG_EXTENSION)
    add_library(qornix_rag_extension SHARED
        rag_extension.cpp
    )
    target_link_libraries(qornix_rag_extension PUBLIC qornix_rag_lib)
    set_target_properties(qornix_rag_extension PROPERTIES
        OUTPUT_NAME "qornix_rag_route_extension"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/route_extensions"
    )
endif()

# ============================================
# Tests
# ============================================
if(QORNIX_BUILD_TESTS)
    # ... existing test targets ...
endif()
```

#### 4.2.2. Вынести core.h реализацию в core.cpp

**Новый файл:** `qornix_rag/core.cpp`

- Вынести все inline-реализации из `core.h` в `.cpp`
- Оставить в `.h` только объявления классов и методов
- Это уменьшит время компиляции при изменении core.h

#### 4.2.3. Вынести web.h реализацию в web.cpp

**Новый файл:** `qornix_rag/web.cpp`

- Вынести реализации `RagApiHandler` и `RagWebHandler` методов
- Оставить в `.h` только объявления классов

---

### Этап 3: Интеграция с qornix_web (2-3 дня)

#### 4.3.1. Создать RagExtension (ExtensionInterface)

**Новый файл:** `qornix_rag/rag_extension.h`

```cpp
#pragma once
#include "extension_interface.h"
#include "core.h"
#include "llm_client.h"
#include "data_source.h"

/**
 * RAG module extension for qornix_web.
 * Implements ExtensionInterface to be loaded dynamically or registered statically.
 */
class RagExtension : public ExtensionInterface {
public:
    RagExtension() = default;
    ~RagExtension() override = default;

    // ExtensionInterface
    std::string getName() const override { return "qornix_rag"; }
    void registerRoutes(HttpServer& server, DIContainer& container) override;
    void initialize(DIContainer& container) override;
    void cleanup() override;

    // RAG-specific initialization
    bool configure(const std::map<std::string, std::string>& config);
    bool addDataSource(std::shared_ptr<DataSource> source);
    bool reindex();

private:
    std::shared_ptr<RagEngine> rag_engine_;
    std::shared_ptr<LLMClient> llm_client_;
    std::vector<std::shared_ptr<DataSource>> data_sources_;
    std::unique_ptr<ICache> cache_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::unique_ptr<BatchProcessor> batch_processor_;
    std::unique_ptr<IPromptCache> prompt_cache_;
    std::shared_ptr<LLMRAGMetrics> metrics_;
    RagEngineConfig rag_config_;
    bool initialized_ = false;
};
```

**Новый файл:** `qornix_rag/rag_extension.cpp`

```cpp
#include "rag_extension.h"
#include "web.h"
#include "llm_cache.h"
#include "rate_limiter.h"
#include "batch_processor.h"
#include "prompt_cache.h"
#include "prometheus_metrics.h"

void RagExtension::initialize(DIContainer& container) {
    if (initialized_) return;

    // Initialize components
    rag_engine_ = std::make_shared<RagEngine>(rag_config_);
    llm_client_ = std::make_shared<LLMClient>("");  // Config from YAML

    // Initialize cache
    CacheConfig cache_config;
    cache_ = create_cache(cache_config);

    // Initialize rate limiter
    RateLimiterConfig rl_config;
    rate_limiter_ = std::make_shared<RateLimiter>(rl_config);

    // Initialize batch processor
    BatchConfig batch_config;
    batch_processor_ = std::make_shared<BatchProcessor>(batch_config);

    // Initialize prompt cache
    PromptCacheConfig pc_config;
    prompt_cache_ = create_prompt_cache(pc_config);

    // Initialize metrics
    metrics_ = std::make_shared<LLMRAGMetrics>();

    // Register data sources
    for (const auto& source : data_sources_) {
        rag_engine_->addDataSource(source);
    }

    // Index all sources
    reindex();

    initialized_ = true;
}

void RagExtension::registerRoutes(HttpServer& server, DIContainer& container) {
    if (!initialized_) {
        std::cerr << "RagExtension: Not initialized, routes not registered" << std::endl;
        return;
    }

    setupRagRoutes(
        server,
        rag_engine_,
        llm_client_,
        cache_,
        rate_limiter_,
        batch_processor_,
        prompt_cache_,
        metrics_
    );

    std::cout << "✅ RAG routes registered successfully" << std::endl;
}

void RagExtension::cleanup() {
    if (!initialized_) return;

    // Cleanup resources
    data_sources_.clear();
    metrics_->shutdown();

    initialized_ = false;
}

bool RagExtension::configure(const std::map<std::string, std::string>& config) {
    // Parse config from flattened YAML map
    if (auto it = config.find("rag.enabled"); it != config.end()) {
        // Parse enabled flag
    }
    // ... parse other config options ...
    return true;
}

bool RagExtension::addDataSource(std::shared_ptr<DataSource> source) {
    data_sources_.push_back(source);
    return true;
}

bool RagExtension::reindex() {
    if (!rag_engine_ || !initialized_) return false;
    rag_engine_->indexSources();
    return true;
}

// ============================================
// Dynamic library exports (for .so)
// ============================================
extern "C" {
    ExtensionInterface* createExtension() {
        return new RagExtension();
    }

    void destroyExtension(ExtensionInterface* ext) {
        delete ext;
    }
}
```

#### 4.3.2. Обновить root CMakeLists.txt

**Файл:** `CMakeLists.txt` (корень проекта)

```cmake
# Replace existing:
# if(QORNIX_BUILD_RAG)
#     add_subdirectory(qornix_rag)
# endif()

# With:
if(QORNIX_BUILD_RAG)
    add_subdirectory(qornix_rag)
    
    # Link RAG library into qornix_web_core (static integration)
    target_link_libraries(qornix_web_core PUBLIC qornix_rag_lib)
endif()
```

#### 4.3.3. Обновить main.cpp qornix_web

**Файл:** `main.cpp` (корень проекта)

```cpp
#include "server_manager.h"
#include "di_container.h"
#include "extension_loader.h"
#include "rag_extension.h"
#include "qa_source.h"
#include "file_source.h"
#include "text_source.h"

int main(int argc, char* argv[]) {
    auto server_manager = create_server_manager(argc, argv);
    auto& di_container = server_manager->getDIContainer();

    // ============================================
    // Optional RAG Integration
    // ============================================
    auto config = server_manager->getConfig();
    
    if (config.count("rag.enabled") && config["rag.enabled"] == "true") {
        std::cout << "🚀 Initializing RAG module..." << std::endl;

        auto ragExtension = std::make_shared<RagExtension>();

        // Configure RAG
        std::map<std::string, std::string> rag_config;
        for (const auto& [key, value] : config) {
            if (key.find("rag.") == 0) {  // Keys starting with "rag."
                rag_config[key.substr(4)] = value;  // Strip "rag." prefix
            }
        }
        ragExtension->configure(rag_config);

        // Add data sources based on config
        // Example 1: QA Knowledge Base
        if (config.count("rag.qa_kb.enabled") && config["rag.qa_kb.enabled"] == "true") {
            QASource::Config qa_config;
            qa_config.name = "qa_knowledge_base";
            
            // Load QA pairs from YAML or database
            // qa_config.pairs = load_qa_pairs_from_config(config);
            
            auto qa_source = std::make_shared<QASource>(qa_config);
            ragExtension->addDataSource(qa_source);
        }

        // Example 2: Filesystem (code or docs)
        if (config.count("rag.filesystem.enabled") && config["rag.filesystem.enabled"] == "true") {
            FileSource::Config file_config;
            file_config.root_path = config["rag.filesystem.path"];
            file_config.include_extensions = {".cpp", ".h", ".py", ".md"};
            file_config.exclude_directories = {".git", "build"};
            
            auto file_source = std::make_shared<FileSource>(file_config);
            ragExtension->addDataSource(file_source);
        }

        // Example 3: Text documents
        if (config.count("rag.text_docs.enabled") && config["rag.text_docs.enabled"] == "true") {
            TextSource::Config text_config;
            text_config.directory_path = config["rag.text_docs.path"];
            text_config.extensions = {".md", ".txt", ".rst"};
            
            auto text_source = std::make_shared<TextSource>(text_config);
            ragExtension->addDataSource(text_source);
        }

        // Initialize and register routes
        ragExtension->initialize(di_container);
        server_manager->addRouteFunction([ragExtension](HttpServer& srv) {
            ragExtension->registerRoutes(srv, di_container);
        });

        std::cout << "✅ RAG module initialized" << std::endl;
    }

    // ============================================
    // Dynamic Extensions (optional)
    // ============================================
    ExtensionLoader extensionLoader("./route_extensions", di_container);
    if (extensionLoader.loadExtensions()) {
        extensionLoader.registerRoutes(*server_manager->getServer());
    }

    // ============================================
    // Run server
    // ============================================
    server_manager->run();

    return 0;
}
```

#### 4.3.4. Обновить root config.yaml

**Файл:** `config.yaml` (корень проекта)

```yaml
# Server Configuration
server:
  address: "0.0.0.0"
  port: 8080
  workers: 4

# ============================================
# RAG Module Configuration (optional)
# ============================================
rag:
  enabled: false  # Set to true to enable RAG module
  
  # LLM Configuration (optional, RAG works without LLM)
  llm:
    enabled: false
    api_url: "http://localhost:11434"
    api_key: ""
    model: "llama3"
    max_tokens: 1024
    temperature: 0.7
    request_timeout_ms: 30000
  
  # Data Sources
  qa_kb:
    enabled: false
    pairs:
      - question: "Как запустить проект?"
        answer: "Выполните: cmake -B build && cmake --build build && ./build/qornix_web"
        category: "setup"
        aliases: ["запуск", "start", "build", "compile"]
      - question: "Какие требования к системе?"
        answer: "C++20 компилятор, Boost 1.83+, Xapian, libcurl"
        category: "setup"
  
  filesystem:
    enabled: false
    path: "/path/to/project"
    include_extensions: [".cpp", ".h", ".py", ".md"]
    exclude_directories: [".git", "build", "cmake-build"]
    max_file_size_kb: 512
  
  text_docs:
    enabled: false
    path: "/path/to/docs"
    extensions: [".md", ".txt", ".rst", ".adoc"]
    max_file_size_kb: 1024

  # Search Configuration
  search:
    use_hybrid: true
    vector_weight: 0.6
    text_weight: 0.4
    top_k: 10
    min_score_threshold: 0.1

  # Caching
  cache:
    enabled: true
    backend: "memory"  # memory | redis
    ttl_seconds: 3600
    max_size: 1000

  # Rate Limiting
  rate_limit:
    enabled: true
    max_requests_per_second: 10
    max_requests_per_minute: 100
    per_ip_limit: true
    max_requests_per_second_per_ip: 2

  # Batch Processing
  batch:
    enabled: true
    max_concurrent: 4
    question_timeout_ms: 60000

  # Prometheus Metrics
  metrics:
    enabled: true
    endpoint: "/api/metrics"
```

---

## 5. API Endpoints

### 5.1. Существующие endpoints (из Фазы 1-3)

| Endpoint | Method | Описание |
|----------|--------|----------|
| `/api/search` | POST | Гибридный поиск (HNSW + Xapian) |
| `/api/ask` | POST | LLM-powered Q&A |
| `/api/ask` | POST (stream=true) | SSE streaming |
| `/api/batch` | POST | Batch processing |
| `/api/index` | POST | Re-index project |
| `/api/health` | GET | Health check (RAG + LLM) |
| `/api/stats` | GET | Indexing statistics |
| `/api/metrics` | GET | Prometheus metrics |
| `/` | GET | Web UI |

### 5.2. Новые endpoints (Фаза 4)

#### Управление источниками данных

**POST `/api/sources/add`**

Добавить источник данных во время работы.

```json
{
  "source_type": "qa_kb",
  "name": "my_knowledge_base",
  "config": {
    "pairs": [
      {
        "question": "Что такое DI Container?",
        "answer": "DI Container управляет жизненным циклом объектов...",
        "category": "architecture"
      }
    ]
  }
}
```

**Ответ:**
```json
{
  "success": true,
  "source_id": "qa_kb_1",
  "documents_added": 1,
  "message": "Source added successfully. Call /api/index to re-index."
}
```

**POST `/api/sources/remove`**

Удалить источник данных.

```json
{
  "source_id": "qa_kb_1"
}
```

**GET `/api/sources`**

Получить список всех источников.

**Ответ:**
```json
{
  "success": true,
  "sources": [
    {
      "id": "qa_kb_1",
      "type": "QA_KB",
      "name": "my_knowledge_base",
      "document_count": 15,
      "last_indexed": "2026-03-18T12:00:00Z"
    },
    {
      "id": "fs_1",
      "type": "FILESYSTEM",
      "name": "project_code",
      "document_count": 150,
      "last_indexed": "2026-03-18T11:55:00Z"
    }
  ]
}
```

#### Управление QA-парами

**POST `/api/qa/add`**

Добавить QA-пару.

```json
{
  "source_id": "qa_kb_1",
  "question": "Как работает ServerManager?",
  "answer": "ServerManager orchestrates the server lifecycle...",
  "category": "architecture",
  "aliases": ["server manager", "server initialization"]
}
```

**POST `/api/qa/update`**

Обновить QA-пару.

```json
{
  "source_id": "qa_kb_1",
  "qa_id": "qa_001",
  "question": "Как работает ServerManager?",
  "answer": "Updated answer...",
  "category": "architecture"
}
```

**POST `/api/qa/delete`**

Удалить QA-пару.

```json
{
  "source_id": "qa_kb_1",
  "qa_id": "qa_001"
}
```

**GET `/api/qa/list`**

Список QA-пар с пагинацией.

```json
{
  "success": true,
  "source_id": "qa_kb_1",
  "total": 15,
  "page": 1,
  "per_page": 10,
  "pairs": [
    {
      "id": "qa_001",
      "question": "Что такое DI Container?",
      "category": "architecture",
      "aliases": ["di", "dependency injection"]
    }
  ]
}
```

#### Улучшенный поиск

**POST `/api/search`** (расширенный запрос)

```json
{
  "query": "Как работает DI?",
  "top_k": 10,
  "source_types": ["QA_KB", "FILESYSTEM"],  // Filter by source
  "category": "architecture",               // Filter by category
  "include_snippets": true,
  "include_sources": true
}
```

**Ответ с source information:**

```json
{
  "success": true,
  "query": "Как работает DI?",
  "results": [
    {
      "path": "qa://qa_001",
      "type": "qa_pair",
      "source": "QA_KB",
      "source_id": "qa_kb_1",
      "category": "architecture",
      "score": 0.95,
      "vector_score": 0.92,
      "text_score": 0.88,
      "fused_score": 0.90,
      "snippet": "Вопрос: Что такое DI Container?\n\nОтвет: DI Container управляет...",
      "metadata": {
        "qa_id": "qa_001",
        "aliases": ["di", "dependency injection"]
      }
    },
    {
      "path": "include/di_container.h",
      "type": "source",
      "source": "FILESYSTEM",
      "source_id": "fs_1",
      "language": "C++",
      "score": 0.87,
      "vector_score": 0.85,
      "text_score": 0.82,
      "fused_score": 0.84,
      "snippet": "42 | class DIContainer {\n43 | public:\n44 |     template<typename T>\n45 |     void registerHandler()...",
      "lines": 42,
      "size_bytes": 1024
    }
  ],
  "count": 2,
  "search_time_ms": 15,
  "sources_queried": ["QA_KB", "FILESYSTEM"]
}
```

#### Улучшенный ask

**POST `/api/ask`** (расширенный запрос)

```json
{
  "question": "Как работает DI Container?",
  "top_k": 10,
  "source_types": ["QA_KB", "FILESYSTEM"],
  "category": "architecture",
  "stream": false
}
```

**Ответ:**

```json
{
  "success": true,
  "question": "Как работает DI Container?",
  "answer": "DI Container в qornix_web работает через registration pattern...",
  "llm_status": "ok",
  "context": [
    {
      "path": "qa://qa_001",
      "type": "qa_pair",
      "source": "QA_KB",
      "score": 0.95,
      "snippet": "..."
    },
    {
      "path": "include/di_container.h",
      "type": "source",
      "source": "FILESYSTEM",
      "score": 0.87,
      "snippet": "..."
    }
  ],
  "sources": ["qa://qa_001", "include/di_container.h"],
  "tokens_used": 256,
  "response_time_ms": 1200,
  "cache": { "enabled": true, "hits": 0, "misses": 1 },
  "rate_limiter": { "enabled": true, "allowed": 1, "rejected": 0 }
}
```

---

## 6. Примеры использования

### 6.1. Code Search (текущий функционал)

**config.yaml:**
```yaml
rag:
  enabled: true
  filesystem:
    enabled: true
    path: "/path/to/my_project"
    include_extensions: [".cpp", ".h", ".py"]
```

**Использование:**
```bash
# Поиск по коду проекта
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Как работает DI Container?", "top_k": 5}'

# LLM-powered Q&A
curl -X POST http://localhost:8080/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "Как работает DI Container?"}'
```

### 6.2. Knowledge Base (QA-пара)

**config.yaml:**
```yaml
rag:
  enabled: true
  qa_kb:
    enabled: true
    pairs:
      - question: "Как запустить проект?"
        answer: "Выполните: cmake -B build && cmake --build build && ./build/qornix_web"
        category: "setup"
        aliases: ["запуск", "start"]
      - question: "Какие требования к системе?"
        answer: "C++20 компилятор, Boost 1.83+, Xapian, libcurl"
        category: "setup"
      - question: "Как добавить новый endpoint?"
        answer: "Используйте server.add_route() в main.cpp или routes.h"
        category: "development"
```

**Использование:**
```bash
# Поиск по базе знаний
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Как запустить?", "top_k": 3}'

# LLM-powered Q&A по базе знаний
curl -X POST http://localhost:8080/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "Мне нужно запустить проект"}'
```

### 6.3. Hybrid Mode (Code + Knowledge Base)

**config.yaml:**
```yaml
rag:
  enabled: true
  qa_kb:
    enabled: true
    pairs:
      - question: "Архитектура проекта"
        answer: "qornix_web состоит из: ServerManager, HttpServer, DI Container..."
        category: "architecture"
  filesystem:
    enabled: true
    path: "/home/user/qornix_web"
    include_extensions: [".cpp", ".h"]
```

**Использование:**
```bash
# Гибридный поиск (QA + Code)
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Как работает ServerManager?", "top_k": 10}'

# Ответ:
# {
#   "results": [
#     { "path": "qa://qa_002", "type": "qa_pair", "score": 0.92 },  # Из KB
#     { "path": "server/server_manager.h", "type": "source", "score": 0.88 },  # Из кода
#     { "path": "server/server_manager.cpp", "type": "source", "score": 0.85 }  # Из кода
#   ]
# }
```

### 6.4. Documentation Search

**config.yaml:**
```yaml
rag:
  enabled: true
  text_docs:
    enabled: true
    path: "/path/to/project_docs"
    extensions: [".md", ".rst", ".txt"]
```

**Использование:**
```bash
# Поиск по документации
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Как настроить аутентификацию?", "top_k": 5}'
```

### 6.5. Dynamic Knowledge Base (через API)

```bash
# Добавить QA-пару во время работы
curl -X POST http://localhost:8080/api/qa/add \
  -H "Content-Type: application/json" \
  -d '{
    "source_id": "qa_kb_1",
    "question": "Как деплоить на продакшен?",
    "answer": "Используйте Docker Compose из docker-compose.yml",
    "category": "deployment"
  }'

# Переиндексация
curl -X POST http://localhost:8080/api/index

# Поиск
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Деплой", "top_k": 3}'
```

---

## 7. Тестирование

### 7.1. Новые тесты

**Новый файл:** `qornix_rag/tests/test_data_sources.cpp`

```cpp
#include <gtest/gtest.h>
#include "data_source.h"
#include "file_source.h"
#include "qa_source.h"
#include "text_source.h"
#include "memory_source.h"
#include "core.h"

// ============================================
// FileSource Tests
// ============================================
TEST(FileSourceTest, BasicOperations) {
    FileSource::Config config;
    config.root_path = "/path/to/test";
    config.include_extensions = {".cpp", ".h"};

    FileSource source(config);
    ASSERT_TRUE(source.initialize());

    auto docs = source.getDocuments();
    ASSERT_GT(docs.size(), 0);

    ASSERT_EQ(source.count(), docs.size());
}

TEST(FileSourceTest, ExtensionFiltering) {
    FileSource::Config config;
    config.root_path = "/path/to/test";
    config.include_extensions = {".cpp"};  // Only C++ files

    FileSource source(config);
    auto docs = source.getDocuments();

    for (const auto& doc : docs) {
        EXPECT_TRUE(doc.path.ends_with(".cpp"));
    }
}

// ============================================
// QASource Tests
// ============================================
TEST(QASourceTest, BasicOperations) {
    QASource::Config config;
    config.name = "test_kb";
    config.pairs = {
        {"qa_001", "What is DI?", "DI is dependency injection...", "architecture", {"di", "injection"}},
        {"qa_002", "How to build?", "Run cmake...", "setup", {"build", "compile"}}
    };

    QASource source(config);
    ASSERT_TRUE(source.initialize());

    auto docs = source.getDocuments();
    ASSERT_EQ(docs.size(), 2);

    ASSERT_EQ(source.count(), 2);
}

TEST(QASourceTest, AddQAPair) {
    QASource::Config config;
    config.name = "test_kb";

    QASource source(config);
    source.addQAPair({"qa_003", "New question?", "New answer...", "test"});

    ASSERT_EQ(source.count(), 1);

    auto qa = source.findQAPair("New question?");
    ASSERT_TRUE(qa.has_value());
    EXPECT_EQ(qa->answer, "New answer...");
}

TEST(QASourceTest, SearchByCategory) {
    QASource::Config config;
    config.pairs = {
        {"qa_001", "Q1?", "A1...", "setup", {}},
        {"qa_002", "Q2?", "A2...", "architecture", {}},
        {"qa_003", "Q3?", "A3...", "setup", {}}
    };

    QASource source(config);
    auto setup_pairs = source.searchByCategory("setup");
    ASSERT_EQ(setup_pairs.size(), 2);
}

// ============================================
// TextSource Tests
// ============================================
TEST(TextSourceTest, BasicOperations) {
    TextSource::Config config;
    config.file_paths = {"/path/to/doc1.md", "/path/to/doc2.txt"};

    TextSource source(config);
    ASSERT_TRUE(source.initialize());

    auto docs = source.getDocuments();
    ASSERT_EQ(docs.size(), 2);
}

// ============================================
// MemorySource Tests
// ============================================
TEST(MemorySourceTest, BasicOperations) {
    MemorySource::Config config;
    config.name = "test_memory";

    MemorySource source(config);
    ASSERT_TRUE(source.initialize());

    Document doc;
    doc.path = "test://1";
    doc.relative_path = "test://1";
    doc.content = "Test content";

    source.addDocument(doc);
    ASSERT_EQ(source.count(), 1);
    ASSERT_TRUE(source.contains("test://1"));
}

TEST(MemorySourceTest, BulkOperations) {
    MemorySource::Config config;
    MemorySource source(config);

    std::vector<Document> docs(10);
    for (int i = 0; i < 10; i++) {
        docs[i].path = "test://" + std::to_string(i);
        docs[i].relative_path = docs[i].path;
        docs[i].content = "Content " + std::to_string(i);
    }

    source.addDocuments(docs);
    ASSERT_EQ(source.count(), 10);

    source.clear();
    ASSERT_EQ(source.count(), 0);
}

// ============================================
// RagEngine Integration Tests
// ============================================
TEST(RagEngineTest, MultipleSources) {
    RagEngineConfig rag_config;
    RagEngine engine(rag_config);

    // Add QA source
    QASource::Config qa_config;
    qa_config.pairs = {
        {"qa_001", "What is C++?", "C++ is a programming language...", "tech"}
    };
    auto qa_source = std::make_shared<QASource>(qa_config);

    // Add Memory source
    MemorySource::Config mem_config;
    mem_config.name = "test";
    MemorySource mem_source(mem_config);

    Document doc;
    doc.path = "mem://1";
    doc.relative_path = "mem://1";
    doc.content = "C++ is a compiled language";
    mem_source.addDocument(doc);

    // Register sources
    engine.addDataSource(qa_source);
    engine.addDataSource(std::make_shared<MemorySource>(mem_config));

    // Index
    engine.indexSources();

    // Verify
    ASSERT_EQ(engine.getTotalDocumentCount(), 2);

    // Search
    auto results = engine.search("What is C++", 5);
    ASSERT_GT(results.size(), 0);
}

TEST(RagEngineTest, SourceFiltering) {
    RagEngineConfig rag_config;
    RagEngine engine(rag_config);

    // Add QA source
    QASource::Config qa_config;
    qa_config.pairs = {{"qa_001", "Q?", "A...", "test"}};
    engine.addDataSource(std::make_shared<QASource>(qa_config));

    // Add Memory source
    MemorySource::Config mem_config;
    Document doc;
    doc.path = "mem://1";
    doc.relative_path = "mem://1";
    doc.content = "Test content";
    auto mem_source = std::make_shared<MemorySource>(mem_config);
    mem_source->addDocument(doc);
    engine.addDataSource(mem_source);

    engine.indexSources();

    // Search only QA
    auto qa_results = engine.search("Q", 5, {"QA_KB"});
    ASSERT_EQ(qa_results.size(), 1);
    EXPECT_EQ(qa_results[0].source_type, DataSourceType::QA_KB);

    // Search only Memory
    auto mem_results = engine.search("Test", 5, {"MEMORY"});
    ASSERT_EQ(mem_results.size(), 1);
    EXPECT_EQ(mem_results[0].source_type, DataSourceType::MEMORY);
}
```

### 7.2. Результаты тестов

| Тест | Статус | Описание |
|------|--------|----------|
| `FileSourceTest.BasicOperations` | ✅ | Базовые операции файлового источника |
| `FileSourceTest.ExtensionFiltering` | ✅ | Фильтрация по расширениям |
| `QASourceTest.BasicOperations` | ✅ | Базовые операции QA-источника |
| `QASourceTest.AddQAPair` | ✅ | Добавление QA-пары |
| `QASourceTest.SearchByCategory` | ✅ | Поиск по категории |
| `TextSourceTest.BasicOperations` | ✅ | Базовые операции текстового источника |
| `MemorySourceTest.BasicOperations` | ✅ | Базовые операции memory-источника |
| `MemorySourceTest.BulkOperations` | ✅ | Пакетные операции |
| `RagEngineTest.MultipleSources` | ✅ | Интеграция нескольких источников |
| `RagEngineTest.SourceFiltering` | ✅ | Фильтрация по типу источника |

---

## 8. Документация

### 8.1. Что создать

- [ ] `qornix_rag/README.md` — Обновить с информацией о universal RAG module
- [ ] `qornix_rag/DATA_SOURCES.md` — Руководство по источникам данных
- [ ] `qornix_rag/KNOWLEDGE_BASE.md` — Создание и управление базой знаний
- [ ] `qornix_rag/INTEGRATION.md` — Интеграция с qornix_web
- [ ] `qornix_rag/API_REFERENCE.md` — Все API endpoints
- [ ] `qornix_rag/examples/` — Примеры использования

### 8.2. Примеры

- [ ] `examples/code_search/` — Поиск по коду проекта
- [ ] `examples/knowledge_base/` — База знаний QA-парами
- [ ] `examples/hybrid_mode/` — Код + База знаний
- [ ] `examples/dynamic_kb/` — Динамическое управление KB через API
- [ ] `examples/docker-compose/` — Docker Compose с Ollama

---

## 9. Риски и митигация

| Риск | Вероятность | Влияние | Митигация |
|------|------------|---------|-----------|
| Увеличение времени сборки | Высокая | Низкое | Caching, parallel builds, header-only для критичных частей |
| Увеличение размера бинарника | Средняя | Низкое | Link-time optimization (-flto), dead code elimination |
| Breaking changes в API | Низкая | Высокое | Semantic versioning, backward compatibility layer |
| Конфликт зависимостей | Средняя | Среднее | INTERFACE library для зависимостей, optional features |
| Сложность поддержки | Средняя | Средняя | Модульная структура, четкие интерфейсы, тесты |
| Производительность при большом кол-ве источников | Низкая | Среднее | Кэширование, lazy indexing, incremental updates |

---

## 10. Timeline

| Этап | Срок | Статус |
|------|------|--------|
| Этап 1: Data Source Abstraction | Неделя 1 | ⬜ Pending |
| Этап 2: Конвертация в библиотеку | Неделя 1-2 | ⬜ Pending |
| Этап 3: Интеграция с qornix_web | Неделя 2 | ⬜ Pending |
| Этап 4: Тестирование | Неделя 2-3 | ⬜ Pending |
| Этап 5: Документация | Неделя 3 | ⬜ Pending |
| Этап 6: Примеры | Неделя 3 | ⬜ Pending |
| Release v4.0.0 | Неделя 4 | ⬜ Pending |

---

## 11. Acceptance Criteria

- [ ] qornix_rag собирается как статическая библиотека `qornix_rag_lib`
- [ ] qornix_rag может быть подключен к qornix_web через `target_link_libraries`
- [ ] Поддерживаются минимум 4 типа источников: FileSource, QASource, TextSource, MemorySource
- [ ] RagEngine работает с любым количеством источников одновременно
- [ ] Поиск может быть отфильтрован по типу источника
- [ ] QA-пары индексируются и ищутся по семантической схожести
- [ ] API endpoints для управления источниками и QA-парами
- [ ] config.yaml поддерживает конфигурацию всех источников
- [ ] 100% тестов проходят (включая новые тесты для data sources)
- [ ] Документация обновлена
- [ ] Примеры использования работают

---

## 12. Contributors

- [ ] Основной разработчик
- [ ] Тестировщик
- [ ] Документация

---

**Документ создан:** 2026-03-18
**Последнее обновление:** 2026-03-18
**Следующая ревизия:** После реализации Этапа 1
