# Qornix RAG Full Guide

This guide is the product-level entry point for the modernized `qornix_rag` module. It covers what the RAG system can do, which dependencies are required for each mode, which databases/vector stores are supported, how to install local LLM and embedding runtimes, how to run a lightweight setup, and how to move toward a fuller local production setup.

`qornix_rag` can be used in two ways:

- **standalone local app**: `./qornix_rag/run.sh`, UI at `/`, API at `/api/*`;
- **generated qornix_web app**: `./create_new_project.sh ../my_rag_app --template rag_app`, UI at `/` and `/rag`, API at `/api/rag/*`.

The standalone app is optimized for fast local use. The generated `rag_app` template is the product-facing integration path when you want a separate application with its own config, deploy bundle, auth options, and runtime directories.

## Current Capability Summary

The current RAG pipeline is no longer only a code-search demo. It now includes these major layers:

| Layer | Capability |
|---|---|
| File ingestion | Source code, text, Markdown, HTML, JSON/YAML/XML, CSV, PDF, DOCX, XLSX, PPTX, images with OCR |
| Advanced metadata | Parser diagnostics, page/sheet/slide/OCR-region/code-symbol metadata, document structure metadata |
| Chunking | Source-aware, heading-aware, page-aware, row-aware, slide-aware, OCR-region-aware, symbol-aware chunking |
| Embeddings | Dependency-light TF-IDF fallback and optional ONNX Runtime semantic embeddings |
| Vector search | Local HNSW by default; optional Faiss, Qdrant, and PostgreSQL/pgvector backends |
| Text search | Xapian lexical index |
| Hybrid retrieval | Vector + Xapian fusion, query expansion, multi-query retrieval, reranking, metadata filters |
| Citations | Source ids, source paths, locators, curated metadata and grounding diagnostics |
| LLM integration | Ollama by default; OpenAI-compatible APIs, vLLM and LM Studio style endpoints |
| LLM response cache | Memory LRU cache by default; Redis settings are documented as an external-cache target and currently fall back to memory unless the Redis backend is wired in the build |
| QA/wiki | Persistent QA entries, tags, categories, duplicate preview, version history, safe Markdown rendering |
| Upload flow | Secure multi-file upload from UI/API with allowlists, size limits, ingestion jobs and delete flow |
| Operations | Health/diagnostics, metrics, analytics, feedback endpoint, portable bundle and generated-app deployment docs |

## Architecture At A Glance

```text
files / uploads / Markdown KB / QA wiki
        ↓
IngestionPipeline
        ↓
text + parser metadata + diagnostics
        ↓
DocumentChunker
        ↓
chunks + locators + citation metadata
        ↓
Embedding backend: tfidf or onnx
        ↓
Vector store: local_hnsw / faiss / qdrant / pgvector
        ↓
Xapian lexical index
        ↓
Hybrid retrieval + filters + reranking
        ↓
LLM answer + citations + grounding diagnostics
```

The system is intentionally local-first by default. It binds to `127.0.0.1`, uses local runtime data directories, and can run without a neural embedding model. You can then enable fuller components one by one.

## Supported Input Formats

| Format | Text extraction | Metadata/chunking level | Main dependency |
|---|---|---|---|
| `.cpp`, `.h`, `.hpp`, `.c`, `.rs`, `.py`, `.js`, `.ts`, etc. | Built in | Symbol/scope metadata where detected | none |
| `.md`, `.txt`, `.rst`, `.adoc` | Built in | Heading/text chunks | none |
| `.json`, `.yaml`, `.yml`, `.xml` | Built in | Structured text chunks | none |
| `.html`, `.htm` | Built in | Safe text extraction without regex-heavy parsing | none |
| `.csv` | Built in | Header, row ranges, inferred cell types | none |
| `.pdf` | `pdftotext` | Page-aware chunks, PDF diagnostics | `poppler-utils` |
| `.docx` | OpenXML zip parser | Paragraphs, headings, tables, styles, notes/comments metadata | `libzip` |
| `.xlsx` | OpenXML zip parser | Sheet/row/cell/formula/style metadata | `libzip`, `pugixml` |
| `.pptx` | OpenXML zip parser | Slide/notes/comments/media/alt-text metadata | `libzip`, `pugixml` |
| images: `.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.webp` | OCR | Image size, EXIF orientation, OCR confidence/region metadata | `tesseract-ocr` |

If an optional parser dependency is missing, startup still works. That parser reports `parser_unavailable` or a structured diagnostic instead of crashing the process.

## Main Runtime Databases And Indexes

| Component | Default path | Purpose | Required? |
|---|---|---|---|
| SQLite KB | `qornix_rag/data/rag_kb.db` or generated app `data/rag_kb.db` | QA/wiki, import history, analytics, embedding model metadata, persisted docs where enabled | Recommended |
| Local HNSW | `qornix_rag/data/hnsw_index.bin` | Local vector index | Default |
| Local HNSW metadata | `qornix_rag/data/hnsw_index.meta.json` | Snapshot hash/model signature safety | Default |
| Xapian index | runtime-managed local lexical index | BM25-style lexical retrieval | Required by current build |
| Uploaded files | `qornix_rag/data/uploads` or generated app `data/uploads` | Files uploaded through UI/API | Optional but enabled by default |
| Markdown KB | `qornix_rag/knowledge_base` or generated app `knowledge_base` | File-based wiki import source | Optional |
| Qdrant | external service | Remote vector store | Optional |
| PostgreSQL + pgvector | external PostgreSQL | SQL-backed vector store | Optional |
| Faiss | linked library | Alternative local vector store | Optional |
| Redis | external service | Optional shared LLM response cache backend for multi-instance deployments | Optional |


## LLM Response Cache: Memory And Redis

`qornix_rag` has a small cache layer for LLM responses. It is separate from the vector index, Xapian index, SQLite KB and embedding cache.

What it caches:

```text
question + retrieved context + configured LLM model + generation parameters
        ↓
LLM answer payload
```

What it does **not** cache:

```text
file ingestion
PDF/DOCX/XLSX/PPTX/OCR parsing
chunking
embedding generation
HNSW/Faiss/Qdrant/pgvector indexes
Xapian lexical indexes
SQLite QA/wiki records
```

### Memory cache, default local backend

The default runtime backend is:

```yaml
cache:
  enabled: true
  backend: memory
  ttl_seconds: 3600
  max_size: 1000
```

Memory cache behavior:

- lives inside the `qornix_rag` process;
- uses TTL expiration and LRU-style eviction;
- is very fast and requires no external service;
- is cleared when the process restarts;
- is not shared between multiple app instances;
- is the right choice for local development, single-user standalone mode and small generated apps.

Use memory cache when you want the simplest setup and do not need cache sharing across replicas.

### Redis cache, external shared backend

Redis is the supported external cache backend for production-style deployments and multi-instance generated apps:

```yaml
cache:
  enabled: true
  backend: redis
  ttl_seconds: 3600
  max_size: 1000
  redis:
    host: 127.0.0.1
    port: 6379
    db: 0
    password: ""
    ttl_seconds: 3600
```

Implementation status:

```text
backend=redis → RedisCache over RESP TCP client → safe MemoryCache fallback only if Redis is unavailable
```

The Redis backend supports optional `AUTH`, `SELECT`, `PING`, `GET`, `SETEX`, `DEL`, and prefix-scoped `SCAN` for `clear()`. No `hiredis` dependency is required.

Why Redis is useful:

- shared cache across multiple RAG app instances;
- cache survives app process restarts if Redis persistence is enabled;
- centralized TTL and memory eviction policies;
- easier operational monitoring with Redis tools;
- useful when several generated apps or workers call the same LLM provider;
- can reduce LLM latency and cost for repeated questions with the same retrieved context.

Typical production Redis concerns:

- bind Redis to localhost/private networks only;
- use a password/ACL when reachable outside the same host;
- configure `maxmemory` and an eviction policy appropriate for cache workloads;
- keep TTLs finite for LLM cache keys;
- decide whether Redis persistence is desirable for cache data. For pure cache workloads, persistence is optional.

Official Redis documentation:

- install on Ubuntu/Debian: https://redis.io/docs/latest/operate/oss_and_stack/install/install-stack/apt/
- key eviction: https://redis.io/docs/latest/develop/reference/eviction/
- persistence: https://redis.io/docs/latest/operate/oss_and_stack/management/persistence/


## Recommended Ubuntu Dependencies

### Minimal local development profile

This profile runs the UI, source-code/text ingestion, TF-IDF embeddings, local HNSW, Xapian, SQLite, QA/wiki, upload UI and Ollama integration.

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git curl ca-certificates \
  libboost-url-dev libboost-json-dev libboost-log-dev libboost-filesystem-dev \
  libssl-dev libyaml-cpp-dev libsqlite3-dev \
  libxapian-dev xapian-tools \
  libcurl4-openssl-dev \
  libzip-dev libpugixml-dev \
  poppler-utils tesseract-ocr
```

`hnswlib` is required by the current RAG build. Use the repository helper:

```bash
./qornix_rag/depend_install.sh
```

or install it manually:

```bash
cd /tmp
git clone https://github.com/nmslib/hnswlib.git
cd hnswlib
cmake -S . -B build
cmake --build build -j$(nproc)
sudo cmake --install build
```

### Full local semantic profile

Add ONNX Runtime C++ SDK and an embedding model:

```bash
# Example CPU SDK layout. Choose the current version you want to use.
cd /tmp
ORT_VERSION=1.24.4
wget https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz
tar -xzf onnxruntime-linux-x64-${ORT_VERSION}.tgz
sudo mkdir -p /opt/onnxruntime
sudo cp -r onnxruntime-linux-x64-${ORT_VERSION}/* /opt/onnxruntime/
echo /opt/onnxruntime/lib | sudo tee /etc/ld.so.conf.d/onnxruntime.conf
sudo ldconfig
```

Then configure with the runtime visible to CMake:

```bash
cmake -S . -B build \
  -DQORNIX_BUILD_RAG=ON \
  -DONNXRUNTIME_ROOT=/opt/onnxruntime \
  -DCMAKE_PREFIX_PATH=/opt/onnxruntime
cmake --build build -j$(nproc)
```

If CMake still prints:

```text
ONNX Runtime not found. Build will use TF-IDF fallback.
```

then the C++ headers/library were not found. The application will still work with TF-IDF, but ONNX embeddings will not be active.

### Optional vector-store dependencies

```bash
# PostgreSQL client development files for pgvector backend support
sudo apt install -y libpq-dev postgresql-client

# Faiss, when available from your distribution or installed manually
sudo apt install -y libfaiss-dev || true
```

Qdrant is normally run as a separate service, commonly through Docker. PostgreSQL/pgvector is a separate database extension.

## Local LLM Setup

The easiest supported LLM provider is Ollama.

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama serve
ollama pull llama3.2:3b
```

Then configure:

```yaml
llm:
  api_url: http://localhost:11434
  model: llama3.2:3b
```

Useful model choices:

| Hardware | Suggested models | Notes |
|---|---|---|
| Low RAM / first smoke test | `llama3.2:3b`, `qwen2.5:3b`, `phi3:mini` | Quick checks, lower quality |
| 16-32 GB RAM | `llama3.1:8b`, `qwen2.5:7b`, `mistral:7b` | Good local default range |
| 64-128 GB RAM | `qwen2.5:14b`, `qwen2.5-coder:14b`, larger Qwen/Llama variants | Better reasoning/code help |
| Code-heavy RAG | `qwen2.5-coder:*`, `deepseek-coder:*` if available in your environment | Prefer code-tuned models |

Use exactly the model id reported by Ollama:

```bash
ollama list
```

For example, if `ollama list` shows `llama3:latest`, set `llm.model: llama3:latest`. The UI distinguishes these states:

- provider unavailable: Ollama/API server cannot be reached;
- model not found: provider is reachable but `llm.model` is not in the provider's model list;
- ready: provider is reachable and the configured model is available.

OpenAI-compatible endpoints, vLLM and LM Studio style model-list/usage responses are supported by the same LLM client path. Set `llm.api_url`, `llm.api_key` if needed, and `llm.model` to the exact served id.

## Embedding Model Setup

There are two different model categories:

| Category | Used by | File type | Example |
|---|---|---|---|
| LLM/chat model | Answer generation | Ollama/OpenAI-compatible provider model | `llama3.2:3b` |
| Embedding model | Retrieval vectors | local `.onnx` + `tokenizer.json` | `all-MiniLM-L6-v2` ONNX export |

Do not put Ollama `.gguf` chat models into `qornix_rag/models`. The ONNX embedding backend expects a compatible text encoder model.

Recommended path:

```bash
./qornix_rag/download_onnx_model.sh
```

Manual download path:

```bash
python3 -m pip install --user huggingface_hub
huggingface-cli download Xenova/all-MiniLM-L6-v2 \
  --include "onnx/model.onnx" "tokenizer.json" \
  --local-dir qornix_rag/models/downloaded
cp qornix_rag/models/downloaded/onnx/model.onnx qornix_rag/models/semantic_model.onnx
cp qornix_rag/models/downloaded/tokenizer.json qornix_rag/models/tokenizer.json
```

Manual export path:

```bash
python3 -m pip install --user "optimum[onnxruntime]" transformers
optimum-cli export onnx \
  --model sentence-transformers/all-MiniLM-L6-v2 \
  --task feature-extraction \
  qornix_rag/models/exported
```

Then configure:

```yaml
embedding:
  backend: onnx
  active_model_id: local-semantic-v1
  models_dir: qornix_rag/models
  auto_discover_models: true
  enable_fallback: true
  registry:
    local-semantic-v1:
      backend: onnx
      name: all-MiniLM-L6-v2 local ONNX
      model_path: qornix_rag/models/semantic_model.onnx
      tokenizer_path: qornix_rag/models/tokenizer.json
      tokenizer_type: WordPiece
      pooling: mean
      dimension: 384
      max_seq_len: 256
      onnx_threads: 2
      normalize_embeddings: true
      lowercase_tokens: true
```

Check the active embedding backend:

```bash
curl http://localhost:8081/api/embedding/models
curl http://localhost:8081/api/health
```

If the model signature changes, persisted embeddings are treated as stale and are regenerated instead of being silently reused.

## Lightweight vs Full Configuration

### Lightweight mode

Use this for development, documentation work, and low-RAM machines.

```yaml
embedding:
  backend: tfidf
  enable_fallback: true

vector_store:
  backend: local_hnsw

indexing:
  # Optional. Omit to start without startup filesystem scanning.
  # scan_path: /path/to/project
  auto_index_on_startup: true
  max_file_size_kb: 4096

llm:
  api_url: http://localhost:11434
  model: llama3.2:3b
```

Pros:

- no ONNX model required;
- fast setup;
- works offline except for model downloads;
- good for exact/code/documentation search.

Cons:

- less semantic than neural embeddings;
- similar wording matters more.

### Full local mode

Use this for better semantic retrieval and larger document collections.

```yaml
embedding:
  backend: onnx
  active_model_id: local-semantic-v1
  enable_fallback: true

search:
  use_hybrid: true
  use_query_expansion: true
  use_reranking: true
  use_multi_query_retrieval: true
  use_embedding_reranker: true

vector_store:
  backend: local_hnsw
  auto_load: true
  auto_save: true

upload:
  enabled: true
  auto_ingest: true
```

Pros:

- better meaning-based retrieval;
- citation metadata is more useful;
- safer embedding cache invalidation through model signatures.

Cons:

- requires ONNX Runtime C++ SDK;
- requires compatible embedding model files;
- slower indexing.

### External vector-store mode

Use this when you want the vector index outside the process.

Qdrant:

```yaml
vector_store:
  backend: qdrant
  endpoint: http://127.0.0.1:6333
  collection: qornix_rag_vectors
  distance: Cosine
```

PostgreSQL/pgvector:

```yaml
vector_store:
  backend: pgvector
  connection_string: host=127.0.0.1 port=5432 dbname=qornix user=qornix password=secret
  table: qornix_rag_vectors
  distance: Cosine
```

Faiss:

```yaml
vector_store:
  backend: faiss
```

Faiss must be available at build time. Qdrant and pgvector require their external services to be running.

## Running Standalone

```bash
./qornix_rag/run.sh
./qornix_rag/run.sh --port 8082
./qornix_rag/run.sh --scan-path /path/to/project
./qornix_rag/run.sh --address 127.0.0.1 --port 8082 --scan-path /path/to/project
```

Environment overrides:

```bash
QORNIX_RAG_CONFIG=/path/to/config.yaml ./qornix_rag/run.sh
QORNIX_RAG_HOME=/path/to/runtime ./qornix_rag/run.sh
```

Use `--address 0.0.0.0` only behind a trusted network/proxy/auth boundary. Standalone mode is designed as a local single-user application.

## Creating A Product App

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

Generated app paths are app-local:

```text
config.yaml
models/
data/rag_kb.db
data/uploads/
knowledge_base/
templates/rag_interface.html
```

The generated app does not use `qornix_rag/run.sh`. It links the RAG module through `qornix::rag_extension` and maps its config under `rag:`.

## Xapian Language-Aware Retrieval

Hybrid retrieval uses two complementary channels:

```text
semantic vectors from ONNX/TF-IDF
+
Xapian lexical/full-text index
```

The vector side is good at semantic similarity. Xapian is good at exact identifiers, API paths, config keys, symbols, metadata, and language word forms. Current Xapian settings are configurable:

```yaml
search:
  xapian_enabled: true
  xapian_language: auto       # auto | none | Xapian language name or ISO 639 alias
  xapian_stemming: true
  xapian_stemming_strategy: some
  xapian_cjk_ngrams: false
  xapian_word_breaks: true
  xapian_spelling: false
  xapian_metadata_prefixes: true
```

`auto` is intentionally lightweight: it selects `russian` when Cyrillic text is present and `english` otherwise. Technical/file metadata values such as `python`, `javascript`, `html`, `xml`, `pdf`, `image`, or `c/c++ header` are ignored for stemmer selection because they are not natural-language stemmers. For projects primarily written in another language, set the Xapian language explicitly, for example `en`/`english`, `de`/`german`, `fr`/`french`, `es`/`spanish`, `ru`/`russian`, or another stemmer supported by the installed Xapian package. Use `none`/`xapian_stemming: false` for code-only repositories where exact identifiers should dominate. The authoritative language list is in the Xapian `Xapian::Stem` documentation: https://xapian.org/docs/apidoc/html/classXapian_1_1Stem.html

When metadata prefixes are enabled, Xapian also indexes fielded prefixes for:

```text
path:, source:, type:, lang:, meta:, symbol:, page:, sheet:, slide:
```

Examples:

```json
{"query":"symbol:RagUploadService"}
{"query":"type:pdf page:5 upload"}
{"query":"lang:russian индексация документов"}
```

The effective Xapian configuration and build/search status are visible in `/api/health`, `/api/stats`, and `/api/admin/diagnostics`.

## Uploading Documents

The UI has an **Upload** tab in both standalone and generated `rag_app` projects. The backend endpoint is:

```text
Standalone: POST /api/documents/upload
rag_app:    POST /api/rag/documents/upload
```

The upload flow:

```text
browser multi-file upload
  ↓
extension/MIME/size allowlist
  ↓
safe filename sanitization
  ↓
store under uploads_dir/upload_<timestamp>/
  ↓
ingestion/reindex job
  ↓
chunks + embeddings + Xapian index
  ↓
search/ask can use the uploaded content
```

Default upload config:

```yaml
upload:
  enabled: true
  uploads_dir: qornix_rag/data/uploads
  max_file_size_kb: 16384
  max_files_per_request: 20
  auto_ingest: true
  async_ingest: false
  overwrite_existing: false
```

Delete uploaded files:

```text
Standalone: POST /api/uploads/delete
rag_app:    POST /api/rag/uploads/delete
```

Deletion is restricted to files under `uploads_dir` and can trigger reindexing so removed documents no longer appear in retrieval.

## Important API Groups

| Group | Standalone | Generated `rag_app` | Purpose |
|---|---|---|---|
| Health | `/api/health` | `/api/rag/health` | LLM, embeddings, index, dependencies |
| Search | `/api/search` | `/api/rag/search` | Hybrid retrieval with filters |
| Ask | `/api/ask` | `/api/rag/ask` | LLM answer with retrieved context |
| Upload | `/api/documents/upload` | `/api/rag/documents/upload` | Secure document upload |
| Upload delete | `/api/uploads/delete` | `/api/rag/uploads/delete` | Remove uploaded file and reindex |
| Index | `/api/index` | `/api/rag/index` | Index or reindex a scan path/root |
| QA | `/api/qa/*` | `/api/rag/qa/*` | QA/wiki management |
| Embeddings | `/api/embedding/models` | `/api/rag/embedding/models` | Model registry diagnostics |
| Analytics | `/api/analytics` | `/api/rag/analytics` | Search/feedback/quality reports |
| Metrics | `/api/metrics` | `/api/rag/metrics` | Prometheus metrics |

Metadata-aware filters are supported in search/ask bodies. Example:

```json
{
  "query": "route registration",
  "top_k": 5,
  "filters": {
    "type": "cpp",
    "metadata_contains": {
      "chunk_symbol_kind": "function"
    }
  }
}
```

## Health Diagnostics Checklist

Open the health endpoint or UI status panel and check:

| Field/status | Meaning | Fix |
|---|---|---|
| `embedding backend: tfidf` | ONNX is not active | OK for lightweight mode; install ONNX Runtime and model files for semantic mode |
| `ONNX Runtime not found` during CMake | C++ SDK/library not detected | Install SDK, run `ldconfig`, pass `CMAKE_PREFIX_PATH`/`ONNXRUNTIME_ROOT` |
| `provider_unavailable` | LLM API server not reachable | Start Ollama/LM Studio/vLLM or fix `llm.api_url` |
| `model_not_found` | Provider is reachable but configured model is absent | Pull/select a model exactly as listed by provider |
| `parser_unavailable` | Optional parser dependency missing | Install `poppler-utils`, `tesseract-ocr`, `libzip`, `pugixml`, etc. |
| snapshot hash mismatch | Persisted vector index belongs to an older chunk/model state | Usually normal after config/content changes; index is rebuilt |


## Observability And Prometheus Metrics

`qornix_rag` includes a built-in Prometheus text exporter. It does not require `prometheus-cpp`; metrics are stored in-process and rendered by the built-in `LLMRAGMetrics`/`MetricsRegistry` classes.

Endpoints:

| Runtime | Endpoint |
|---|---|
| Standalone | `GET /api/metrics` |
| Generated `rag_app` | `GET /api/rag/metrics` |

Quick check:

```bash
curl -fsS http://127.0.0.1:8082/api/metrics
curl -fsS http://127.0.0.1:8008/api/rag/metrics
```

The exported set currently covers LLM request counters, failed request counters, request duration summaries, provider token counters when token usage is reported, LLM response cache hits/misses/size, rate-limit rejections, batch question counters, and latest indexing gauges for files and lines.

Typical output shape:

```text
# HELP qornix_rag_llm_requests_total Total count of events
# TYPE qornix_rag_llm_requests_total counter
qornix_rag_llm_requests_total 12.0
# HELP qornix_rag_indexed_files_count Current value
# TYPE qornix_rag_indexed_files_count gauge
qornix_rag_indexed_files_count 510.0
```

Prometheus scrape example for standalone mode:

```yaml
scrape_configs:
  - job_name: qornix-rag
    metrics_path: /api/metrics
    static_configs:
      - targets: ['127.0.0.1:8082']
```

Prometheus scrape example for generated apps:

```yaml
scrape_configs:
  - job_name: qornix-rag-app
    metrics_path: /api/rag/metrics
    static_configs:
      - targets: ['127.0.0.1:8008']
```

Operational notes:

- Metrics are process-local. Restarting the process resets counters and summaries.
- `metrics_enabled` is reported by `/api/health` and `/api/admin/diagnostics`.
- Protect metrics endpoints when exposing the app beyond localhost; they reveal operational state and usage volumes.
- Use metrics together with `/api/admin/diagnostics` and `/api/analytics` for release smoke checks and production monitoring.

Suggested alert ideas:

| Signal | Why it matters |
|---|---|
| Increasing `qornix_rag_llm_requests_failed_total` | Provider/model/API failures |
| Increasing `qornix_rag_rate_limit_rejections_total` | Traffic spike, abuse or too strict limits |
| Flat `qornix_rag_indexed_files_count` after an expected upload/reindex | Upload or ingestion pipeline not updating the index |
| Low cache hits with repeated queries | Cache key/config mismatch or too small TTL/cache size |

## Security Notes

- Keep standalone mode bound to `127.0.0.1` unless a reverse proxy or trusted network boundary is in place.
- `rag_app` can enable qornix auth/RBAC; standalone mode is intentionally local single-user.
- Protect write/admin routes before network exposure: upload, delete, indexing, QA writes, diagnostics and metrics.
- Keep upload allowlists strict. Do not allow arbitrary executables or archives unless a separate sandbox scanner is added.
- Do not commit downloaded models, uploaded documents, `data/`, `logs/`, secrets or API keys.
- Use environment variables for tokens/secrets where possible.

## Official References

- Ollama Linux install: https://docs.ollama.com/linux
- Ollama download page: https://ollama.com/download
- ONNX Runtime install: https://onnxruntime.ai/docs/install/
- ONNX Runtime C++ guide: https://onnxruntime.ai/docs/get-started/with-cpp.html
- Hugging Face Hub model downloads: https://huggingface.co/docs/hub/en/models-downloading
- Hugging Face Optimum ONNX export: https://huggingface.co/docs/optimum-onnx/onnx/usage_guides/export_a_model
- Xapian install docs: https://xapian.org/docs/install.html
- Tesseract install docs: https://tesseract-ocr.github.io/tessdoc/Installation.html
- Poppler project: https://poppler.freedesktop.org/
- Faiss project: https://github.com/facebookresearch/faiss
- Qdrant quickstart: https://qdrant.tech/documentation/quickstart/
- pgvector project: https://github.com/pgvector/pgvector
