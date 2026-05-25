# Operations for Generated RAG Apps

This document applies to generated `rag_app` projects and to default generated apps created with `--with-rag`.

Standalone `qornix_rag` is local-first and does not require auth. A generated Qornix Web app can be exposed on a network, so auth, TLS, proxy rules, and deployment security belong to the host application or infrastructure layer.

## Runtime Paths

Persist these directories in production-like deployments:

```text
data/            SQLite QA and persisted RAG metadata
knowledge_base/  Markdown/text knowledge source files
logs/            application logs when file logging is enabled
models/          optional local ONNX embedding model files
```

Do not bake user data, SQLite databases, logs, local model licenses, or API keys into a container image or release artifact.

## Health And Diagnostics

Readiness-style health:

```http
GET /api/rag/health
```

Admin diagnostics:

```http
GET /api/rag/admin/diagnostics
```

Diagnostics include RAG index state, embedding model identity, LLM status, cache statistics, rate-limit counters, SQLite persistence counts, and RAG route auth status. Protect this endpoint with host-app auth, an internal network, or a reverse proxy rule before exposing it outside a trusted environment.

Prometheus-style RAG metrics:

```http
GET /api/rag/metrics
```

The generated application may also expose host-level Qornix Web metrics if the host app registers them.

## Rate Limits

RAG rate limiting is configured under:

```yaml
rag:
  rate_limit:
    enabled: true
    max_requests_per_second: 10
    max_requests_per_minute: 100
    per_ip_limit: true
    max_requests_per_second_per_ip: 2
    max_requests_per_minute_per_ip: 30
```

Use stricter limits when LLM calls are enabled or the app is exposed beyond localhost.

## RAG Route Security

Enable the baseline route guard for generated apps that expose RAG admin or write routes:

```yaml
rag:
  security:
    enabled: true
    mode: admin_token
    admin_token_env: QORNIX_RAG_ADMIN_TOKEN
    protect_admin_routes: true
    protect_write_routes: true
```

Then set `QORNIX_RAG_ADMIN_TOKEN` in the runtime environment. Requests can use either `Authorization: Bearer <token>` or `X-Qornix-RAG-Admin-Token: <token>`.

Use `mode: host_header` only behind an authenticated host application or reverse proxy that forwards the configured admin role header.

## Logging

Generated apps use the host application's logging config:

```yaml
logging:
  enabled: true
  level: info
  to_file: true
  file_path: logs/server
  rotation_size: 10485760
  max_files: 5
```

Mount `logs/` as a persistent volume when using Docker Compose.

## Docker Compose

The template's `docker-compose.yml` persists:

- `knowledge_base/`;
- `data/`;
- `logs/`;
- Ollama model storage through the `ollama` named volume.

Before starting a generated app with Compose, create local runtime folders:

```bash
mkdir -p data knowledge_base logs
docker compose up --build
```

## Backup And Restore

Baseline backup is file-based:

```bash
mkdir -p backups
tar -czf backups/rag-runtime-$(date +%Y%m%d-%H%M%S).tar.gz data knowledge_base models
```

Restore into a stopped application:

```bash
tar -xzf backups/rag-runtime-YYYYMMDD-HHMMSS.tar.gz
```

After restore, start the app and call:

```http
POST /api/rag/index
```

This rebuilds the in-memory retrieval index from the restored data.

## Security Checklist

- Keep `server.address: 127.0.0.1` for local-only deployments.
- Use `0.0.0.0` only behind a trusted network boundary or reverse proxy.
- Protect `/api/rag/admin/diagnostics`, `/api/rag/metrics`, QA write, ingestion, and document delete endpoints with `rag.security` or an upstream auth layer.
- Store LLM API keys outside Git and container images.
- Review upload/indexing path allowlists before enabling arbitrary user-controlled sources.
- Keep `rag.indexing.max_file_size_kb` conservative for shared deployments.
