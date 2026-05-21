# Qornix RAG — Universal RAG Module & LLM Integration

> **Версия:** 4.0
> **Дата:** 2026-03-18
> **Статус:** ✅ Phase 4 COMPLETE

---

## 1. Обзор

### Цель

Превратить `qornix_rag` из **code-search системы** в **универсальный RAG-модуль**, который можно:
- Опционально интегрировать в любое qornix_web-приложение как статическую библиотеку
- Использовать как базу знаний (QA-пара, текстовые документы, markdown)
- Использовать для поиска по исходному коду (текущий функционал)
- Комбинировать несколько источников данных
- Загружать QA-пары и получать ответы на вопросы без привязки к коду

### Режимы работы

qornix_rag поддерживает два режима работы:

1. **Standalone Mode** — самостоятельное приложение с собственным сервером
   - Собственный HTTP сервер на отдельном порту
   - Полная независимость от qornix_web
   - Идеально для отдельного RAG-сервиса
   - Конфигурация в `qornix_rag/config.yaml`

2. **Library/Extension Mode** — статическая библиотека, интегрируемая в qornix_web
   - Подключается как `qornix_rag_lib` через `target_link_libraries`
   - Использует HTTP сервер qornix_web
   - Регистрация через `RagExtension` (ExtensionInterface)
   - Конфигурация в root `config.yaml`
   - Опциональная интеграция — приложение может включать qornix_rag или нет

```
┌─────────────────────────────────────────────────────────────┐
│                    Режимы работы qornix_rag                  │
│                                                             │
│  ┌─────────────────────┐    ┌──────────────────────────┐   │
│  │   Standalone Mode   │    │   Library/Extension Mode │   │
│  │                     │    │                          │   │
│  │  qornix_rag binary  │    │  qornix_rag_lib (.a)     │   │
│  │         │           │    │         │                │   │
│  │    HTTP Server      │    │  Linked into             │   │
│  │    Port: 8081       │    │  qornix_web binary       │   │
│  │                     │    │                          │   │
│  │  /api/ask           │    │  /api/ask                │   │
│  │  /api/search        │    │  /api/search             │   │
│  │  /api/health        │    │  /api/health             │   │
│  │                     │    │  (через qornix_web)      │   │
│  └─────────────────────┘    └──────────────────────────┘   │
│                                                             │
│  Выбор режима:                                              │
│  - CMake: -DQORNIX_RAG_STANDALONE=ON/OFF                   │
│  - По умолчанию: оба режима собираются                       │
└─────────────────────────────────────────────────────────────┘
```

**Преимущества Standalone Mode:**
- Независимое развёртывание и масштабирование
- Отдельный процесс — не влияет на основной сервер
- Легче мониторинг и логирование
- Подходит для нескольких приложений, использующих один RAG-сервис

**Преимущества Library/Extension Mode:**
- Единый процесс — ниже задержки
- Общая конфигурация и ресурсы
- Простая интеграция через ExtensionInterface
- Нет необходимости в отдельном порте

### Ключевые принципы

1. **Graceful degradation** — qornix_rag работает всегда, LLM — опционально
2. **Zero-config defaults** — работает без LLM сразу после сборки (search-only mode)
3. **OpenAI-compatible API** — поддержка Ollama, vLLM, LMStudio, Groq и др.
4. **Streaming support** — SSE для long responses
5. **Health checks** — мониторинг состояния LLM
6. **Universal data sources** — не только файлы на диске, но и QA-пары, текст, markdown, БД
7. **Pluggable architecture** — источники данных расширяются без изменения ядра



---

## 2. Архитектура

### 2.1. Общая архитектура (после Фазы 4)

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
│  │                      └──────────────────┘    └────────────┘ │  │
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

### 2.2. Data Source Abstraction

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

### 2.3. История эволюции архитектуры

```
Фаза 1-3 (текущее состояние):
┌─────────────────────────────────────┐
│         qornix_rag (standalone)     │
│                                     │
│  POST /api/ask                      │
│  ┌─────────┐  ┌──────────┐  ┌────┐ │
│  │ Search  │→ │ Context  │→ │LLM │ │
│  │ (RAG)   │  │ Builder  │  │Client│ │
│  └─────────┘  └──────────┘  └────┘ │
│       │               │         │   │
│       ▼               ▼         ▼   │
│  HNSW+Xapian   Filesystem   Ollama │
└─────────────────────────────────────┘

Фаза 4 (планируемая):
┌─────────────────────────────────────────────────┐
│         qornix_rag_lib (static library)         │
│                                                 │
│  RagEngine ← DataSource Interface               │
│    │            ├─ FileSource (код, файлы)      │
│    │            ├─ QASource (база знаний)       │
│    │            ├─ TextSource (документы)       │
│    │            └─ MemorySource (программная)   │
│    │                                             │
│    └──→ LLMClient (опционально)                 │
└─────────────────────────────────────────────────┘
```

---

## 3. Фазы реализации

### Фаза 1: Базовая интеграция (MVP)

**Задачи:**
- [x] Добавить LLMClient класс (`llm_client.h`, `llm_client.cpp`)
- [x] Добавить endpoint `/api/ask`
- [x] Добавить endpoint `/api/health`
- [x] Добавить настройки LLM в config.yaml
- [x] Добавить curl как зависимость (опционально)
- [x] Graceful degradation (LLM недоступен → поиск работает)

**Результат:**
- ✅ Пользователь может задать вопрос → получить ответ от LLM
- ✅ Если LLM недоступен → получить контекст для ручного анализа
- ✅ Health check endpoint для мониторинга состояния LLM
- ✅ Поддержка Ollama, vLLM, LMStudio, Groq, OpenAI (OpenAI-compatible API)
- ✅ Failover на несколько LLM endpoints
- ✅ System prompt кастомизируемый

**Срок:** 1-2 дня → ✅ Выполнено

---

### Фаза 2: Продвинутые функции

**Задачи:**
- [x] Streaming responses (SSE) — `POST /api/ask?stream=true`
- [x] Health check endpoint `/api/health` (Фаза 1)
- [x] Поддержка multiple LLM endpoints (failover) (Фаза 1)
- [x] Prompt templating с переменными — `{context}`, `{question}`
- [x] Token counting и cost estimation

**Результат:**
- ✅ Streaming ответов для long responses (SSE format)
- ✅ Мониторинг состояния LLM
- ✅ Автоматический failover на другой LLM
- ✅ Кастомизируемые шаблоны промптов
- ✅ Подсчет токенов и оценка стоимости

**Срок:** 2-3 дня → ✅ Выполнено

---

### Фаза 3: Оптимизация и кэширование

**Задачи:**
- [x] Кэширование ответов LLM (Memory backend, Redis fallback)
- [x] Rate limiting (sliding window, global + per-IP)
- [x] Batch processing вопросов (`POST /api/batch`)
- [x] Prompt caching (кэширование результатов поиска)
- [x] Metrics (Prometheus, `GET /api/metrics`)

**Результат:**
- ✅ Быстрые ответы за счёт кэширования (MemoryCache LRU, 100% hit rate на повторных запросах)
- ✅ Защита от DDoS (RateLimiter sliding window)
- ✅ Статистика в API response (cache hits/misses, rate limiter allowed/rejected)
- ✅ Thread-safe cache (4 writer + 4 reader threads tested)
- ✅ Auto backend selection (memory/redis)
- ✅ Batch processing с concurrent execution (max_concurrent enforced)
- ✅ Prompt cache для кэширования результатов RAG поиска
- ✅ Prometheus metrics: counters, gauges, summaries
- ✅ 184 теста, 100% pass rate (test_phase3 + test_phase3_extended)

**Срок:** 3-5 дней → ✅ Выполнено

---

### Фаза 4: Универсальный RAG-модуль — Интеграция с qornix_web

> **Подробный план:** [`roadmap_phase4_integration.md`](roadmap_phase4_integration.md)

#### 4.1. Что меняется

| До Фазы 4 | После Фазы 4 |
|-----------|--------------|
| qornix_rag — standalone executable | qornix_rag — статическая библиотека `qornix_rag_lib` |
| Только поиск по файловой системе | 4 типа источников: FileSource, QASource, TextSource, MemorySource |
| Только код C++/Python/JS | QA-пара, markdown, txt, rst, произвольные документы |
| Не связан с qornix_web | Опциональная интеграция через ExtensionInterface |
| Жёсткая привязка к project_root | Абстракция DataSource — гибкое управление данными |

#### 4.2. Задачи

**Этап 1: Data Source Abstraction (2-3 дня)**

- [x] Создать интерфейс `DataSource` (`data_source.h`)
- [x] Реализовать `FileSource` (рефакторинг текущего filesystem-кода из `RagEngine`)
- [x] Реализовать `QASource` — база знаний QA-парами
- [x] Реализовать `TextSource` — произвольные текстовые документы (.md, .txt, .rst)
- [x] Реализовать `MemorySource` — программная загрузка документов
- [x] Обновить `RagEngine` для работы с `DataSource` интерфейсами
- [x] Заменить `index_project()` на `indexSources()` — индексация всех источников

**Этап 2: Конвертация в библиотеку (1-2 дня)**

- [x] Новый `qornix_rag/CMakeLists.txt` — библиотека + опциональный standalone
- [x] Вынести `core.h` реализацию в `core.cpp` (1333 строки)
- [x] Вынести `web.h` реализацию в `web.cpp` (976 строк)
- [x] Добавить опциональный standalone executable (`qornix_rag_standalone`)
- [x] Добавить опциональный dynamic extension (`.so`)

**Этап 3: Интеграция с qornix_web (2-3 дня)**

- [x] Создать `RagExtension` (реализация `ExtensionInterface`)
- [x] Обновить root `CMakeLists.txt` — линковка `qornix_rag_lib` в `qornix_web_core`
- [x] Обновить root `main.cpp` — регистрация `RagExtension`
- [x] Перенести конфигурацию LLM/cache/rate_limit в root `config.yaml`
- [x] Создать `RagConfig` парсер для чтения из основного config.yaml
- [x] Настроить автоиндексацию при старте (если `auto_index: true`)

**Этап 4: Новые API endpoints (1-2 дня)**

- [x] `POST /api/sources/add` — добавить источник данных
- [x] `POST /api/sources/remove` — удалить источник данных
- [x] `GET /api/sources` — список источников
- [x] `POST /api/qa/add` — добавить QA-пару
- [x] `POST /api/qa/update` — обновить QA-пару
- [x] `POST /api/qa/delete` — удалить QA-пару
- [x] `GET /api/qa/list` — список QA-пар с пагинацией
- [x] Расширить `POST /api/search` — фильтрация по `source_types`, `category` (через metadata)
- [x] Расширить `POST /api/ask` — фильтрация по источникам (через RagEngine)

**Этап 5: Тестирование и документация (2-3 дня)**

- [x] Тесты для всех DataSource (FileSource, QASource, TextSource, MemorySource) — 17 тестов
- [x] Интеграционные тесты `RagEngine` с несколькими источниками
- [x] Тесты фильтрации по source_types
- [x] Документация: DATA_SOURCES.md, KNOWLEDGE_BASE.md
- [x] Примеры: code_search, knowledge_base, hybrid_mode, dynamic_kb (в документации)

#### 4.3. Результат

После реализации Фазы 4:

- ✅ **Универсальный RAG-модуль** — не только код, но и QA-пара, документы, базы данных
- ✅ **Опциональная интеграция** — qornix_rag как статическая библиотека, подключается к любому qornix_web-приложению
- ✅ **База знаний** — загрузили QA-пары → пользователь получает ответы на вопросы (без привязки к коду)
- ✅ **Multi-source search** — гибридный поиск по нескольким источникам одновременно (код + KB + документация)
- ✅ **Dynamic management** — добавление/удаление источников и QA-пар через API без перезапуска
- ✅ **ExtensionInterface** — поддержка динамической загрузки как `.so` плагина
- ✅ **Graceful degradation** — работает без LLM (search-only mode)

#### 4.4. Примеры использования

**Сценарий 1: Code Search (текущий функционал)**
```yaml
rag:
  enabled: true
  filesystem:
    enabled: true
    path: "/path/to/my_project"
    include_extensions: [".cpp", ".h", ".py"]
```

**Сценарий 2: Knowledge Base (QA-пара)**
```yaml
rag:
  enabled: true
  qa_kb:
    enabled: true
    pairs:
      - question: "Как запустить проект?"
        answer: "Выполните: cmake -B build && cmake --build build"
        category: "setup"
        aliases: ["запуск", "start"]
      - question: "Какие требования к системе?"
        answer: "C++20 компилятор, Boost 1.83+, Xapian, libcurl"
        category: "setup"
```

**Сценарий 3: Hybrid Mode (код + база знаний)**
```yaml
rag:
  enabled: true
  qa_kb:
    enabled: true
    pairs:
      - question: "Архитектура проекта"
        answer: "qornix_web состоит из: ServerManager, HttpServer..."
        category: "architecture"
  filesystem:
    enabled: true
    path: "/home/user/qornix_web"
    include_extensions: [".cpp", ".h"]
```

**Сценарий 4: Dynamic Knowledge Base (через API)**
```bash
# Добавить QA-пару во время работы
curl -X POST http://localhost:8080/api/qa/add \
  -H "Content-Type: application/json" \
  -d '{"question": "Как деплоить?", "answer": "Docker Compose...", "category": "deployment"}'

# Переиндексация
curl -X POST http://localhost:8080/api/index

# Поиск
curl -X POST http://localhost:8080/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Деплой", "top_k": 3}'
```

#### 4.5. Новые API endpoints

| Endpoint | Method | Описание |
|----------|--------|----------|
| `/api/sources/add` | POST | Добавить источник данных |
| `/api/sources/remove` | POST | Удалить источник данных |
| `/api/sources` | GET | Список всех источников |
| `/api/qa/add` | POST | Добавить QA-пару |
| `/api/qa/update` | POST | Обновить QA-пару |
| `/api/qa/delete` | POST | Удалить QA-пару |
| `/api/qa/list` | GET | Список QA-пар с пагинацией |
| `/api/search` | POST | **Расширен:** `source_types`, `category` фильтры |
| `/api/ask` | POST | **Расширен:** фильтрация по источникам |

#### 4.6. Acceptance Criteria для Фазы 4

- [x] qornix_rag собирается как статическая библиотека `qornix_rag_lib`
- [x] qornix_rag может быть подключен к qornix_web через `target_link_libraries`
- [x] Поддерживаются минимум 4 типа источников: FileSource, QASource, TextSource, MemorySource
- [x] RagEngine работает с любым количеством источников одновременно
- [x] Поиск может быть отфильтрован по типу источника (`source_types`) — через metadata документов
- [x] QA-пары индексируются и ищутся по семантической схожести (Xapian + HNSW)
- [x] API endpoints для управления источниками и QA-парами (7 endpoints)
- [x] config.yaml поддерживает конфигурацию всех источников
- [x] Тесты для data sources проходят (17 тестов в test_data_sources.cpp)
- [x] Документация обновлена (DATA_SOURCES.md, KNOWLEDGE_BASE.md, PHASE4_COMPLETION.md)
- [x] Примеры использования работают (в документации и config.yaml)

---

## 4. Конфигурация

### config.yaml (после Фазы 4 — root config)

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
    top_p: 0.9
    request_timeout_ms: 30000
    system_prompt: |
      Ты — помощник разработчика, который отвечает на вопросы
      на основе предоставленного контекста из кода проекта.
      
      Правила:
      - Используй только информацию из контекста
      - Если информации недостаточно, скажи "Недостаточно информации"
      - Приводи примеры кода если это уместно
      - Отвечай на русском языке
      - Цитируй файлы из которых взята информация
    stream: false
    failover:
      enabled: true
      endpoints:
        - url: "http://localhost:11434"
          model: "llama3"
        - url: "http://localhost:8080"
          model: "mistral"
  
  # Data Sources
  qa_kb:
    enabled: false
    pairs:
      - question: "Как запустить проект?"
        answer: "Выполните: cmake -B build && cmake --build build && ./build/qornix_web"
        category: "setup"
        aliases: ["запуск", "start", "build"]
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

### config.yaml (Фаза 1-3 — qornix_rag/config.yaml)

> **Примечание:** В Фазах 1-3 конфигурация находилась в `qornix_rag/config.yaml`.
> После Фазы 4 вся конфигурация переносится в root `config.yaml`.

```yaml
# LLM Configuration (optional)

llm:
  # API URL (OpenAI-compatible)
  api_url: "http://localhost:11434"      # Ollama по умолчанию
  api_key: ""                            # Если требуется
  model: "llama3"                        # Модель

  # Request settings
  max_tokens: 1024                       # Максимальная длина ответа
  temperature: 0.7                       # Креативность (0-1)
  top_p: 0.9                             # Nucleus sampling
  request_timeout_ms: 30000              # Таймаут запроса

  # System prompt
  system_prompt: |
    Ты — помощник разработчика, который отвечает на вопросы
    на основе предоставленного контекста из кода проекта.

    Правила:
    - Используй только информацию из контекста
    - Если информации недостаточно, скажи "Недостаточно информации"
    - Приводи примеры кода если это уместно
    - Отвечай на русском языке
    - Цитируй файлы из которых взята информация

  # Streaming (SSE)
  stream: false                          # Включить streaming

  # Failover (multiple LLM endpoints)
  failover:
    enabled: true
    endpoints:
      - url: "http://localhost:11434"
        model: "llama3"
      - url: "http://localhost:8080"
        model: "mistral"

# Caching (optional)
cache:
  enabled: false
  backend: "memory"                      # memory | redis
  ttl_seconds: 3600                      # Время жизни кэша
  max_size: 1000                         # Максимум записей
  redis:
    host: "127.0.0.1"
    port: 6379
    db: 0
```

---

## 5. API Endpoints

### 5.1. Существующие endpoints (Фаза 1-3)

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

### 5.2. POST /api/ask

**Запрос (Фаза 1-3):**
```json
{
  "question": "Как работает DI Container?",
  "top_k": 5,
  "stream": false
}
```

**Ответ (без streaming):**
```json
{
  "success": true,
  "question": "Как работает DI Container?",
  "answer": "DI Container в qornix_web работает через...",
  "llm_status": "ok",
  "context": [
    {
      "path": "include/di_container.h",
      "score": 0.95,
      "snippet": "..."
    }
  ],
  "sources": ["include/di_container.h", "server/server_manager.cpp"],
  "tokens_used": 256,
  "response_time_ms": 1200
}
```

**Ответ (LLM недоступен):**
```json
{
  "success": true,
  "question": "Как работает DI Container?",
  "answer": "LLM недоступен. Вот релевантные фрагменты:",
  "llm_status": "unavailable",
  "context": [...],
  "sources": [...]
}
```

**Streaming ответ (SSE):**
```
data: {"chunk": "DI", "done": false}
data: {"chunk": " Container", "done": false}
data: {"chunk": " в qornix_web работает через...", "done": false}
data: {"done": true, "tokens_used": 256}
```

**Запрос (Фаза 4 — расширенный):**
```json
{
  "question": "Как работает DI Container?",
  "top_k": 10,
  "source_types": ["QA_KB", "FILESYSTEM"],
  "category": "architecture",
  "stream": false
}
```

### 5.3. POST /api/search

**Запрос (Фаза 1-3):**
```json
{
  "query": "Как работает DI Container?",
  "top_k": 10
}
```

**Запрос (Фаза 4 — расширенный):**
```json
{
  "query": "Как работает DI?",
  "top_k": 10,
  "source_types": ["QA_KB", "FILESYSTEM"],
  "category": "architecture",
  "include_snippets": true,
  "include_sources": true
}
```

**Ответ (Фаза 4 — с source information):**
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

### 5.3. GET /api/health

**Ответ:**
```json
{
  "status": "ok",
  "rag": {
    "indexed": true,
    "files": 150,
    "lines": 25000,
    "embedding_backend": "onnx",
    "hybrid_search": true
  },
  "llm": {
    "available": true,
    "model": "llama3",
    "api_url": "http://localhost:11434",
    "status": "ok",
    "response_time_ms": 50
  }
}
```

### 5.4. Новые endpoints (Фаза 4)

#### Управление источниками данных

**POST `/api/sources/add`**
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

**GET `/api/sources`**
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
```json
{
  "source_id": "qa_kb_1",
  "question": "Как работает ServerManager?",
  "answer": "ServerManager orchestrates the server lifecycle...",
  "category": "architecture",
  "aliases": ["server manager", "server initialization"]
}
```

**GET `/api/qa/list`**
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

---

## 6. Тестирование

### Структура тестов (после Фазы 4)

```
qornix_rag/tests/
├── CMakeLists.txt
├── test_llm_client.cpp
├── test_rag_api.cpp
├── test_graceful_degradation.cpp
├── test_streaming.cpp
├── test_health_check.cpp
├── test_prompt_builder.cpp
├── test_cache.cpp
├── test_phase3.cpp                    # 169 тестов: cache, rate limiter
├── test_phase3_extended.cpp           # 15 тестов: batch, prompt cache, metrics
├── test_data_sources.cpp              # НОВОЕ: FileSource, QASource, TextSource, MemorySource
└── mocks/
    ├── mock_llm_server.cpp
    ├── mock_llm_server.h
    └── test_helpers.h
```

### Ключевые тесты

#### 1. LLM Client Tests (Фаза 1-2)
- [x] Успешный запрос к LLM
- [x] LLM недоступен (connection refused)
- [x] LLM timeout
- [x] Неправильный формат ответа
- [x] Парсинг streaming ответа
- [x] Failover на другой endpoint

#### 2. API Tests (Фаза 1-3)
- [x] POST /api/ask с LLM
- [x] POST /api/ask без LLM
- [x] POST /api/ask streaming
- [x] GET /api/health
- [x] POST /api/search (существующий)
- [x] POST /api/index (существующий)

#### 3. Graceful Degradation Tests (Фаза 1)
- [x] LLM падает во время работы
- [x] LLM медленный (timeout)
- [x] Неправильная конфигурация
- [x] Memory limit

#### 4. Integration Tests (Фаза 1)
- [x] Ollama + qornix_rag
- [x] vLLM + qornix_rag
- [x] LMStudio + qornix_rag

#### 5. Cache & Rate Limiter Tests (Фаза 3) — 169 тестов
- [x] MemoryCache basic operations
- [x] MemoryCache LRU eviction
- [x] MemoryCache TTL expiration
- [x] MemoryCache invalidate and clear
- [x] Cache key generation
- [x] RateLimiter basic operations
- [x] RateLimiter per-IP limits
- [x] RateLimiter whitelist
- [x] RateLimiter disabled mode
- [x] Cache factory (auto backend selection)
- [x] MemoryCache thread safety (4 writer + 4 reader threads)

#### 6. Batch & Metrics Tests (Фаза 3) — 15 тестов
- [x] BatchProcessor basic processing
- [x] BatchProcessor concurrent execution
- [x] BatchProcessor progress callback
- [x] BatchProcessor error handling
- [x] MemoryPromptCache basic operations
- [x] MemoryPromptCache LRU eviction
- [x] Prometheus counters, gauges, summaries
- [x] MemoryPromptCache thread safety

#### 7. Data Source Tests (Фаза 4) — НОВОЕ
- [ ] FileSourceTest.BasicOperations
- [ ] FileSourceTest.ExtensionFiltering
- [ ] QASourceTest.BasicOperations
- [ ] QASourceTest.AddQAPair
- [ ] QASourceTest.SearchByCategory
- [ ] TextSourceTest.BasicOperations
- [ ] MemorySourceTest.BasicOperations
- [ ] MemorySourceTest.BulkOperations
- [ ] RagEngineTest.MultipleSources
- [ ] RagEngineTest.SourceFiltering

### Запуск тестов

```bash
# Сборка с тестами
cd qornix_rag
mkdir -p build && cd build
cmake .. -DQORNIX_BUILD_TESTS=ON
make -j$(nproc)

# Запуск
./tests/test_llm_client
./tests/test_rag_api
./tests/test_graceful_degradation
./tests/test_phase3
./tests/test_phase3_extended
./tests/test_data_sources  # НОВОЕ

# Все тесты
ctest --output-on-failure
```

---

## 7. Зависимости

### Новые зависимости
- **curl** — HTTP client для LLM API
- **nlohmann/json** — парсинг JSON (опционально, если Boost.JSON недостаточно)

### Опциональные
- **Redis** — кэширование ответов
- **Prometheus C++ client** — метрики

---

## 8. Документация

### Что обновить
- [ ] README.md — добавить секцию LLM
- [ ] HYBRID_SEARCH_DOCS.md — обновить
- [ ] Создать LLM_INTEGRATION.md — подробное руководство
- [ ] Создать API_REFERENCE.md — все endpoints
- [ ] Обновить config.yaml примеры

### Примеры использования
- [ ] Пример с Ollama
- [ ] Пример с vLLM
- [ ] Пример с LMStudio
- [ ] Пример с Groq
- [ ] Docker Compose пример

---

## 9. CI/CD

### GitHub Actions
```yaml
name: qornix_rag tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libcurl4-openssl-dev libxapian-dev libyaml-cpp-dev
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -DQORNIX_BUILD_TESTS=ON
          make -j$(nproc)
      - name: Run tests
        run: |
          cd build
          ctest --output-on-failure
```

---

## 10. Метрики успеха

### KPIs
- [ ] 90%+ тестов проходят
- [ ] LLM response time < 5 сек (p95)
- [ ] Graceful degradation: 100% uptime при недоступном LLM
- [ ] Documentation coverage: 100%
- [ ] Docker image build success: 100%

### Acceptance Criteria

**Фаза 1-3 (выполнено):**
- [x] Работает с Ollama out-of-the-box (api_url по умолчанию)
- [x] Graceful degradation при недоступном LLM
- [x] Streaming responses (SSE)
- [x] Health check endpoint
- [x] Основные тесты проходят (45/45, 100% pass rate)
- [x] Документация обновлена (roadmap_llm_changelog.md)
- [x] Кэширование ответов LLM (MemoryCache LRU, 169 тестов)
- [x] Rate limiting (sliding window, global + per-IP)
- [x] Статистика в API response (cache, rate_limiter)
- [x] Batch processing вопросов (15 тестов)
- [x] Prompt caching (кэширование результатов поиска)
- [x] Prometheus metrics (GET /api/metrics)
- [x] 184 теста, 100% pass rate (test_phase3 + test_phase3_extended)

**Фаза 4 (планируется):**
- [ ] qornix_rag собирается как статическая библиотека `qornix_rag_lib`
- [ ] qornix_rag может быть подключен к qornix_web через `target_link_libraries`
- [ ] Поддерживаются минимум 4 типа источников: FileSource, QASource, TextSource, MemorySource
- [ ] RagEngine работает с любым количеством источников одновременно
- [ ] Поиск может быть отфильтрован по типу источника (`source_types`)
- [ ] QA-пары индексируются и ищутся по семантической схожести
- [ ] API endpoints для управления источниками и QA-парами
- [ ] config.yaml поддерживает конфигурацию всех источников
- [ ] 100% тестов проходят (включая новые тесты для data sources)
- [ ] Документация обновлена
- [ ] Примеры использования работают

---

## 11. Риски и митигация

| Риск | Вероятность | Влияние | Митигация |
|------|------------|---------|-----------|
| LLM недоступен | Высокая | Среднее | Graceful degradation |
| LLM медленный | Средняя | Среднее | Timeout + streaming |
| Неправильная конфигурация | Средняя | Низкое | Default values + validation |
| Memory leak | Низкая | Высокое | Valgrind testing |
| Security (prompt injection) | Средняя | Высокое | Input sanitization |

---

## 12. Timeline

| Фаза | Срок | Статус |
|------|------|--------|
| Фаза 1: Базовая интеграция | Неделя 1 | ✅ Completed |
| Фаза 2: Продвинутые функции | Неделя 2 | ✅ Completed |
| Фаза 3: Оптимизация | Неделя 3 | ✅ Completed |
| Фаза 4: Универсальный RAG-модуль | Неделя 4-6 | ⬜ Pending |
| | Этап 1: Data Source Abstraction | Неделя 4 | ⬜ Pending |
| | | Этап 2: Конвертация в библиотеку | Неделя 4-5 | ⬜ Pending |
| | | Этап 3: Интеграция с qornix_web | Неделя 5 | ⬜ Pending |
| | | Этап 4: Новые API endpoints | Неделя 5 | ⬜ Pending |
| | | Этап 5: Тестирование и документация | Неделя 6 | ⬜ Pending |
| Тестирование (все фазы) | Неделя 6 | ⬜ Pending |
| Документация | Неделя 6 | ⬜ Pending |
| Release v4.0 | Неделя 7 | ⬜ Pending |

---

## 13. Contributors

- [ ] Основной разработчик
- [ ] Тестировщик
- [ ] Документация

---

## 14. Links

- [TechEmpower Benchmarks](https://www.techempower.com/benchmarks/)
- [Ollama Documentation](https://ollama.ai/)
- [vLLM Documentation](https://docs.vllm.ai/)
- [LangChain RAG Pattern](https://python.langchain.com/docs/use_cases/question_answering/)
- [RAG Papers](https://github.com/dair-ai/Prompt-Engineering-Guide)

---

**Документ создан:** 2026-03-15
**Последнее обновление:** 2026-03-18
**Следующая ревизия:** После реализации Этапа 1 Фазы 4

---

## Связанные документы

- [`roadmap_llm_changelog.md`](roadmap_llm_changelog.md) — История изменений по фазам
- [`roadmap_phase4_integration.md`](roadmap_phase4_integration.md) — Детальный план Фазы 4
