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

Returns read-only operational diagnostics: RAG index state, embedding model id/dimension, LLM status, cache stats, prompt-cache stats, rate-limit counters, SQLite persistence counts, and whether metrics are enabled.

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

Response includes ranked project snippets and matching QA/wiki entries.

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
