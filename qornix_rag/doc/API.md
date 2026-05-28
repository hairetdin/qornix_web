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


## API Route Map

Standalone examples in this document use `/api/...`. Generated `rag_app` projects mount the same RAG API under `/api/rag/...`. For example:

| Operation | Standalone | Generated `rag_app` |
|---|---|---|
| Health | `GET /api/health` | `GET /api/rag/health` |
| Search | `POST /api/search` | `POST /api/rag/search` |
| Ask | `POST /api/ask` | `POST /api/rag/ask` |
| Upload | `POST /api/documents/upload` | `POST /api/rag/documents/upload` |
| Upload delete | `POST /api/uploads/delete` | `POST /api/rag/uploads/delete` |
| Reindex | `POST /api/index` | `POST /api/rag/index` |
| QA collection | `GET/POST /api/qa` | `GET/POST /api/rag/qa` |
| QA compatibility routes | `/api/qa/*` | `/api/rag/qa/*` |
| Embedding registry | `GET /api/embedding/models` | `GET /api/rag/embedding/models` |
| Metrics | `GET /api/metrics` | `GET /api/rag/metrics` |
| Analytics | `GET /api/analytics` | `GET /api/rag/analytics` |

See [FULL_RAG_GUIDE.md](FULL_RAG_GUIDE.md) for the feature/dependency/model guide.

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
- `rag.xapian.enabled`
- `rag.xapian.ready`
- `rag.xapian.status`
- `rag.xapian.language`
- `rag.xapian.effective_index_language`
- `rag.xapian.effective_query_language`
- `rag.xapian.stemming`
- `rag.xapian.stemming_strategy`
- `rag.xapian.cjk_ngrams`
- `rag.xapian.metadata_prefixes`
- `llm.status` (`ok`, `model_not_found`, `models_unreported`, `provider_unavailable`, `not_configured`)
- `llm.provider`
- `llm.model`
- `llm.available` / `llm.provider_available` — provider transport is reachable
- `llm.ready` — provider is reachable and the configured model is usable
- `llm.configured_model_available`
- `llm.available_models`

LLM provider diagnostics use structured `Boost.JSON` parsing for model-list
responses from Ollama `/api/tags` and OpenAI-compatible `/v1/models`
providers, including vLLM and LM Studio. `llm.available` means the provider
transport/model-list endpoint is reachable; `llm.ready` additionally requires
the configured model to be usable when the provider reports model names.

## Admin Diagnostics

```http
GET /api/admin/diagnostics
```

Integrated mode uses the configured prefix, for example:

```http
GET /api/rag/admin/diagnostics
```

Returns read-only operational diagnostics: RAG index state, embedding model id/dimension, vector store status, Xapian language/stemming diagnostics, query expansion/reranking flags, LLM status, cache backend/availability/stats, prompt-cache stats, rate-limit counters, SQLite persistence counts, route auth status, and whether metrics are enabled.

When `rag.security.enabled` is true, admin routes and write routes are protected by the configured baseline guard:

```http
Authorization: Bearer <token>
X-Qornix-RAG-Admin-Token: <token>
```

For `mode: host_header`, the host application or reverse proxy must provide the configured admin role header.

Protect this endpoint with host-application auth, an internal network, or a reverse proxy rule before exposing it outside a trusted environment.

The cache object includes `enabled`, `backend`, `available`, `hits`, `misses`, `size`, `max_size`, and `hit_rate_percent`. For Redis, `size` is an approximate runtime counter; Redis capacity/eviction are controlled by the Redis server.

## Stats

```http
GET /api/stats
```

Returns indexing statistics, embedding status, indexed project root, and cache diagnostics when available.

## Embedding Models

```http
GET /api/embedding/models
```

Returns the configured and auto-discovered embedding registry, active model id, effective runtime model id, backend, readiness/status fields, tokenizer diagnostics, effective chunk token limit, cache/signature metadata, and registry warnings.

```http
POST /api/embedding/switch
```

Request:

```json
{
  "model_id": "local-semantic-v1",
  "reindex": true,
  "force_reembed": true,
  "scan_path": "/path/to/project"
}
```

`model_id` must exist in `embedding.registry` or be auto-discovered from `embedding.models_dir`. `force_reembed` defaults to true and prevents unchanged chunks from reusing vectors generated under the previous model/signature. The model signature includes backend, model/tokenizer paths, tokenizer type, dimension, pooling, normalization, casing, and sequence length, so tokenizer/model changes force safe rebuilds instead of reusing incompatible vectors. If `reindex` is true, the service indexes immediately and returns normal indexing counters in `stats`; otherwise the response reports `reindex_required: true` and the next `/api/index` or `/api/ingest` performs the re-embed.


Response model objects include hardening fields such as:

```json
{
  "id": "local-mini-encoder",
  "backend": "onnx",
  "tokenizer_type": "BPE",
  "tokenizer_vocab_size": 30522,
  "effective_chunk_token_limit": 254,
  "files_present": true,
  "discovered": true,
  "persistent_cache_enabled": true,
  "tokenizer_status": "tokenizer loaded: type=BPE, vocab=30522, max_seq_len=256",
  "model_signature": "..."
}
```

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
  "scan_path": "/path/to/project"
}
```

For background ingestion, pass `async: true` or `background: true`:

```json
{
  "scan_path": "/path/to/project",
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

Chunk metadata preserves parser structure where available: PDF chunks include page fields, CSV/XLSX chunks include sheet/row fields, DOCX chunks include heading/block fields, PPTX chunks include slide fields, image OCR chunks include OCR region fields, and source-code chunks include symbol name/kind/scope fields. `chunk_token_budget` records the effective token budget used by the chunker.

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
  "scan_path": "/path/to/project"
}
```

`scan_path` is optional only when the engine already has a current indexed root. If neither is available, directory indexing returns an error instead of scanning the process working directory.

## Secure Document Uploads

```http
POST /api/documents/upload
Content-Type: multipart/form-data
```

Uploads one or more files through the web/API layer, stores them under the configured `upload.uploads_dir`, validates every file against extension/MIME/size allowlists, and optionally starts ingestion immediately. Use multipart field name `files` for each file.

Optional form fields or query parameters:

- `auto_ingest=true|false` - defaults to `upload.auto_ingest`.
- `async=true|false` - defaults to `upload.async_ingest`.

Example with curl:

```bash
curl -X POST http://localhost:8081/api/documents/upload \
  -F 'files=@report.pdf' \
  -F 'files=@notes.md' \
  -F 'auto_ingest=true'
```

Response fields include:

- `files[].relative_path` - controlled path under `uploads_dir`; use it for deletion.
- `validation[]` - per-file filename, extension, MIME, size, and validation result.
- `job` - ingestion job object when `auto_ingest` is true.
- `stats` - synchronous reindex stats when `async=false`.

Uploaded documents are made searchable by reindexing the current project root, not just the upload batch directory. This preserves already indexed local files and adds the uploaded files to the same HNSW/Xapian hybrid index.

```http
POST /api/uploads/delete
```

Deletes a previously uploaded physical file only if it resolves inside `upload.uploads_dir`, removes the persisted document snapshot when SQLite is enabled, and reindexes by default.

```json
{
  "relative_path": "qornix_rag/data/uploads/upload_123/report.pdf",
  "reindex": true
}
```

Upload configuration keys:

```yaml
upload:
  enabled: true
  uploads_dir: qornix_rag/data/uploads
  max_file_size_kb: 16384
  max_files_per_request: 20
  auto_ingest: true
  async_ingest: false
  allowed_extensions: ".txt,.md,.json,.csv,.pdf,.docx,.xlsx,.pptx,.png,.jpg"
  allowed_mime_types: "text/plain,text/markdown,text/csv,application/pdf,image/png,image/jpeg"
```

When route auth is enabled, upload and delete endpoints are protected as write routes.

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

Metadata-aware filters are optional and can be supplied either as a list or as a compact object:

```json
{
  "query": "roadmap",
  "top_k": 5,
  "filters": [
    {"key": "type", "op": "equals", "value": "pdf"},
    {"key": "page", "op": "equals", "value": "2"}
  ]
}
```

```json
{
  "query": "revenue",
  "filters": {
    "type": "xlsx",
    "sheet": "Revenue",
    "metadata_gte": {"chunk_row_start": 10}
  }
}
```

Supported filter operations are `equals`, `contains`, `prefix`, `exists`, `not_empty`, `gte`, and `lte`. Common normalized keys include `type`, `language`, `source_path`, `page`, `sheet`, `row`, `slide`, `symbol`, and `ocr_confidence`; raw metadata keys such as `chunk_symbol_name`, `metadata_contract`, or `image_ocr_confidence_avg` are also accepted.

Response includes ranked project snippets and matching QA/wiki entries. Retrieval quality metadata includes:

- `expanded_query` and `rewritten_query`
- `query_expansion_applied`
- `multi_query_applied`, `retrieval_strategy`, and per-variant `retrieval_queries` diagnostics
- `reranking_applied` and `reranker_type`
- `filters_applied` and normalized `filters` when metadata filters are provided
- per-result `confidence`, `vector_score`, `text_score`, and `fused_score`
- per-result `source_locator`, `citation_label`, and curated structural `metadata` when available

## Ask

```http
POST /api/ask
```

Request:

```json
{
  "question": "How does routing work?",
  "top_k": 5,
  "system_prompt": "You are a concise technical assistant.",
  "prompt_template": "Context:\n{context}\n\nQuestion: {question}"
}
```

`/api/ask` accepts the same `filters` / `metadata_filters` payload as `/api/search`. Filtered Ask requests restrict project context by metadata and omit QA/wiki fallback entries so citations remain scoped to the requested document subset.

`system_prompt` and `prompt_template` are optional per-request overrides. If omitted, the configured `llm.system_prompt` and `llm.prompt_template` are used. The prompt template should include `{question}` and can include `{context}`.

Response:

```json
{
  "success": true,
  "question": "...",
  "expanded_query": "...",
  "rewritten_query": "...",
  "query_expansion_applied": true,
  "multi_query_applied": true,
  "retrieval_strategy": "multi_query_union",
  "retrieval_queries": [],
  "reranking_applied": true,
  "reranker_type": "embedding_model_semantic_reranker",
  "answer": "...",
  "context": [],
  "sources": [],
  "grounding_status": "grounded",
  "grounding_evaluator": "citation_lexical_claim_grounding",
  "claim_grounding_status": "grounded",
  "grounded_claims": [],
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


### Quality diagnostics and feedback

Search and Ask use a quality pipeline beyond the deterministic baseline:

1. query expansion keeps the original query visible as `expanded_query`;
2. query rewriting builds a compact high-signal `rewritten_query`;
3. multi-query retrieval runs a bounded union of original, expanded, rewritten, and keyword variants and returns per-variant `retrieval_queries`;
4. the active embedding model participates in semantic reranking when available;
5. Ask responses expose citation/claim grounding diagnostics through `grounding_evaluator`, `claim_grounding_status`, and `grounded_claims`.

User feedback can be captured for quality analytics:

```http
POST /api/feedback
```

```json
{
  "request_id": "optional-client-request-id",
  "query": "How does routing work?",
  "question": "How does routing work?",
  "rating": "not_helpful",
  "category": "bad_citation",
  "comment": "The answer cited the wrong source.",
  "citations": ["S1"]
}
```

Feedback is summarized in `/api/analytics` and exported through `/api/analytics/export`. Suggested `category` values are `helpfulness`, `missing_context`, `bad_citation`, and `wrong_answer`.

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
      "answer_html_sanitized": true,
      "markdown_renderer": "qornix_markdown_safe_v1",
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

`pairs` is kept as a compatibility alias for older UI code. New clients should use `items`. When SQLite FTS5 is available, `query=` uses the QA FTS index; otherwise the service falls back to bounded `LIKE` matching. `tag=` uses the normalized `qa_tags` table instead of scanning JSON metadata.

Duplicate preview before add/update:

```http
POST /api/qa/duplicate-check
```

```json
{
  "question": "How do I run standalone RAG?",
  "answer": "Use ./qornix_rag/run.sh",
  "category": "setup",
  "exclude_pair_id": "sqlite_kb_qa_...",
  "threshold": 0.72,
  "limit": 5
}
```

Response includes `warning`, `duplicates_found`, `checked_pairs`, and a bounded `candidates` array with `pair_id`, `question`, `category`, `similarity`, and `reason`. The standalone UI calls this endpoint before saving add/edit forms and asks for confirmation when likely duplicates are found.

Suggestions:

```http
GET /api/qa/suggest?q=rag&limit=10
GET /api/qa/categories?q=set&limit=20
GET /api/qa/tags?q=local&limit=20
```

`/api/qa/tags` is backed by the normalized tag index when SQLite is enabled, so large QA/wiki tables do not need to load all QA rows for autocomplete.

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

Version history/provenance:

```http
GET /api/qa/history?pair_id=sqlite_kb_qa_...&limit=25
```

Returns append-only create/update/delete history with `version`, `action`, `question`, `answer`, `category`, `aliases`, `metadata`, `changed_at`, and `source_id`.

Delete:

```http
POST /api/qa/delete
```

```json
{
  "pair_id": "sqlite_kb_qa_..."
}
```

QA entries participate in Ask context and Search results. Richer Markdown rendering is intentionally sanitized server-side: raw HTML is escaped, only a small safe subset is emitted, and Markdown links are allowed only for relative, `http`, `https`, and `mailto` targets.

QA import/export:

```http
POST /api/qa/import
GET /api/qa/export?category=setup&tag=local
POST /api/qa/export
```

Import accepts either a JSON array or an object with `pairs`. Export returns `pairs` with `aliases`, `tags`, and string metadata. Search and Ask context for QA entries include `tags`, `attribution`, `category`, `pair_id`, confidence, and `Q*` citation ids.


## Prometheus Metrics

Standalone RAG exposes Prometheus-compatible text metrics at:

```http
GET /api/metrics
```

Generated `rag_app` exposes the same RAG metrics under the generated API prefix:

```http
GET /api/rag/metrics
```

The response uses Prometheus text exposition format:

```http
Content-Type: text/plain; version=0.0.4; charset=utf-8
```

Example:

```bash
curl -fsS http://127.0.0.1:8082/api/metrics
curl -fsS http://127.0.0.1:8008/api/rag/metrics
```

Currently exported RAG metrics include:

| Metric | Type | Meaning |
|---|---|---|
| `qornix_rag_llm_requests_total` | counter | Completed LLM/RAG answer requests observed by the metrics layer |
| `qornix_rag_llm_requests_failed_total` | counter | Failed LLM/RAG answer requests |
| `qornix_rag_llm_request_duration_seconds` | summary | LLM/RAG request duration summary with `_sum` and `_count` samples |
| `qornix_rag_llm_tokens_total` | counter | Token usage reported by the provider/client path when available |
| `qornix_rag_cache_hits_total` | counter | LLM response cache hits |
| `qornix_rag_cache_misses_total` | counter | LLM response cache misses |
| `qornix_rag_cache_size_gauge` | gauge | Current in-process cache size snapshot |
| `qornix_rag_rate_limit_rejections_total` | counter | Requests rejected by the RAG rate limiter |
| `qornix_rag_batch_questions_total` | counter | Questions submitted through batch processing |
| `qornix_rag_batch_completed_total` | counter | Batch questions completed successfully |
| `qornix_rag_indexed_files_count` | gauge | Number of indexed files from the latest index build snapshot |
| `qornix_rag_indexed_lines_count` | gauge | Number of indexed source/text lines from the latest index build snapshot |

The metrics collector is in-process and does not require `prometheus-cpp` or any external library. It renders counters, gauges and simple summaries directly from `qornix_rag/prometheus_metrics.*`.

Use `/api/health` or `/api/admin/diagnostics` to check whether metrics are available via `metrics_enabled`. In generated applications, protect `/api/rag/metrics` with `rag:admin`, host-auth or a reverse proxy rule before exposing the service outside a trusted network.

Example Prometheus scrape job:

```yaml
scrape_configs:
  - job_name: qornix-rag
    metrics_path: /api/metrics
    static_configs:
      - targets: ['127.0.0.1:8082']
```

For generated apps:

```yaml
scrape_configs:
  - job_name: qornix-rag-app
    metrics_path: /api/rag/metrics
    static_configs:
      - targets: ['127.0.0.1:8008']
```

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
- `POST /api/qa/duplicate-check`
- `GET /api/qa/history`
- `POST /api/qa/import`
- `GET/POST /api/qa/export`
- `POST /api/import/markdown`
- `GET /api/import/history`
