# Qornix RAG Configuration

Standalone configuration lives in:

```text
qornix_rag/config.yaml
```

Portable bundle configuration lives in:

```text
dist/qornix_rag-portable-linux-x86_64/config/config.yaml
```

Integrated `qornix_web` applications use the host application's config and map it into the RAG runtime config contract. The standalone config file is not required for integrated or third-party use.

## Server

```yaml
server:
  address: 127.0.0.1
  port: 8081
```

- `server.address`: bind address. Keep `127.0.0.1` for local-only standalone use.
- `server.port`: HTTP port.

CLI options override config values:

```bash
./qornix_rag/run.sh --address 127.0.0.1 --port 8082 --scan-path /path/to/project
```

## Indexing

```yaml
indexing:
  # Optional. If omitted, startup directory scanning is disabled.
  scan_path: /path/to/project
  auto_index_on_startup: true
  max_file_size_kb: 4096
```

`indexing.scan_path` is the directory scan root. If it is omitted, the server starts in upload/API/QA mode without auto-scanning the filesystem. `auto_index_on_startup` only has an effect when `scan_path` is configured.

PDF ingestion uses the optional `pdftotext` command when it is available in `PATH`. Image OCR ingestion uses the optional `tesseract` command when it is available in `PATH`. DOCX ingestion uses optional `libzip` support detected at build time. XLSX/PPTX ingestion uses optional `libzip` + `pugixml` support detected at build time. CSV ingestion is built in. If an optional parser dependency is missing or a document contains no extractable text, ingestion records a structured warning and skips that file.

Chunking preserves parser metadata for page-aware PDFs, row-aware spreadsheets, OCR regions, and code symbols. The chunker uses the configured/document token budget when `tokenizer_max_tokens` or `embedding_token_limit` metadata is present; exact model-tokenizer counting remains a later enhancement.

Ask grounding checks run after answer generation. The service extracts bracketed citations from the answer, reports unsupported or unused citation ids, and can append source ids when a generated answer uses retrieved context but omits citations. Conversation history, when supplied to `/api/ask`, is bounded to recent `user`/`assistant` turns and is used only to resolve follow-up wording, not as a citable source.

## Embeddings

Fresh source checkouts do not include large ONNX model files. The safe default is TF-IDF retrieval:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true
```

This mode works without `qornix_rag/models/semantic_model.onnx` and `qornix_rag/models/tokenizer.json`.

To enable ONNX semantic retrieval, first add compatible model files:

```text
qornix_rag/models/semantic_model.onnx
qornix_rag/models/tokenizer.json
```

Recommended setup:

```bash
./qornix_rag/download_onnx_model.sh
```

The script downloads the default ONNX embedding model, writes an `embedding.registry` entry, and makes it active unless `--no-set-active` is used. Useful install variants:

```bash
./qornix_rag/download_onnx_model.sh \
  --model-id mini-lm-l6-v2 \
  --model-name all-MiniLM-L6-v2 \
  --dimension 384 \
  --max-seq-len 256
```

Then configure:

```yaml
embedding:
  backend: onnx
  model_id: local-semantic-v1
  model_name: local semantic ONNX
  model_version: v1
  model_path: qornix_rag/models/semantic_model.onnx
  tokenizer_path: qornix_rag/models/tokenizer.json
  tokenizer_type: WordPiece
  pooling: mean
  dimension: 0
  max_seq_len: 256
  onnx_threads: 2
  normalize_embeddings: true
  enable_fallback: true
  lowercase_tokens: true
```

For portable bundles, paths are rewritten to `models/semantic_model.onnx` and `models/tokenizer.json`.

`model_id` is optional. If omitted, Qornix derives a stable id from the effective backend, model path, tokenizer path, sequence length, pooling mode, version, and discovered dimension. Persisted embeddings use this id, so changing model metadata creates a new embedding namespace instead of silently reusing stale vectors.

`dimension: 0` means discover the output dimension from the backend. Set a positive dimension to require a specific ONNX output size.

If ONNX Runtime is not available or the model cannot be loaded, the build uses TF-IDF fallback when `enable_fallback` is true. In fallback mode, persisted embeddings use the TF-IDF model id rather than the requested ONNX id. See `qornix_rag/models/README.md` for where to get model files and the current compatibility limits.

For multiple installed models, use a registry map and select one with `active_model_id`:

```yaml
embedding:
  active_model_id: local-semantic-v1
  registry:
    local-semantic-v1:
      backend: onnx
      name: local semantic ONNX
      version: v1
      model_path: qornix_rag/models/semantic_model.onnx
      tokenizer_path: qornix_rag/models/tokenizer.json
      tokenizer_type: WordPiece
      pooling: mean
      dimension: 0
      max_seq_len: 256
      onnx_threads: 2
      normalize_embeddings: true
      lowercase_tokens: true
      enable_fallback: true
      license: model-specific
      source: local
    tfidf-local:
      backend: tfidf
      name: local TF-IDF
      dimension: 256
```

The registry validates backend, ONNX paths, pooling mode, dimensions, tokenizer settings, and license/source metadata. The selected active model is reflected in `/api/health`, `/api/embedding/models`, and admin diagnostics through `embedding_active_model_id`, `embedding_registry_size`, `embedding_registry_model_ids`, and `embedding_registry_warnings`.

Runtime switching is explicit and marks the next index as a full re-embed. To switch and reindex immediately:

```bash
curl -X POST http://localhost:8081/api/embedding/switch \
  -H 'Content-Type: application/json' \
  -d '{"model_id":"local-semantic-v1","reindex":true,"force_reembed":true,"scan_path":"/path/to/project"}'
```

`force_reembed` defaults to `true`. When `reindex` is false, call `/api/index` or `/api/ingest` afterwards to rebuild vectors for the selected model. The ONNX tokenizer path now uses a greedy WordPiece-style tokenizer for BERT-like vocabularies; other tokenizer JSON variants still fall back to the basic token lookup path.

## Vector Store

Hybrid search uses a retrieval-time vector store in addition to Xapian text search. The default local backend is HNSW:

```yaml
vector_store:
  backend: local_hnsw
  index_path: "qornix_rag/data/hnsw_index.bin"
  metadata_path: "qornix_rag/data/hnsw_index.meta.json"
  auto_load: true
  auto_save: true
```

When `index_path` is set, Qornix tries to load the local HNSW index before rebuilding it and saves the rebuilt index after successful indexing. The metadata sidecar validates backend, embedding model id, embedding dimension, vector count, and document/chunk snapshot hash before a saved index is reused.

For generated `rag_app` or `--with-rag` projects, use app-relative paths such as `data/hnsw_index.bin`.

Production-scale vector backends are selected through the same boundary:

```yaml
vector_store:
  backend: qdrant
  endpoint: "http://127.0.0.1:6333"
  collection: qornix_rag_vectors
  distance: Cosine
  upsert_batch_size: 512
  auto_load: false
  auto_save: false
```

```yaml
vector_store:
  backend: pgvector
  connection_string: "host=127.0.0.1 port=5432 dbname=qornix user=qornix password=secret"
  table: qornix_rag_vectors
  upsert_batch_size: 512
  auto_load: false
  auto_save: false
```

```yaml
vector_store:
  backend: faiss
  index_path: "qornix_rag/data/faiss.index"
  metadata_path: "qornix_rag/data/faiss.index.meta.json"
  auto_load: true
  auto_save: true
```

Qdrant requires CURL support at build time and a reachable Qdrant server. pgvector requires libpq support, PostgreSQL, and the `vector` extension in the target database. Faiss is optional; if Faiss headers/library are not available during CMake configuration, selecting `backend: faiss` reports a dependency-unavailable vector-store status instead of silently falling back.

Health and diagnostics responses expose backend-specific vector fields:

- `vector_store_health_status`: `ready`, `not_ready`, `config_error`, `dependency_unavailable`, `backend_unavailable`, or `disabled`;
- `vector_store_health_detail`: configuration, dependency, or last backend error detail;
- `vector_store_ready`, `vector_store_size`, and `vector_store_dimension`.

Persisted SQLite embeddings can be migrated into a deployment-scale backend with:

```bash
qornix_rag_vector_migrate \
  --sqlite-db qornix_rag/data/rag_kb.db \
  --source-id local_markdown \
  --model-id tfidf:d256 \
  --target qdrant \
  --endpoint http://127.0.0.1:6333 \
  --collection qornix_rag_vectors \
  --label-map qornix_rag/data/qdrant_label_map.json \
  --recreate
```

For pgvector, replace the target options with:

```bash
--target pgvector \
--connection-string "host=127.0.0.1 port=5432 dbname=qornix user=qornix password=secret" \
--table qornix_rag_vectors
```

## Retrieval Quality

Search can expand the user query with deterministic lexical variants and rerank first-stage vector/text results:

```yaml
search:
  use_query_expansion: true
  use_reranking: true
  rerank_input_multiplier: 3
  rerank_path_boost: 0.15
  rerank_metadata_boost: 0.10
  rerank_exact_content_boost: 0.05
```

Query expansion is local and non-LLM-based. It adds normalized tokens, simple singular variants, identifier splits, and path-like stems. API responses expose `expanded_query` and `query_expansion_applied` so rewritten retrieval input is visible.

Reranking applies after first-stage vector/Xapian retrieval. It boosts results with matching paths, chunk metadata, or exact content phrases, then returns the requested `top_k`.

### Xapian language-aware lexical retrieval

Xapian is the exact/full-text side of hybrid retrieval. It complements semantic vector search by ranking exact terms, API routes, config keys, symbols, metadata, and natural-language word forms.

```yaml
search:
  xapian_enabled: true
  # auto is a lightweight heuristic: Cyrillic text is treated as russian,
  # otherwise english is used. For non-English/non-Russian projects, set an
  # explicit Xapian language name or two-letter ISO 639 code.
  xapian_language: auto
  xapian_stemming: true
  xapian_stemming_strategy: some   # none | some | all
  xapian_cjk_ngrams: false
  xapian_word_breaks: true
  xapian_spelling: false
  xapian_metadata_prefixes: true
```

For a single-language documentation project, prefer an explicit language so indexing and query parsing use the same stemmer deterministically:

```yaml
search:
  xapian_language: en     # or english, de/german, fr/french, es/spanish, ru/russian, ...
  xapian_stemming: true
```

For code/config-only projects where exact identifiers matter more than word forms use:

```yaml
search:
  xapian_language: none
  xapian_stemming: false
```

`qornix_rag` accepts the Xapian stemmer names and aliases supported by the installed Xapian package. Common values include `en`/`english`, `de`/`german`, `fr`/`french`, `es`/`spanish`, `it`/`italian`, `pt`/`portuguese`, `nl`/`dutch`, `fi`/`finnish`, `sv`/`swedish`, `da`/`danish`, `no`/`norwegian`, `tr`/`turkish`, `ro`/`romanian`, `hu`/`hungarian`, and `ru`/`russian`. Newer Xapian versions also document stemmers such as `ar`/`arabic`, `hy`/`armenian`, `eu`/`basque`, `ca`/`catalan`, `eo`/`esperanto`, `et`/`estonian`, `el`/`greek`, `hi`/`hindi`, `id`/`indonesian`, `ga`/`irish`, `lt`/`lithuanian`, `ne`/`nepali`, `pl`/`polish`, `sr`/`serbian`, `ta`/`tamil`, and `yi`/`yiddish`. For the authoritative list for your Xapian version, see `Xapian::Stem` documentation and `Xapian::Stem::get_available_languages()`: https://xapian.org/docs/apidoc/html/classXapian_1_1Stem.html

In `auto` mode, document metadata such as `python`, `javascript`, `html`, `xml`, `pdf`, `image`, or `c/c++ header` is treated as a technical/file type and is not passed to `Xapian::Stem`. Stemming is selected only from supported natural-language stemmers; otherwise the content heuristic is used.

`xapian_metadata_prefixes: true` indexes fielded prefixes for `path:`, `source:`, `type:`, `lang:`, `meta:`, `symbol:`, `page:`, `sheet:`, and `slide:` queries. These prefixes sit beside the existing metadata filters; filters remain exact post-retrieval constraints, while prefixes improve the lexical candidate set.

Diagnostics are exposed in `/api/health`, `/api/stats`, and `/api/admin/diagnostics` under `rag.xapian` or `xapian`.

## Route Security

Standalone keeps route security disabled by default because it is intended for a local single-user process bound to `127.0.0.1`.

Generated or integrated applications can enable a lightweight RAG route guard:

```yaml
rag:
  security:
    enabled: true
    mode: admin_token
    admin_token_env: QORNIX_RAG_ADMIN_TOKEN
    token_header: X-Qornix-RAG-Admin-Token
    protect_admin_routes: true
    protect_write_routes: true
```

`mode: admin_token` accepts either `Authorization: Bearer <token>` or the configured token header. The token is read from `admin_token` first, then `admin_token_env`.

`mode: host_header` is for applications or reverse proxies that already authenticate users and forward a role header:

```yaml
rag:
  security:
    enabled: true
    mode: host_header
    role_header: X-Qornix-Role
    admin_role: admin
```

This guard is a baseline for RAG admin/write routes. It does not replace full application auth/RBAC, session management, TLS, or proxy hardening.

## LLM

Default local Ollama-style config:

```yaml
llm:
  api_url: "http://localhost:11434"
  api_key: ""
  model: "llama3"
  max_tokens: 1024
  temperature: 0.7
  top_p: 0.9
  request_timeout_ms: 30000
  stream: false
```

Diagnostics:

- `/api/health` reports provider, configured model, available models, and model availability.
- The UI shows a banner when the provider is unavailable or the configured model is missing.
- For Ollama, run `ollama pull <model>` or change `llm.model` to an installed model.


## Metrics And Observability

Prometheus-style metrics are enabled when the RAG service is created with an `LLMRAGMetrics` instance. Standalone `run.sh` and generated `rag_app` wire this collector by default.

Endpoints:

```http
GET /api/metrics
GET /api/rag/metrics
```

There is currently no separate `metrics.enabled` YAML switch in the standalone config. Metrics availability is reported by:

```http
GET /api/health
GET /api/admin/diagnostics
```

Look for:

```json
{
  "metrics_enabled": true
}
```

The metrics endpoint is intentionally lightweight and dependency-free. It exports Prometheus text format from the in-process collector. Counters and summaries reset when the process restarts.

Metrics include LLM requests/failures/duration/token usage, LLM response cache hits/misses/size, rate-limit rejections, batch question counters, and latest indexing file/line gauges.

Protect `/api/metrics` or `/api/rag/metrics` with the same care as diagnostics endpoints when exposing the app beyond localhost.

## Cache And Rate Limit

The LLM response cache is applied after retrieval and prompt construction. It caches completed answers, not documents, chunks, embeddings, Xapian data or vector indexes.

```yaml
cache:
  enabled: true
  backend: "memory" # memory | redis
  ttl_seconds: 3600
  max_size: 1000
  key_prefix: "qornix_rag:"

  redis:
    host: "127.0.0.1"
    port: 6379
    db: 0
    password: ""
    ttl_seconds: 3600

rate_limit:
  enabled: true
  max_requests_per_second: 10
  max_requests_per_minute: 100
  per_ip_limit: true
```

`backend: memory` uses the in-process LRU cache. It is fastest and simplest for local use, but is cleared on restart and is not shared between app instances.

`backend: redis` uses a Redis server through a built-in RESP TCP client; no `hiredis` library is required. Redis is useful for shared cache across several RAG instances and for Redis-managed TTL/eviction/persistence. If Redis is unavailable during startup, the cache factory logs a warning and falls back to `memory` so the RAG service can still run.

Standalone config uses top-level `cache`. Generated `rag_app` config uses the same keys under `rag.cache`.

These settings apply to LLM requests in standalone mode and generated RAG apps.

## Persistent QA And Markdown

```yaml
rag:
  sqlite:
    enabled: true
    db_path: "qornix_rag/data/rag_kb.db"
    source_id: "sqlite_kb"
    name: "SQLite Knowledge Base"
    auto_migrate: true

  markdown:
    enabled: true
    directory_path: "qornix_rag/knowledge_base"
    recursive: true
```

SQLite QA pairs store `category`, `aliases`, string `metadata`, and optional `tags` metadata. QA list/export APIs can filter by `query`, `category`, and `tag`; Ask/Search include QA attribution metadata when available. Stored answers are returned both as Markdown text and as a small rendered `answer_html` field for UI display.

Portable config rewrites these to:

```yaml
db_path: "data/rag_kb.db"
directory_path: "knowledge_base"
```

## Runtime Environment

`run.sh` sets:

```text
QORNIX_RAG_HOME
QORNIX_RAG_TEMPLATES_DIR
```

The portable bundle's `run.sh` also sets `LD_LIBRARY_PATH` to its local `lib/` directory.

## Operational Profiles

Use these profiles as starting points rather than as separate config files.

### Lightweight local profile

Best for first run, small projects, and machines without ONNX Runtime:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true

vector_store:
  backend: local_hnsw
  auto_load: true
  auto_save: true

search:
  use_hybrid: true
  use_query_expansion: true
  use_reranking: true
  use_multi_query_retrieval: true

upload:
  enabled: true
  auto_ingest: true
```

### Full semantic local profile

Best when ONNX Runtime and a compatible text embedding model are installed:

```yaml
embedding:
  backend: onnx
  active_model_id: local-semantic-v1
  models_dir: qornix_rag/models
  auto_discover_models: true
  validate_model_files: true
  enable_fallback: true
  registry:
    local-semantic-v1:
      backend: onnx
      model_path: qornix_rag/models/semantic_model.onnx
      tokenizer_path: qornix_rag/models/tokenizer.json
      tokenizer_type: WordPiece
      pooling: mean
      dimension: 384
      max_seq_len: 256
      onnx_threads: 2
      normalize_embeddings: true

search:
  use_hybrid: true
  use_multi_query_retrieval: true
  use_embedding_reranker: true
```

### External vector-store profile

Use this when the vector index should live outside the process:

```yaml
vector_store:
  backend: qdrant
  endpoint: http://127.0.0.1:6333
  collection: qornix_rag_vectors
  distance: Cosine
```

or:

```yaml
vector_store:
  backend: pgvector
  connection_string: host=127.0.0.1 port=5432 dbname=qornix user=qornix password=secret
  table: qornix_rag_vectors
  distance: Cosine
```

## Upload Configuration

Standalone:

```yaml
upload:
  enabled: true
  uploads_dir: qornix_rag/data/uploads
  max_file_size_kb: 16384
  max_files_per_request: 20
  auto_ingest: true
  async_ingest: false
  overwrite_existing: false
  allowed_extensions: ".txt,.md,.rst,.adoc,.json,.yaml,.yml,.xml,.html,.htm,.csv,.pdf,.docx,.xlsx,.pptx,.png,.jpg,.jpeg,.tif,.tiff,.bmp,.webp"
```

Generated `rag_app` uses the same keys under `rag.upload` and app-relative paths:

```yaml
rag:
  upload:
    uploads_dir: data/uploads
```

Upload is intentionally allowlist-based. Do not add executables, archives, scripts or arbitrary binary formats unless a separate scanning/sandboxing step is added.

## Dependency-to-Feature Matrix

| Feature | Config area | Build/runtime dependency |
|---|---|---|
| YAML config | all | `yaml-cpp` |
| Local HNSW | `vector_store.backend: local_hnsw` | `hnswlib` |
| Xapian lexical index | search | `libxapian-dev` |
| LLM client | `llm.*` | `libcurl` and reachable provider |
| SQLite QA/wiki | `rag.sqlite` | `sqlite3` |
| PDF ingestion | ingestion | `pdftotext` from `poppler-utils` |
| OCR ingestion | ingestion | `tesseract-ocr` |
| DOCX ingestion | ingestion | `libzip` |
| XLSX/PPTX ingestion | ingestion | `libzip` + `pugixml` |
| ONNX embeddings | `embedding.backend: onnx` | ONNX Runtime C++ SDK + compatible model/tokenizer |
| Faiss vector store | `vector_store.backend: faiss` | Faiss headers/library at build time |
| Qdrant vector store | `vector_store.backend: qdrant` | running Qdrant service |
| pgvector vector store | `vector_store.backend: pgvector` | `libpq` at build time and PostgreSQL + pgvector at runtime |

For install commands and model download examples, see [Full RAG guide](FULL_RAG_GUIDE.md).
