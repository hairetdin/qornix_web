# Dependency Matrix

This document summarizes the dependency baseline for building Qornix Web and the standalone `qornix_orm` library with the async HTTP and async DB APIs enabled.

## Baseline

| Dependency | Required for async branch | Notes |
| --- | --- | --- |
| C++ compiler | C++20-capable compiler | GCC 10+, Clang 10+ or newer compatible compiler. |
| CMake | 3.20+ | Root project and templates use C++20. |
| Boost | 1.83+ | Required baseline for Boost.URL, Boost.JSON, Boost.Asio and Boost.MySQL-capable async builds. |
| yaml-cpp | required | Unified project/template configuration. |
| OpenSSL | module-dependent | Auth and Boost.MySQL async TLS/link checks. |
| SQLite | optional ORM driver | Required for default self-contained ORM tests. |
| libpq | optional PostgreSQL driver | Required for sync PostgreSQL and async PostgreSQL live builds. |
| mysqlclient | optional sync MySQL driver | Required for sync MySQL driver. |
| Boost.MySQL headers | optional async MySQL driver | Required when `QORNIX_ENABLE_ASYNC_MYSQL=ON`. |
| pugixml/libxml2 | ORM schema workflow | XML schema parsing and validation support. |

## CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `QORNIX_BUILD_APP` | `ON` | Build root demo app. |
| `QORNIX_BUILD_EXAMPLES` | `OFF` | Build examples. |
| `QORNIX_BUILD_TESTS` | `ON` | Build root tests. |
| `QORNIX_ENABLE_ORM` | `ON` | Build and link `qornix_orm`. |
| `QORNIX_BUILD_DYNAMIC_API` | `ON` | Build Dynamic API module when ORM is enabled. |
| `QORNIX_BUILD_RAG` | `OFF` | Build optional RAG module. |
| `QORNIX_ENABLE_ASYNC_DB` | `ON` in ORM | Build async DB facade and common driver contracts. |
| `QORNIX_ENABLE_ASYNC_POSTGRES` | `OFF` | Build async PostgreSQL driver path. |
| `QORNIX_ENABLE_ASYNC_MYSQL` | `OFF` | Build async MySQL driver path. |

## Build verification commands

```bash
cmake -S . -B build/qornix-release   -DCMAKE_BUILD_TYPE=Release   -DQORNIX_BUILD_EXAMPLES=ON   -DQORNIX_BUILD_TESTS=ON   -DQORNIX_BUILD_RAG=OFF
cmake --build build/qornix-release --parallel
ctest --test-dir build/qornix-release --output-on-failure
```

For PostgreSQL/MySQL integration tests, enable the corresponding async backend flag and provide the DSN/environment variables documented in `qornix_orm/doc/testing.md`.

## RAG dependency profile

`QORNIX_BUILD_RAG=ON` adds the `qornix_rag` module. The module is intentionally tiered: the lightweight profile works without neural embeddings, while the full semantic profile adds ONNX Runtime and external model files.

| Dependency | Required? | Used for |
| --- | --- | --- |
| `hnswlib` | required by current RAG build | local HNSW vector index |
| `libxapian-dev` | required by current RAG build | lexical/Xapian search index |
| `Boost.Filesystem`, `Boost.URL`, `Boost.JSON` | required | file traversal, URL/API helpers, JSON parsing |
| `libcurl4-openssl-dev` | optional but recommended | LLM provider HTTP client |
| `libsqlite3-dev` | optional but recommended | QA/wiki persistence, analytics, model metadata |
| `libzip-dev` | optional | DOCX/OpenXML ingestion |
| `libpugixml-dev` | optional | XLSX/PPTX OpenXML parsing |
| `poppler-utils` | optional runtime | PDF text extraction through `pdftotext` |
| `tesseract-ocr` | optional runtime | image OCR ingestion |
| ONNX Runtime C++ SDK | optional | semantic embedding backend |
| Faiss | optional | alternative local vector backend |
| `libpq-dev` | optional | PostgreSQL/pgvector backend client |
| Qdrant service | optional runtime | external vector backend |
| PostgreSQL + pgvector | optional runtime | SQL-backed vector backend |
| Ollama/LM Studio/vLLM/OpenAI-compatible API | optional runtime | LLM answer generation |
| Redis Open Source | optional runtime | external/shared LLM response cache when `cache.backend=redis` |
| Prometheus server | optional external runtime | scrape `/api/metrics` or `/api/rag/metrics`; no `prometheus-cpp` build dependency is required |

Recommended RAG documentation:

- `qornix_rag/doc/FULL_RAG_GUIDE.md`
- `qornix_rag/doc/CONFIG.md`
- `qornix_rag/doc/API.md`
- `doc/rag_app_template.md`
