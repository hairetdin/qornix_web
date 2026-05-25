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
  max_file_size_kb: 512
```

Portable config sets `auto_index_on_startup: false` by default so the bundle can start without indexing itself.

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

The script downloads the default ONNX embedding model and updates `qornix_rag/config.yaml`.

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

## Vector Store

Hybrid search uses a retrieval-time vector store in addition to Xapian text search. The current local backend is HNSW:

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
