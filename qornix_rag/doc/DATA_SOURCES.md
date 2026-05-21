# Data Sources — Qornix RAG

> **Версия:** 1.0
> **Дата:** 2026-03-18
> **Статус:** Phase 4

---

## 1. Обзор

`qornix_rag` поддерживает **универсальные источники данных** через абстракцию `DataSource`. Это позволяет использовать RAG-модуль не только для поиска по исходному коду, но и как:

- **Базу знаний** (QA-пары)
- **Систему управления документами** (markdown, txt, rst)
- **Гибридную систему** (код + документация + знания)

### Ключевые принципы

1. **Плагинная архитектура** — источники данных расширяются без изменения ядра
2. **Множественные источники** — можно комбинировать несколько источников одновременно
3. **Динамическое управление** — добавление/удаление источников через API без перезапуска
4. **Единый интерфейс** — все источники реализуют `DataSource` интерфейс

---

## 2. Архитектура

### 2.1. DataSource Interface

```cpp
class DataSource {
public:
    virtual ~DataSource() = default;

    // Основные методы
    virtual std::vector<Document> getDocuments() = 0;
    virtual void addDocument(const Document& doc) = 0;
    virtual void removeDocument(const std::string& id) = 0;

    // Методы идентификации
    virtual DataSourceType getType() const = 0;
    virtual size_t count() const = 0;
    virtual std::string getId() const = 0;
    virtual std::string getName() const = 0;

    // Lifecycle
    virtual bool initialize() = 0;
    virtual void cleanup() = 0;

    // Progress reporting
    virtual void setProgressCallback(ProgressCallback callback);
};
```

### 2.2. DataSourceType Enum

```cpp
enum class DataSourceType {
    FILESYSTEM,   // Файлы на диске (код, конфиги)
    QA_KB,        // База знаний QA-парами
    TEXT_DOCS,    // Текстовые документы (.md, .txt, .rst)
    DATABASE,     // База данных (заглушка)
    MEMORY,       // Программная загрузка
    CUSTOM        // Пользовательский источник
};
```

---

## 3. Реализации DataSource

### 3.1. FileSource — Файловая система

**Файлы:** `file_source.h`, `file_source.cpp`

Сканирует директории и индексирует файлы по расширению.

#### Конфигурация

```cpp
struct Config {
    std::string root_path;                          // Корневая директория
    std::vector<std::string> include_extensions;    // Включаемые расширения
    std::vector<std::string> exclude_directories;   // Исключаемые директории
    size_t max_file_size_kb = 512;                  // Макс. размер файла
    bool recursive = true;                          // Рекурсивное сканирование
};
```

#### Пример использования

```cpp
FileSource::Config config;
config.root_path = "/path/to/project";
config.include_extensions = {".cpp", ".h", ".py", ".md"};
config.exclude_directories = {".git", "build", "__pycache__"};

auto source = std::make_shared<FileSource>(config);
engine.addDataSource(source);
```

#### Поддерживаемые расширения

| Тип | Расширения |
|-----|-----------|
| **Исходный код** | `.cpp`, `.h`, `.hpp`, `.cc`, `.cxx`, `.java`, `.py`, `.js`, `.ts`, `.go`, `.rs`, `.swift`, `.kt` |
| **Конфигурации** | `.yaml`, `.yml`, `.json`, `.xml`, `.toml`, `.ini`, `.cfg` |
| **Документы** | `.md`, `.markdown`, `.rst`, `.adoc`, `.txt` |

---

### 3.2. QASource — База знаний QA-парами

**Файлы:** `qa_source.h`, `qa_source.cpp`

Хранит вопросы-ответы и ищет по семантической схожести.

#### Структура QA-пары

```cpp
struct QAPair {
    std::string id;                      // Уникальный идентификатор
    std::string question;                // Текст вопроса
    std::string answer;                  // Текст ответа
    std::string category = "general";    // Категория/тег
    std::vector<std::string> aliases;    // Альтернативные формулировки
    std::map<std::string, std::string> metadata; // Доп. метаданные
};
```

#### Конфигурация

```cpp
struct Config {
    std::vector<QAPair> pairs;           // Начальные QA-пары
    std::string name = "QA Knowledge Base";
    std::string source_id = "qa_default";
    bool enable_fuzzy_search = true;
};
```

#### Пример использования

```cpp
QASource::Config config;
config.name = "Project Knowledge Base";
config.source_id = "prod_kb";
config.pairs = {
    {"qa_001", "Как запустить проект?", 
     "Выполните: cmake -B build && cmake --build build && ./build/qornix_web",
     "setup", {"запуск", "start", "build"}},
    {"qa_002", "Какие требования к системе?",
     "C++20 компилятор, Boost 1.83+, Xapian, libcurl",
     "setup", {"требования", "requirements"}}
};

auto source = std::make_shared<QASource>(config);
engine.addDataSource(source);
```

#### API методы

| Метод | Описание |
|-------|----------|
| `addQAPair(const QAPair& pair)` | Добавить QA-пару |
| `removeQAPair(const std::string& id)` | Удалить QA-пару по ID |
| `findQAPair(const std::string& id)` | Найти QA-пару по ID |
| `searchByCategory(const std::string& category)` | Найти по категории |
| `getAllPairs() const` | Получить все пары |
| `updateQAPair(const QAPair& pair)` | Обновить QA-пару |
| `loadFromJson(const std::string& json_str)` | Загрузить из JSON |
| `toJson() const` | Сохранить в JSON |

---

### 3.3. TextSource — Текстовые документы

**Файлы:** `text_source.h`, `text_source.cpp`

Загружает и индексирует произвольные текстовые документы.

#### Конфигурация

```cpp
struct Config {
    std::vector<std::string> file_paths;      // Явный список файлов
    std::string directory_path;               // Или директория для сканирования
    std::vector<std::string> extensions;      // Включаемые расширения
    bool recursive = true;                    // Рекурсивное сканирование
    size_t max_file_size_kb = 1024;           // Макс. размер файла (больше для документов)
};
```

#### Пример использования

```cpp
TextSource::Config config;
config.directory_path = "/path/to/documentation";
config.extensions = {".md", ".txt", ".rst", ".adoc"};
config.max_file_size_kb = 1024;

auto source = std::make_shared<TextSource>(config);
engine.addDataSource(source);
```

#### API методы

| Метод | Описание |
|-------|----------|
| `addFile(const std::string& path)` | Добавить файл динамически |
| `removeFile(const std::string& path)` | Удалить файл |
| `addFiles(const std::vector<std::string>& paths)` | Добавить несколько файлов |
| `setDirectoryPath(const std::string& path)` | Изменить директорию |
| `setExtensions(const std::vector<std::string>& ext)` | Изменить расширения |

---

### 3.4. MemorySource — Программная загрузка

**Файлы:** `memory_source.h`, `memory_source.cpp`

Документы добавляются программно. Полезно для тестирования, динамического контента, временных баз знаний.

#### Конфигурация

```cpp
struct Config {
    std::string name;
    bool allow_duplicates = false;
};
```

#### Пример использования

```cpp
MemorySource::Config config;
config.name = "Dynamic Content";
config.allow_duplicates = false;

auto source = std::make_shared<MemorySource>(config);

// Добавить документ
Document doc;
doc.path = "dynamic.txt";
doc.relative_path = "dynamic.txt";
doc.content = "Динамический контент";
doc.type = "text";
doc.language = "text";
doc.size_bytes = doc.content.size();

source->addDocument(doc);
engine.addDataSource(source);
```

#### API методы

| Метод | Описание |
|-------|----------|
| `addDocuments(const std::vector<Document>& docs)` | Пакетное добавление |
| `clear()` | Очистить все документы |
| `contains(const std::string& id)` | Проверить наличие |
| `getDocument(const std::string& id)` | Получить документ по ID |

---

## 4. Интеграция с RagEngine

### 4.1. Добавление источников

```cpp
RagEngine engine(config);

// Добавить несколько источников
auto file_source = std::make_shared<FileSource>(file_config);
auto qa_source = std::make_shared<QASource>(qa_config);
auto text_source = std::make_shared<TextSource>(text_config);

engine.addDataSource(file_source);
engine.addDataSource(qa_source);
engine.addDataSource(text_source);
```

### 4.2. Индексация

```cpp
// Индексация всех источников
engine.indexSources();

// Получение статистики
auto stats = engine.getStatistics();
std::cout << "Total files: " << stats.total_files << std::endl;
```

### 4.3. Поиск с фильтрацией

```cpp
// Поиск по всем источникам
auto results = engine.search("Как работает DI?", 10);

// Поиск с фильтрацией по типу источника
auto results = engine.search("Как работает DI?", 10, 
    {DataSourceType::QA_KB, DataSourceType::FILESYSTEM});
```

### 4.4. Управление источниками

```cpp
// Удаление источника
engine.removeDataSourceByName("qa_default");

// Получение списка источников
auto sources = engine.getDataSources();
for (const auto& src : sources) {
    std::cout << src->getName() << " (" 
              << data_source_type_to_string(src->getType()) 
              << "): " << src->count() << " docs" << std::endl;
}
```

---

## 5. API Endpoints (Phase 4)

### 5.1. Управление источниками данных

#### GET `/api/sources`

Список всех источников данных.

**Ответ:**
```json
{
  "success": true,
  "sources": [
    {
      "id": "qa_default",
      "type": "QA_KB",
      "name": "Project Knowledge Base",
      "document_count": 15
    },
    {
      "id": "fs_1",
      "type": "FILESYSTEM",
      "name": "Project Code",
      "document_count": 150
    }
  ],
  "count": 2
}
```

#### POST `/api/sources/add`

Добавить источник данных.

**Request:**
```json
{
  "source_type": "qa_kb",
  "name": "New QA Source",
  "source_id": "qa_new",
  "pairs": [
    {
      "question": "What is this?",
      "answer": "This is a new QA source.",
      "category": "test"
    }
  ]
}
```

**Ответ:**
```json
{
  "success": true,
  "message": "Data source added",
  "source_id": "qa_new",
  "source_type": "QA_KB",
  "document_count": 1
}
```

#### POST `/api/sources/remove`

Удалить источник данных.

**Request:**
```json
{
  "source_id": "qa_new"
}
```

**Ответ:**
```json
{
  "success": true,
  "message": "Data source removed",
  "source_id": "qa_new"
}
```

### 5.2. Управление QA-парами

#### POST `/api/qa/add`

Добавить QA-пару.

**Request:**
```json
{
  "source_id": "qa_default",
  "question": "Как деплоить?",
  "answer": "Используйте Docker Compose.",
  "category": "deployment"
}
```

**Ответ:**
```json
{
  "success": true,
  "message": "QA pair added",
  "pair_id": "qa_default_1234567890",
  "source_id": "qa_default"
}
```

#### POST `/api/qa/update`

Обновить QA-пару.

**Request:**
```json
{
  "pair_id": "qa_default_1234567890",
  "answer": "Обновленный ответ."
}
```

#### POST `/api/qa/delete`

Удалить QA-пару.

**Request:**
```json
{
  "pair_id": "qa_default_1234567890"
}
```

#### GET `/api/qa/list`

Список QA-пар с пагинацией.

**Query Parameters:**
- `source_id` — ID источника (опционально)
- `page` — номер страницы (по умолчанию 1)
- `per_page` — количество на странице (по умолчанию 20)

**Ответ:**
```json
{
  "success": true,
  "source_id": "qa_default",
  "total": 15,
  "page": 1,
  "per_page": 20,
  "pairs": [
    {
      "id": "qa_001",
      "question": "Как запустить проект?",
      "category": "setup",
      "aliases": ["запуск", "start"]
    }
  ]
}
```

---

## 6. Сценарии использования

### 6.1. Code Search (текущий функционал)

```yaml
rag:
  enabled: true
  filesystem:
    enabled: true
    path: "/path/to/my_project"
    include_extensions: [".cpp", ".h", ".py"]
```

### 6.2. Knowledge Base (QA-пара)

```yaml
rag:
  enabled: true
  qa_kb:
    enabled: true
    name: "Project KB"
    source_id: "prod_kb"
    pairs:
      - question: "Как запустить проект?"
        answer: "cmake -B build && cmake --build build"
        category: "setup"
        aliases: ["запуск", "start"]
```

### 6.3. Hybrid Mode (код + база знаний)

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

### 6.4. Dynamic Knowledge Base (через API)

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

---

## 7. Тестирование

### 7.1. Запуск тестов

```bash
cd build
ctest -R test_data_sources -V
```

### 7.2. Что тестируется

| Тест | Охват |
|------|-------|
| `memory_source_basic` | Создание, инициализация, тип |
| `memory_source_add_document` | Добавление, дубликаты |
| `memory_source_remove_document` | Удаление |
| `memory_source_get_documents` | Получение документов |
| `memory_source_clear` | Очистка |
| `qa_source_basic` | Создание, инициализация, тип |
| `qa_source_add_pair` | Добавление QA-пары |
| `qa_source_find_pair` | Поиск по ID |
| `qa_source_remove_pair` | Удаление |
| `qa_source_search_by_category` | Поиск по категории |
| `qa_source_get_all_pairs` | Получение всех пар |
| `qa_source_update_pair` | Обновление |
| `qa_source_to_document` | Конвертация в Document |
| `data_source_type_to_string` | Конвертация типа |
| `rag_engine_add_data_source` | Добавление в RagEngine |
| `rag_engine_remove_data_source` | Удаление из RagEngine |
| `rag_engine_multi_source` | Множественные источники |

---

## 8. Известные ограничения

1. **DATABASE источник — заглушка** — пока не реализован
2. **Нестандартные кодировки** — поддерживается только UTF-8
3. **Бинарные файлы** — автоматически пропускаются
4. **Нет incremental indexing** — полная переиндексация при каждом вызове `indexSources()`
5. **Нет event-driven updates** — документы обновляются только при переиндексации

---

## 9. Планы развития

- [ ] Database DataSource (SQLite, PostgreSQL)
- [ ] Incremental indexing (только измененные файлы)
- [ ] Event-driven updates (inotify, FileSystemWatcher)
- [ ] Custom DataSource API для пользователей
- [ ] Web-based document upload
- [ ] Document versioning

---

**Last updated:** 2026-03-18
**Current version:** v1.0 (Phase 4)
