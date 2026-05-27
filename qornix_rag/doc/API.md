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
- `rag.vector_store_health_status`
- `rag.vector_store_health_detail`
- `rag.vector_store_ready`
- `rag.vector_store_size`
- `rag.vector_store_dimension`
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

## Embedding Models

```http
GET /api/embedding/models
```

Returns the configured embedding registry, active model id, effective runtime model id, backend, readiness/status fields, and registry warnings.

```http
POST /api/embedding/switch
```

Request:

```json
{
  "model_id": "local-semantic-v1",
  "reindex": true,
  "force_reembed": true,
  "project_path": "/path/to/project"
}
```

`model_id` must exist in `embedding.registry`. `force_reembed` defaults to true and prevents unchanged chunks from reusing vectors generated under the previous model. If `reindex` is true, the service indexes immediately and returns normal indexing counters in `stats`; otherwise the response reports `reindex_required: true` and the next `/api/index` or `/api/ingest` performs the re-embed.

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

Supported filesystem ingestion formats include source/config/text files, Markdown, HTML, CSV, PDF text extraction through optional `pdftotext`, image OCR through optional `tesseract`, DOCX text extraction through optional `libzip`, and XLSX/PPTX text extraction through optional `libzip` + `pugixml`. If an optional parser dependency is unavailable, matching files are skipped with a structured warning instead of failing the whole job.

Chunk metadata preserves parser structure where available: PDF chunks include page fields, CSV/XLSX chunks include sheet/row fields, image OCR chunks include OCR region fields, and source-code chunks include symbol name/kind fields. `chunk_token_budget` records the effective token budget used by the chunker.

Ask responses include citation post-processing and grounding metadata:

- `answer_citations` - bracketed citation ids found in the generated answer.
- `missing_citations` - answer citation ids that were not present in retrieved context.
- `uncited_context_citations` - retrieved context ids not cited by the answer.
- `citations_post_processed` - true when the service appended source ids to an uncited generated answer.
- `conversation_turns_used` - sanitized recent `user`/`assistant` history turns included for follow-up wording only.

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
  "llm_truncated": false,
  "llm_finish_reason": "",
  "llm_parser_error": "",
  "response_time_ms": 1234
}
```

`llm_status` may be:

- `ok`
- `fallback`
- `unavailable`
- `not_configured`
- `parser_error`
- `provider_error`
- `truncated`
- `rate_limited`
- `cache_hit`

`llm_parser_error` is populated when the provider returned a response that could not be parsed as supported JSON. `llm_truncated` and `llm_finish_reason` are populated when the provider reports an incomplete answer, such as OpenAI-compatible `finish_reason: "length"` or an incomplete Ollama stream/chunk.

## QA

Add:

```http
POST /api/qa/add
```

```json
{
  "question": "How do I run standalone RAG?",
  "answer": "Use ./qornix_rag/run.sh",
  "category": "setup",
  "tags": ["local", "startup"],
  "aliases": ["How do I start local RAG?"],
  "metadata": {
    "source": "operator runbook"
  }
}
```

List:

```http
GET /api/qa/list?limit=25&offset=0
GET /api/qa/list?query=rag&category=setup&tag=local&limit=25&offset=0
```

Response:

```json
{
  "success": true,
  "items": [
    {
      "id": "sqlite_kb_qa_...",
      "question": "How do I run standalone RAG?",
      "answer": "Use ./qornix_rag/run.sh",
      "answer_html": "<p>Use ./qornix_rag/run.sh</p>",
      "category": "setup",
      "aliases": [],
      "tags": ["local", "startup"],
      "metadata": {
        "source": "operator runbook"
      }
    }
  ],
  "pairs": [],
  "total": 42,
  "limit": 25,
  "offset": 0,
  "has_more": true
}
```

`pairs` is kept as a compatibility alias for older UI code. New clients should use `items`.

Suggestions:

```http
GET /api/qa/suggest?q=rag&limit=10
GET /api/qa/categories?q=set&limit=20
GET /api/qa/tags?q=local&limit=20
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
  "category": "setup",
  "tags": ["local"],
  "aliases": ["Updated alias"],
  "metadata": {
    "source": "runbook"
  }
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

QA import/export:

```http
POST /api/qa/import
GET /api/qa/export?category=setup&tag=local
POST /api/qa/export
```

Import accepts either a JSON array or an object with `pairs`. Export returns `pairs` with `aliases`, `tags`, and string metadata. Search and Ask context for QA entries include `tags`, `attribution`, `category`, `pair_id`, confidence, and `Q*` citation ids.

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
- `POST /api/qa/import`
- `GET/POST /api/qa/export`
- `POST /api/import/markdown`
- `GET /api/import/history`
