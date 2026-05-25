# Qornix RAG API

Standalone base URL:

```text
http://localhost:8081
```

Integrated mode may use a prefix such as:

```text
/rag
/api/rag/*
```

## Health

```http
GET /api/health
```

Returns RAG index state and LLM diagnostics.

Important fields:

- `rag.indexed`
- `rag.files`
- `rag.embedding_backend`
- `rag.embedding_model_id`
- `rag.embedding_dim`
- `rag.vector_store_backend`
- `rag.vector_store_status`
- `rag.query_expansion`
- `rag.reranking`
- `llm.status`
- `llm.provider`
- `llm.model`
- `llm.configured_model_available`
- `llm.available_models`

## Admin Diagnostics

```http
GET /api/admin/diagnostics
```

Integrated mode uses the configured prefix, for example:

```http
GET /api/rag/admin/diagnostics
```

Returns read-only operational diagnostics: RAG index state, embedding model id/dimension, vector store status, query expansion/reranking flags, LLM status, cache stats, prompt-cache stats, rate-limit counters, SQLite persistence counts, route auth status, and whether metrics are enabled.

When `rag.security.enabled` is true, admin routes and write routes are protected by the configured baseline guard:

```http
Authorization: Bearer <token>
X-Qornix-RAG-Admin-Token: <token>
```

For `mode: host_header`, the host application or reverse proxy must provide the configured admin role header.

Protect this endpoint with host-application auth, an internal network, or a reverse proxy rule before exposing it outside a trusted environment.

## Stats

```http
GET /api/stats
```

Returns indexing statistics, embedding status, and indexed project root.

## Sources

```http
GET /api/sources
```

Lists registered data sources.

## Index

```http
POST /api/index
```

## Ingestion

```http
POST /api/ingest
```

Runs a synchronous ingestion/indexing job by default:

```json
{
  "project_path": "/path/to/project"
}
```

For background ingestion, pass `async: true` or `background: true`:

```json
{
  "project_path": "/path/to/project",
  "async": true
}
```

The async response returns `202 Accepted` with a job object. Poll:

```http
GET /api/ingest/{id}
GET /api/ingest/jobs
```

Job fields include `status`, `progress_percent`, `files_seen`, `documents_imported`, `skipped`, `errors`, and timestamps when persisted storage is enabled.

Index and ingestion stats include incremental embedding counters:

- `indexed_chunks`
- `reused_embeddings`
- `generated_embeddings`
- `stale_embeddings`

Request:

```json
{
  "project_path": "/path/to/project"
}
```

`project_path` is optional. If omitted, the current indexed project root or configured project path is used.

## Search

```http
POST /api/search
```

Request:

```json
{
  "query": "How does routing work?",
  "top_k": 5,
  "full_context": false
}
```

Response includes ranked project snippets and matching QA/wiki entries. Retrieval quality metadata includes:

- `expanded_query`
- `query_expansion_applied`
- `reranking_applied`
- per-result `confidence`, `vector_score`, `text_score`, and `fused_score`

## Ask

```http
POST /api/ask
```

Request:

```json
{
  "question": "How does routing work?",
  "top_k": 5
}
```

Response:

```json
{
  "success": true,
  "question": "...",
  "expanded_query": "...",
  "query_expansion_applied": true,
  "reranking_applied": true,
  "answer": "...",
  "context": [],
  "sources": [],
  "llm_status": "ok",
  "response_time_ms": 1234
}
```

`llm_status` may be:

- `ok`
- `fallback`
- `unavailable`
- `not_configured`

## QA

Add:

```http
POST /api/qa/add
```

```json
{
  "question": "How do I run standalone RAG?",
  "answer": "Use ./qornix_rag/run.sh",
  "category": "setup"
}
```

List:

```http
GET /api/qa/list?per_page=25&page=1
```

Update:

```http
POST /api/qa/update
```

```json
{
  "pair_id": "sqlite_kb_qa_...",
  "question": "Updated question",
  "answer": "Updated answer",
  "category": "setup"
}
```

Delete:

```http
POST /api/qa/delete
```

```json
{
  "pair_id": "sqlite_kb_qa_..."
}
```

QA entries participate in Ask context and Search results.

## Optional Endpoints

When the corresponding services are enabled:

- `POST /api/batch`
- `GET /api/metrics`
- `GET /api/admin/diagnostics`
- `GET /api/analytics`
- `GET /api/analytics/gaps`
- `POST /api/analytics/export`
- `POST /api/qa/dedup`
- `POST /api/qa/dedup/remove`
- `POST /api/import/markdown`
- `GET /api/import/history`
