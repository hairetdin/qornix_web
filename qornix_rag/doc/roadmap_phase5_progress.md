# Фаза 5: Персистентность, Аналитика и Импорт — Прогресс

> **Версия:** 1.0
> **Дата:** 2026-03-18
> **Статус:** 🚀 В РАЗРАБОТКЕ (~35% завершено)

---

## 1. Текущий прогресс

### ✅ Завершено (ядро — 3 компонента)

| Компонент | Файлы | Статус | Описание |
|-----------|-------|--------|----------|
| **SQLiteSource** | `sqlite_source.h` + `.cpp` | ✅ Полная реализация | Persistent QA-пары в SQLite, CRUD, миграция, hash-дедуп при вставке |
| **MarkdownSource** | `markdown_source.h` + `.cpp` | ✅ Полная реализация | Импорт Markdown с frontmatter, парсинг YAML, сканирование директорий |
| **AnalyticsService** | `analytics_service.h` + `.cpp` | ✅ Полная реализация | Логирование поисковых запросов, knowledge gaps, missing answers, daily trends |

### ❌ Не реализовано

| Задача | Описание | Приоритет |
|--------|----------|-----------|
| **DeduplicationService** | Семантическая дедупликация QA-пар полностью отсутствует | 🔴 Критично |
| **7 API endpoints** | `/api/analytics`, `/api/analytics/gaps`, `/api/analytics/export`, `/api/qa/dedup`, `/api/qa/dedup/remove`, `/api/import/markdown`, `/api/import/history` | 🔴 Критично |
| **Интеграция в RagExtension** | SQLiteSource, MarkdownSource, AnalyticsService не подключены | 🔴 Критично |
| **4 набора тестов** | `test_sqlite_source.cpp`, `test_markdown_source.cpp`, `test_deduplication.cpp`, `test_analytics.cpp` | 🟡 Важно |

### 📊 Сводная таблица

| Компонент | Ядро | API Endpoints | RagExtension | Тесты | Общий статус |
|-----------|------|---------------|--------------|-------|--------------|
| SQLiteSource | ✅ | ❌ | ✅ | ❌ | 🟡 50% |
| MarkdownSource | ✅ | ✅ | ✅ | ❌ | 🟡 60% |
| DeduplicationService | ❌ | ❌ | N/A | ❌ | 🔴 0% |
| AnalyticsService | ✅ | ✅ | ✅ | ❌ | 🟡 60% |
| **ИТОГО** | **75% (3/4)** | **71% (5/7)** | **75% (3/4)** | **0%** | **~55%** |

---

## 2. План оставшихся задач

### Этап 1: Интеграция SQLiteSource в RagExtension
- [ ] Добавить `#include "sqlite_source.h"` в `rag_extension.h`
- [ ] Добавить `std::shared_ptr<SQLiteSource>` в RagExtension
- [ ] Добавить парсинг `rag.sqlite.db_path` в `configure()`
- [ ] Создать SQLiteSource в `initialize()` если `rag.sqlite.enabled`
- [ ] Написать `test_sqlite_source.cpp`

### Этап 2: Интеграция MarkdownSource в RagExtension
- [ ] Добавить `#include "markdown_source.h"` в `rag_extension.h`
- [ ] Добавить `std::shared_ptr<MarkdownSource>` в RagExtension
- [ ] Добавить парсинг `rag.markdown.directory_path` в `configure()`
- [ ] Создать MarkdownSource в `initialize()` если `rag.markdown.enabled`
- [ ] Написать `test_markdown_source.cpp`

### Этап 3: Интеграция AnalyticsService в RagExtension
- [ ] Добавить `#include "analytics_service.h"` в `rag_extension.h`
- [ ] Добавить `std::shared_ptr<AnalyticsService>` в RagExtension
- [ ] Добавить парсинг `rag.analytics.*` в `configure()`
- [ ] Создать AnalyticsService в `initialize()`
- [ ] Написать `test_analytics.cpp`

### Этап 4: DeduplicationService (с нуля)
- [ ] Создать `deduplication_service.h`
- [ ] Создать `deduplication_service.cpp`
- [ ] Написать `test_deduplication.cpp`

### Этап 5: API Endpoints (7 endpoints в web.cpp)
- [ ] `GET /api/analytics` — отчёт аналитики
- [ ] `GET /api/analytics/gaps` — knowledge gaps
- [ ] `POST /api/analytics/export` — экспорт в JSON
- [ ] `POST /api/qa/dedup` — найти дубликаты
- [ ] `POST /api/qa/dedup/remove` — удалить дубликаты
- [ ] `POST /api/import/markdown` — импорт Markdown
- [ ] `GET /api/import/history` — история импортов

### Этап 6: Сборка и тестирование
- [ ] Собрать проект
- [ ] Запустить все тесты
- [ ] Обновить документацию

---

## 3. Зависимости между этапами

```
Этап 1 (SQLiteSource integration)  ───→ независим
Этап 2 (MarkdownSource integration) ───→ независим
Этап 3 (AnalyticsService integration) ───→ независим
Этап 4 (DeduplicationService) ───→ независим
Этап 5 (API Endpoints) ───→ зависит от Этапов 1-4
Этап 6 (Build & Test) ───→ зависит от Этапов 1-5
```

---

## 4. Notes

- MarkdownParser не реализован отдельно — парсинг frontmatter встроен в `MarkdownSource::parseFrontmatter()`
- Hash-дедупликация уже есть в `SQLiteSource::addQAPair()` и `MarkdownSource::importFile()` — но это только string-based, не semantic
- AnalyticsService уже реализован, но не интегрирован в RagApiHandler (не логирует поисковые запросы)
