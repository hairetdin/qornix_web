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
  project_path: .
```

- `server.address`: bind address. Keep `127.0.0.1` for local-only standalone use.
- `server.port`: HTTP port.
- `server.project_path`: default project path for startup indexing and reindexing.

CLI options override config values:

```bash
./qornix_rag/run.sh --address 127.0.0.1 --port 8082 --project /path/to/project
```

## Indexing

```yaml
indexing:
  auto_index_on_startup: true
  max_file_size_kb: 4096
```

Portable config sets `auto_index_on_startup: false` by default so the bundle can start without indexing itself.

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
  -d '{"model_id":"local-semantic-v1","reindex":true,"force_reembed":true}'
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

## Cache And Rate Limit

```yaml
cache:
  enabled: true
  backend: "memory"
  ttl_seconds: 3600
  max_size: 1000

rate_limit:
  enabled: true
  max_requests_per_second: 10
  max_requests_per_minute: 100
  per_ip_limit: true
```

These settings apply to LLM requests in standalone mode.

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
