# Knowledge Base — Qornix RAG

> **Версия:** 1.0
> **Дата:** 2026-03-18
> **Статус:** Phase 4

---

## 1. Обзор

`qornix_rag` поддерживает использование в качестве **базы знаний** через `QASource` — источник данных, который хранит и ищет QA-пары (вопрос-ответ).

Это позволяет:
- Создавать базу знаний проекта без привязки к исходному коду
- Динамически добавлять/обновлять/удалять знания через API
- Искать ответы по семантической схожести вопросов
- Комбинировать базу знаний с поиском по коду (hybrid mode)

---

## 2. Быстрый старт

### 2.1. Конфигурация

Добавьте секцию `rag.qa_kb` в `config.yaml`:

```yaml
rag:
  enabled: true

  qa_kb:
    enabled: true
    name: "Project Knowledge Base"
    source_id: "prod_kb"
    pairs:
      - question: "Как запустить проект?"
        answer: "Выполните: cmake -B build && cmake --build build && ./build/qornix_web"
        category: "setup"
        aliases: ["запуск", "start", "build"]
      - question: "Какие требования к системе?"
        answer: "C++20 компилятор, Boost 1.83+, Xapian, libcurl, yaml-cpp"
        category: "setup"
      - question: "Как работает DI Container?"
        answer: "DI Container управляет жизненным циклом объектов и их зависимостями"
        category: "architecture"
        aliases: ["di", "dependency injection"]
```

### 2.2. Запуск

```bash
cmake -B build -DQORNIX_BUILD_RAG=ON
cmake --build build
./build/qornix_web
```

### 2.3. Проверка

```bash
# Поиск по базе знаний
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "Как запустить?", "top_k": 3}'

# Ответ от LLM (если настроен)
curl -X POST http://localhost:8008/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "Как запустить проект?", "top_k": 3}'
```

---

## 3. Структура QA-пары

### 3.1. Поля

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

### 3.2. Примеры

#### Базовая QA-пара

```json
{
  "id": "qa_001",
  "question": "Что такое RAG?",
  "answer": "RAG (Retrieval-Augmented Generation) — это паттерн, который комбинирует поиск по базе знаний с генерацией ответов LLM.",
  "category": "ai"
}
```

#### QA-пара с алиасами

```json
{
  "id": "qa_002",
  "question": "Как деплоить проект?",
  "answer": "Используйте Docker Compose: docker-compose up -d",
  "category": "deployment",
  "aliases": ["деплой", "deploy", "docker", "развертывание"]
}
```

#### QA-пара с метаданными

```json
{
  "id": "qa_003",
  "question": "Какая версия C++ требуется?",
  "answer": "C++20 или новее (GCC 11+, Clang 12+, MSVC 19.2+)",
  "category": "requirements",
  "metadata": {
    "author": "admin",
    "reviewed_by": "tech_lead",
    "last_updated": "2026-03-18",
    "version": "1.0"
  }
}
```

---

## 4. API для управления базой знаний

### 4.1. Добавление QA-пары

**POST `/api/qa/add`**

```bash
curl -X POST http://localhost:8008/api/qa/add \
  -H "Content-Type: application/json" \
  -d '{
    "source_id": "prod_kb",
    "question": "Как настроить логирование?",
    "answer": "Редактируйте секцию logging в config.yaml",
    "category": "configuration",
    "aliases": ["лог", "log"]
  }'
```

**Ответ:**
```json
{
  "success": true,
  "message": "QA pair added",
  "pair_id": "prod_kb_1711000000",
  "source_id": "prod_kb"
}
```

### 4.2. Обновление QA-пары

**POST `/api/qa/update`**

```bash
curl -X POST http://localhost:8008/api/qa/update \
  -H "Content-Type: application/json" \
  -d '{
    "pair_id": "prod_kb_1711000000",
    "answer": "Обновленный ответ с подробной инструкцией"
  }'
```

### 4.3. Удаление QA-пары

**POST `/api/qa/delete`**

```bash
curl -X POST http://localhost:8008/api/qa/delete \
  -H "Content-Type: application/json" \
  -d '{"pair_id": "prod_kb_1711000000"}'
```

### 4.4. Список QA-пар

**GET `/api/qa/list`**

```bash
# Первая страница (20 пар)
curl "http://localhost:8008/api/qa/list"

# Конкретная страница
curl "http://localhost:8008/api/qa/list?page=2&per_page=10"

# Конкретный источник
curl "http://localhost:8008/api/qa/list?source_id=prod_kb"
```

**Ответ:**
```json
{
  "success": true,
  "source_id": "prod_kb",
  "total": 45,
  "page": 1,
  "per_page": 20,
  "pairs": [
    {
      "id": "qa_001",
      "question": "Как запустить проект?",
      "category": "setup",
      "aliases": ["запуск", "start"]
    },
    {
      "id": "qa_002",
      "question": "Какие требования к системе?",
      "category": "setup",
      "aliases": ["требования", "requirements"]
    }
  ]
}
```

### 4.5. Добавление источника данных

**POST `/api/sources/add`**

```bash
curl -X POST http://localhost:8008/api/sources/add \
  -H "Content-Type: application/json" \
  -d '{
    "source_type": "qa_kb",
    "name": "Support KB",
    "source_id": "support_kb",
    "pairs": [
      {
        "question": "Как сбросить пароль?",
        "answer": "Используйте /api/auth/reset-password",
        "category": "support"
      }
    ]
  }'
```

### 4.6. Удаление источника

**POST `/api/sources/remove`**

```bash
curl -X POST http://localhost:8008/api/sources/remove \
  -H "Content-Type: application/json" \
  -d '{"source_id": "support_kb"}'
```

### 4.7. Список источников

**GET `/api/sources`**

```bash
curl http://localhost:8008/api/sources
```

**Ответ:**
```json
{
  "success": true,
  "sources": [
    {
      "id": "prod_kb",
      "type": "QA_KB",
      "name": "Project Knowledge Base",
      "document_count": 45
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

---

## 5. Сценарии использования

### 5.1. Техническая документация

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "Как добавить новый endpoint?"
        answer: "Создайте handler в handlers/ и зарегистрируйте маршрут в routes.h"
        category: "development"
        aliases: ["endpoint", "route", "маршрут"]
      
      - question: "Как работает middleware?"
        answer: "Middleware выполняется до и после handler. Используйте addMiddleware()"
        category: "development"
```

### 5.2. FAQ поддержка

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "Как исправить ошибку компиляции?"
        answer: "Убедитесь, что все зависимости установлены: ./depend_install.sh"
        category: "support"
        aliases: ["ошибка", "compile", "build error"]
      
      - question: "Почему сервер не запускается?"
        answer: "Проверьте, что порт 8008 свободен, и config.yaml существует"
        category: "support"
        aliases: ["не запускается", "port", "startup"]
```

### 5.3. Onboarding новых разработчиков

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "Как начать работать над проектом?"
        answer: "1. Клонируйте репозиторий\n2. Установите зависимости\n3. Соберите проект\n4. Запустите тесты"
        category: "onboarding"
        aliases: ["начать", "first steps", "setup"]
      
      - question: "Какой код-ревью процесс?"
        answer: "Создайте PR, получите approval от 2+maintainers, пройдите CI"
        category: "onboarding"
```

### 5.4. Hybrid Mode (код + база знаний)

```yaml
rag:
  enabled: true
  
  # База знаний
  qa_kb:
    enabled: true
    pairs:
      - question: "Архитектура проекта"
        answer: "qornix_web состоит из ServerManager, HttpServer, DI Container"
        category: "architecture"
  
  # Поиск по коду
  filesystem:
    enabled: true
    path: "/path/to/project"
    include_extensions: [".cpp", ".h"]
```

---

## 6. Программное использование

### 6.1. C++ API

```cpp
#include "qa_source.h"

// Создание базы знаний
QASource::Config config;
config.name = "My Knowledge Base";
config.source_id = "my_kb";

auto qa_source = std::make_shared<QASource>(config);

// Добавление QA-пары
QASource::QAPair pair;
pair.id = "qa_001";
pair.question = "Что такое DI?";
pair.answer = "Dependency Injection — паттерн внедрения зависимостей";
pair.category = "architecture";
pair.aliases = {"di", "injection"};

qa_source->addQAPair(pair);

// Поиск
auto found = qa_source->findQAPair("qa_001");
if (found) {
    std::cout << "Answer: " << found.value().answer << std::endl;
}

// Поиск по категории
auto arch_pairs = qa_source->searchByCategory("architecture");

// Получение всех пар
auto all = qa_source->getAllPairs();
```

### 6.2. Интеграция с RagEngine

```cpp
#include "rag_engine.h"
#include "qa_source.h"
#include "file_source.h"

RagEngineConfig engine_config;
RagEngine engine(engine_config);

// Добавление QA-источника
QASource::Config qa_config;
qa_config.name = "Project KB";
qa_config.source_id = "prod_kb";
qa_config.pairs = { /* ... */ };
engine.addDataSource(std::make_shared<QASource>(qa_config));

// Добавление filesystem-источника
FileSource::Config file_config;
file_config.root_path = "/path/to/project";
engine.addDataSource(std::make_shared<FileSource>(file_config));

// Индексация
engine.indexSources();

// Поиск (ищет по всем источникам)
auto results = engine.search("Как работает DI?", 10);
```

---

## 7. Лучшие практики

### 7.1. Структура категорий

Используйте иерархию категорий для организации:

```yaml
categories:
  - "setup"          # Установка и настройка
  - "development"    # Разработка
  - "deployment"     # Деплой
  - "support"        # Поддержка
  - "onboarding"     # Онбординг
  - "architecture"   # Архитектура
  - "security"       # Безопасность
  - "performance"    # Производительность
```

### 7.2. Алиасы

Добавляйте алиасы для каждого вопроса:

```json
{
  "question": "Как деплоить?",
  "aliases": ["деплой", "deploy", "docker", "развертывание", "production"]
}
```

### 7.3. Метаданные

Используйте метаданные для отслеживания:

```json
{
  "metadata": {
    "author": "john_doe",
    "reviewed_by": "jane_smith",
    "last_updated": "2026-03-18",
    "version": "1.2",
    "priority": "high"
  }
}
```

### 7.4. Регулярное обновление

- Проверяйте актуальность QA-пар ежемесячно
- Удаляйте устаревшие пары
- Обновляйте версии и ссылки

---

## 8. Ограничения

1. **findQAPair — точное совпадение** — метод `findQAPair()` ищет точный match вопроса/алиаса; для семантического поиска используйте `RagEngine::search()`
2. **Нет автораспознавания дубликатов** — нужно добавлять вручную
3. **Нет версионирования** — старые версии ответов не сохраняются
4. **Нет поддержки вложений** — только текст
5. **Нет multi-language** — один источник = один язык (рекомендуется)

---

## 9. Как работает поиск

### 9.1. Полнотекстовый поиск (Xapian)

При индексации QA-пары конвертируются в документы:

```cpp
doc.content = "Вопрос: " + pair.question + "\n\nОтвет: " + pair.answer;
```

Полный контент (вопрос + ответ) индексируется Xapian. Поиск по ключевым словам из ответа **работает**:

```bash
# Если в ответе есть "docker-compose", этот запрос найдёт QA-пару
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "docker-compose restart", "top_k": 3}'
```

### 9.2. Семантический поиск (HNSW/Embeddings)

Тот же `doc.content` используется для генерации embedding-вектора. Поиск по смыслу (не по ключевым словам) также работает:

```bash
# Найдёт QA-пару по смыслу, даже без точного совпадения слов
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "как перезапустить сервис", "top_k": 3}'
```

### 9.3. Гибридный поиск (Xapian + HNSW)

По умолчанию используется взвешенная комбинация:

```
fused_score = vector_weight * vector_score + text_weight * text_score
```

Настройки в `config.yaml`:

```yaml
rag:
  search:
    vector_weight: 0.6
    text_weight: 0.4
```

### 9.4. Точный поиск по ID

Метод `findQAPair(id)` — для быстрого доступа к конкретной QA-паре без поиска.

---

## 10. Планы развития

- [ ] Автоматическое обнаружение дубликатов
- [ ] Версионирование QA-пар
- [ ] Поддержка вложений (изображения, файлы)
- [ ] Multi-language источники
- [ ] Web UI для управления базой знаний
- [ ] Импорт/экспорт в Markdown
- [ ] Аналитика запросов (что ищут, но не находят)

---

## 11. Примеры

### 11.1. Полный пример config.yaml

```yaml
rag:
  enabled: true

  qa_kb:
    enabled: true
    name: "Tech Support KB"
    source_id: "tech_support"
    pairs:
      - question: "Как перезапустить сервис?"
        answer: "docker-compose restart"
        category: "operations"
        aliases: ["restart", "перезапуск", "reload"]
        metadata:
          author: "devops"
          last_updated: "2026-03-18"

      - question: "Где найти логи?"
        answer: "logs/server.log или docker-compose logs -f"
        category: "operations"
        aliases: ["log", "журнал", "logging"]
        metadata:
          author: "devops"
          last_updated: "2026-03-18"

  filesystem:
    enabled: true
    path: "/path/to/project"
    include_extensions: [".cpp", ".h"]
```

### 11.2. API workflow

```bash
# 1. Добавить QA-пару
curl -X POST http://localhost:8008/api/qa/add \
  -H "Content-Type: application/json" \
  -d '{
    "question": "Как масштабировать?",
    "answer": "Увеличьте workers в config.yaml",
    "category": "operations"
  }'

# 2. Проверить список
curl http://localhost:8008/api/qa/list

# 3. Обновить ответ
curl -X POST http://localhost:8008/api/qa/update \
  -H "Content-Type: application/json" \
  -d '{
    "pair_id": "tech_support_1711000000",
    "answer": "Используйте Kubernetes: kubectl scale deployment..."
  }'

# 4. Поиск
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "масштабирование", "top_k": 3}'

# 5. Вопрос с LLM
curl -X POST http://localhost:8008/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "Как масштабировать сервис?", "top_k": 3}'
```

---

**Last updated:** 2026-03-18
**Current version:** v1.0 (Phase 4)
