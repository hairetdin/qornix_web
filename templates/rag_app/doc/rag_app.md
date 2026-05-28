# Generated Qornix RAG App

This application was generated from `qornix_web/templates/rag_app`. It embeds the reusable `qornix_rag` module as a product-style web application with app-local config and runtime directories.

## Run

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/{{PROJECT_NAME}}
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/rag
http://127.0.0.1:8008/api/rag/health
```

## Runtime Layout

```text
config.yaml                 # app config; RAG settings live under rag:
models/                     # optional ONNX embedding model + tokenizer
data/rag_kb.db              # SQLite QA/wiki and metadata store
data/uploads/               # uploaded documents
data/hnsw_index.bin         # local vector index
knowledge_base/             # optional Markdown wiki source
templates/rag_interface.html
```

## Included RAG Features

- local hybrid search over configured scan paths, uploaded documents, Markdown KB and QA/wiki;
- secure multi-file upload UI/API with extension, MIME, size and count limits;
- delete flow for uploaded documents;
- document ingestion for code/text/Markdown/HTML/CSV/PDF/DOCX/XLSX/PPTX/image OCR;
- metadata-aware chunks for pages, rows, sheets, slides, OCR regions and code symbols;
- local HNSW vector index plus Xapian lexical index;
- TF-IDF retrieval by default and optional ONNX Runtime semantic embeddings;
- Ollama/OpenAI-compatible LLM provider support;
- QA/wiki with duplicate preview, tags, categories, history and safe Markdown rendering;
- health, diagnostics, metrics, analytics and feedback endpoints.

## API Prefix

Standalone documentation often shows `/api/...`. In this generated app the RAG API is mounted under `/api/rag/...`:

```text
GET  /api/rag/health
POST /api/rag/search
POST /api/rag/ask
POST /api/rag/index
POST /api/rag/documents/upload
POST /api/rag/uploads/delete
GET  /api/rag/embedding/models
GET  /api/rag/qa/list
POST /api/rag/qa/add
POST /api/rag/qa/duplicate-check
GET  /api/rag/qa/history
GET  /api/rag/analytics
GET  /api/rag/metrics
```

## Lightweight Mode

The generated default is dependency-light:

```yaml
rag:
  embedding:
    backend: tfidf
    enable_fallback: true
  vector_store:
    backend: local_hnsw
```

This mode works without ONNX Runtime or downloaded embedding files.

## Full Semantic Mode

For better semantic retrieval, install ONNX Runtime C++ SDK, place a compatible text embedding model under `models/`, and switch to:

```yaml
rag:
  embedding:
    backend: onnx
    active_model_id: local-semantic-v1
    models_dir: models
    auto_discover_models: true
    enable_fallback: true
    registry:
      local-semantic-v1:
        backend: onnx
        model_path: models/semantic_model.onnx
        tokenizer_path: models/tokenizer.json
        tokenizer_type: WordPiece
        pooling: mean
        dimension: 384
        max_seq_len: 256
        normalize_embeddings: true
```

Useful helper:

```bash
./download_onnx_model.sh
```

## Ollama LLM

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama serve
ollama pull llama3.2:3b
```

Then set the exact installed model id in `config.yaml`:

```yaml
rag:
  llm:
    api_url: http://localhost:11434
    model: llama3.2:3b
```

Use `ollama list` to verify exact names. If the provider is reachable but the configured model is missing, the UI reports the missing model and shows available models when the provider exposes them.

## LLM Response Cache

Generated apps inherit the same cache behavior as standalone mode. The cache is used for repeated LLM answers, not for document indexes.

Default lightweight setting:

```yaml
rag:
  cache:
    enabled: true
    backend: memory
    max_size: 1000
    ttl_seconds: 3600
```

The memory backend is in-process, fast, dependency-free and reset on restart. It is the practical default for a single generated app instance.

Use Redis when several generated app instances should share completed LLM response cache entries:

```yaml
rag:
  cache:
    enabled: true
    backend: redis
    max_size: 1000
    ttl_seconds: 3600
    redis:
      host: 127.0.0.1
      port: 6379
      db: 0
      password: ""
      ttl_seconds: 3600
```

Redis is implemented through the built-in RESP TCP client. If Redis is unavailable at startup, the app logs a warning and falls back to memory cache so the service can still start.

## More Documentation

Read these files in the framework repository:

```text
qornix_rag/doc/FULL_RAG_GUIDE.md
qornix_rag/doc/CONFIG.md
qornix_rag/doc/API.md
qornix_rag/models/README.md
doc/rag_app_template.md
```


## Xapian language-aware retrieval

Generated RAG apps inherit the same Xapian controls as standalone mode:

```yaml
rag:
  search:
    xapian_enabled: true
    xapian_language: auto
    xapian_stemming: true
    xapian_stemming_strategy: some
    xapian_cjk_ngrams: false
    xapian_word_breaks: true
    xapian_spelling: false
    xapian_metadata_prefixes: true
```

Use an explicit language for single-language documentation projects, for example `en`/`english`, `de`/`german`, `fr`/`french`, `es`/`spanish`, `ru`/`russian`, or any other stemmer supported by the installed Xapian package. `auto` uses a small Cyrillic-vs-default heuristic; it is not universal language detection. Use `none` with `xapian_stemming: false` for code-only projects where exact identifiers are more important than word forms. Diagnostics are available from `/api/rag/health` and `/api/rag/admin/diagnostics`. See Xapian's authoritative language list: https://xapian.org/docs/apidoc/html/classXapian_1_1Stem.html


## Redis response cache

Generated RAG apps support the same LLM response cache backends as standalone `qornix_rag`. Use `rag.cache.backend: memory` for a single local instance. Use `rag.cache.backend: redis` when several app instances should share cached LLM answers. Redis stores only completed LLM response cache entries; uploaded files, SQLite QA/wiki data, embeddings, HNSW/Faiss/Qdrant/pgvector and Xapian are separate storage layers.

Example:

```yaml
rag:
  cache:
    enabled: true
    backend: redis
    ttl_seconds: 3600
    key_prefix: "qornix_rag:"
    redis:
      host: 127.0.0.1
      port: 6379
      db: 0
      password: ""
      ttl_seconds: 3600
```


## Metrics

Generated apps expose RAG Prometheus metrics at:

```http
GET /api/rag/metrics
```

Use this endpoint for scrape-based monitoring of LLM requests, failures, duration summaries, token counters, cache hits/misses, rate-limit rejections, batch counters and latest indexing gauges. The collector is in-process, so counters reset on restart.

```bash
curl -fsS http://127.0.0.1:8008/api/rag/metrics
```

Protect this endpoint with `rag:admin`, host authentication or an internal-only network boundary.
