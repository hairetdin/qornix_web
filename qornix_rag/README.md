# Qornix RAG

`qornix_rag` is a local-first RAG/wiki/search module for C++ projects and document collections. It can run as a standalone single-user application or as an embedded module inside a generated `qornix_web` application.

The current implementation includes document ingestion, parser metadata, structure-aware chunking, TF-IDF or ONNX embeddings, local/external vector stores, Xapian lexical indexing, hybrid retrieval, citations, QA/wiki persistence, upload UI/API, LLM provider diagnostics, analytics and feedback.

Короткий русскоязычный FAQ по базовым вопросам проекта находится в [`../doc/WHAT_IS_QORNIX_RAG_RU.md`](../doc/WHAT_IS_QORNIX_RAG_RU.md), английская версия — в [`../doc/WHAT_IS_QORNIX_EN.md`](../doc/WHAT_IS_QORNIX_EN.md). Обзор только RAG-модуля находится в [`doc/OVERVIEW_RU.md`](doc/OVERVIEW_RU.md) и [`doc/OVERVIEW_EN.md`](doc/OVERVIEW_EN.md). Эти документы специально написаны так, чтобы RAG мог уверенно отвечать на вопросы вроде "что такое qornix_rag?", "what is qornix_rag?", "для чего нужен qornix_web?" и "what is RAG?".

For the complete product guide, dependencies, models, databases and run profiles, start here:

- [Full RAG guide](doc/FULL_RAG_GUIDE.md)
- [Configuration reference](doc/CONFIG.md)
- [API reference](doc/API.md)
- [Standalone guide](doc/STANDALONE.md)
- [qornix_web integration](doc/INTEGRATION_QORNIX_WEB.md)
- [Embedding models guide](models/README.md)

## Quick Start: Standalone

From the repository root:

```bash
./qornix_rag/run.sh
```

Open:

```text
http://localhost:8081
```

Useful variants:

```bash
./qornix_rag/run.sh --port 8082
./qornix_rag/run.sh --scan-path /path/to/project
./qornix_rag/run.sh --address 127.0.0.1 --port 8081 --scan-path /path/to/project
QORNIX_RAG_CONFIG=/path/to/config.yaml ./qornix_rag/run.sh
```

Without `indexing.scan_path` or `--scan-path`, standalone mode starts without scanning a directory. Uploads, API ingestion with an explicit `scan_path`, QA storage, and search/ask over already indexed data remain available.

Use `--address 0.0.0.0` only behind a trusted network/proxy/auth boundary. Standalone mode is designed as a local single-user application.

## Quick Start: Generated RAG Application

Create a separate product-style app:

```bash
./create_new_project.sh ../my_rag_app --template rag_app
cd ../my_rag_app
cmake -S . -B build
cmake --build build -j$(nproc)
./build/my_rag_app
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/rag
http://127.0.0.1:8008/api/rag/health
```

Generated apps keep runtime data in their own directory:

```text
config.yaml
models/
data/rag_kb.db
data/uploads/
knowledge_base/
templates/rag_interface.html
```

The generated app does not use `qornix_rag/run.sh`; it links the reusable RAG module through `qornix::rag_extension`.

## Main Features

| Area | Current support |
|---|---|
| File ingestion | Code, text, Markdown, HTML, JSON/YAML/XML, CSV, PDF, DOCX, XLSX, PPTX, image OCR |
| Upload | Secure multi-file upload UI/API, extension/MIME/size allowlists, upload delete flow |
| Metadata | PDF pages, spreadsheet sheets/rows, PPTX slides/notes, DOCX headings/tables, OCR diagnostics, code symbols |
| Chunking | Metadata-aware, token-budget-aware and source-aware chunks |
| Retrieval | Hybrid HNSW/vector + language-aware Xapian, query expansion, multi-query retrieval, reranking, metadata filters |
| Embeddings | TF-IDF fallback by default; optional ONNX Runtime semantic embeddings |
| Vector stores | `local_hnsw` default; optional `faiss`, `qdrant`, `pgvector` |
| LLM providers | Ollama default; OpenAI-compatible/vLLM/LM Studio style endpoints |
| LLM response cache | In-process memory LRU cache by default or external Redis backend for shared production cache |
| QA/wiki | SQLite-backed QA, tags, categories, duplicate checks, history, safe Markdown rendering |
| Diagnostics | Health endpoint, Xapian language/stemming status, model registry, provider/model status, metrics, analytics, feedback |

## Supported Documents

| Format | Dependency |
|---|---|
| Source code, Markdown, text, JSON/YAML/XML, HTML, CSV | Built in |
| PDF | `poppler-utils` / `pdftotext` |
| DOCX | `libzip` |
| XLSX/PPTX | `libzip` + `pugixml` |
| Images/OCR | `tesseract-ocr` |

Missing optional parser dependencies do not prevent startup; they are reported through ingestion diagnostics.


## Operations And Metrics

Health, diagnostics and Prometheus metrics are available in both standalone and generated-app modes:

| Runtime | Health | Diagnostics | Metrics |
|---|---|---|---|
| Standalone | `/api/health` | `/api/admin/diagnostics` | `/api/metrics` |
| Generated `rag_app` | `/api/rag/health` | `/api/rag/admin/diagnostics` | `/api/rag/metrics` |

The Prometheus endpoint returns text exposition format and is implemented by the built-in in-process metrics collector. It covers LLM request counters, failures, durations, tokens, cache hits/misses/size, rate-limit rejections, batch counters and latest indexing file/line gauges.

```bash
curl -fsS http://127.0.0.1:8082/api/metrics
```

Protect diagnostics and metrics before exposing a generated app to a network.

## Lightweight vs Full Mode

Default lightweight mode works after a normal build:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true

vector_store:
  backend: local_hnsw
```

This is enough for local search, upload, QA/wiki and Ask with an LLM. Repeated LLM answers are cached in an in-process memory LRU cache when `cache.enabled: true`.

Full semantic mode adds ONNX Runtime and compatible embedding files:

```yaml
embedding:
  backend: onnx
  active_model_id: local-semantic-v1
  models_dir: qornix_rag/models
  auto_discover_models: true
  enable_fallback: true
```

Use the helper when possible:

```bash
./qornix_rag/download_onnx_model.sh
```

See [Full RAG guide](doc/FULL_RAG_GUIDE.md) and [Embedding models guide](models/README.md) before enabling ONNX. Chat LLMs and embedding models are different artifacts: Ollama serves the chat model, while `qornix_rag/models/*.onnx` is used only for retrieval embeddings.

## Local LLM

The default config is prepared for Ollama:

```yaml
llm:
  api_url: http://localhost:11434
  model: llama3.2:3b
```

Example setup:

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama serve
ollama pull llama3.2:3b
./qornix_rag/run.sh
```

If the configured model is missing, the UI reports `model_not_found` and shows provider models when the provider exposes them. Use exactly the model id shown by `ollama list`, for example `llama3:latest`.

## Upload And Search Smoke Test

```bash
curl http://localhost:8081/api/health

curl -X POST http://localhost:8081/api/search \
  -H "Content-Type: application/json" \
  -d '{"query":"How does routing work?","top_k":5}'

curl -X POST http://localhost:8081/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question":"How does routing work?","top_k":5}'
```

Upload through the UI **Upload** tab, or use the API:

```bash
curl -X POST http://localhost:8081/api/documents/upload \
  -F "files=@README.md"
```

Reindex:

```bash
curl -X POST http://localhost:8081/api/index \
  -H "Content-Type: application/json" \
  -d '{"scan_path":"/path/to/project"}'
```

## Portable Bundle

```bash
./qornix_rag/build_portable.sh --smoke --archive
cd dist/qornix_rag-portable-linux-x86_64
./run.sh
```

Full portable documentation: [Portable bundle guide](doc/PORTABLE.md).

## Build Targets

- `qornix_rag_core`: reusable RAG services and data sources.
- `qornix_rag_http`: HTTP handlers and route registration.
- `qornix_rag_extension`: qornix_web extension adapter.
- `qornix_rag`: standalone executable.
- `qornix_rag_route_extension`: optional dynamic route extension shared object.
- `qornix_rag_vector_migrate`: vector snapshot migration helper.

Manual build:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON
cmake --build build --target qornix_rag -j$(nproc)
```

## Documentation Map

- [Full RAG guide](doc/FULL_RAG_GUIDE.md) — capabilities, dependencies, models, databases and run profiles.
- [Standalone guide](doc/STANDALONE.md) — local `run.sh` usage.
- [Configuration reference](doc/CONFIG.md) — YAML keys and operational profiles.
- [API reference](doc/API.md) — endpoint contract.
- [Data sources](doc/DATA_SOURCES.md) — QA/wiki/source integration.
- [Knowledge base](doc/KNOWLEDGE_BASE.md) — QA/wiki and Markdown KB usage.
- [Portable bundle guide](doc/PORTABLE.md) — portable Linux bundle.
- [qornix_web integration](doc/INTEGRATION_QORNIX_WEB.md) — embedded module integration.
- [Generated rag_app template](../doc/rag_app_template.md) — separate product app template.
- [Embedding models guide](models/README.md) — ONNX model compatibility and setup.

Project planning documents are under `qornix_rag/doc/project_doc/`.
