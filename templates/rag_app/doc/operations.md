# Operations for Generated RAG Apps

This document applies to generated `rag_app` projects and to generated host apps created with `--with-rag`.

Standalone `qornix_rag` is local-first and does not require auth. A generated Qornix Web app can be exposed on a network, so auth, TLS, proxy rules, and deployment security belong to the host application or infrastructure layer.

## Runtime Paths

Persist these directories in production-like deployments:

```text
data/            SQLite QA and persisted RAG metadata
knowledge_base/  Markdown/text knowledge source files
logs/            application logs when file logging is enabled
models/          optional local ONNX embedding model files
data/uploads/    files uploaded through the RAG UI/API
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

The endpoint returns Prometheus text exposition format:

```bash
curl -fsS http://127.0.0.1:8008/api/rag/metrics
```

The built-in collector is in-process and does not require `prometheus-cpp`. It exports LLM request counters, failed request counters, request duration summaries, token counters when reported by the provider/client path, LLM response cache hits/misses/size, rate-limit rejections, batch counters, and latest indexing file/line gauges.

Prometheus scrape example:

```yaml
scrape_configs:
  - job_name: qornix-rag-app
    metrics_path: /api/rag/metrics
    static_configs:
      - targets: ['127.0.0.1:8008']
```

The generated application may also expose host-level Qornix Web metrics if the host app registers them. Protect RAG metrics with `rag:admin`, host-auth, internal networking, or reverse-proxy rules before exposing the application to other users.


## Upload Operations

Generated apps expose secure upload routes under the RAG API prefix:

```http
POST /api/rag/documents/upload
POST /api/rag/uploads/delete
```

Upload config lives under `rag.upload`:

```yaml
rag:
  upload:
    enabled: true
    uploads_dir: data/uploads
    max_file_size_kb: 16384
    max_files_per_request: 20
    auto_ingest: true
    async_ingest: false
```

The backend validates extension, MIME type, per-file size, max files per request, and safe filename handling before storing files. Uploaded files are ingested and made searchable through the same HNSW/Xapian hybrid index. Deletion is restricted to files inside `uploads_dir` and can trigger reindexing.

Protect upload/delete routes with `rag:write` or an upstream authorization layer before exposing the app to other users.

## Cache Operations

RAG cache settings live under `rag.cache` in generated apps. The current supported backend is the in-process memory cache:

```yaml
rag:
  cache:
    enabled: true
    backend: memory
    max_size: 1000
    ttl_seconds: 3600
```

Memory cache reduces repeated LLM calls within one running process. It is cleared on restart and is not shared across replicas, which keeps local operations simple.

Redis is the intended external/shared cache target and the config shape is already reserved:

```yaml
rag:
  cache:
    backend: redis
    redis:
      host: 127.0.0.1
      port: 6379
      db: 0
      password: ""
      ttl_seconds: 3600
```

Redis cache is implemented through the built-in RESP TCP client. Use it for multi-instance deployments, centralized TTL/eviction and cache sharing. If Redis is unavailable at startup, the app falls back to memory cache. Keep Redis on a private network, configure authentication for non-local access, and set finite TTLs for LLM cache keys.

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

## qornix_auth Admin UI

Generated RAG apps can use `qornix_auth` as the host application auth layer:

```yaml
auth:
  enabled: true
  admin_permissions: auth:admin
  bootstrap_admin:
    enabled: true
    password_env: QORNIX_ADMIN_PASSWORD
    permissions: rag:read,rag:write,rag:admin,auth:admin
```

When auth is enabled, the RAG Admin tab exposes session login/logout controls
and user management backed by:

```http
GET /auth/users
POST /auth/users
PATCH /auth/users/{id}
```

Only users with `auth:admin` can access user management. RAG permissions remain
separate: grant `rag:read`, `rag:write`, and `rag:admin` according to the routes
the user should operate.

Cookie-authenticated browser write/admin routes use CSRF protection by default:

```yaml
auth:
  csrf:
    enabled: true
    header: X-CSRF-Token
```

The login and current-user endpoints return `csrf_token`; the bundled Admin UI sends it as `X-CSRF-Token` for `POST`, `PUT`, `PATCH`, and `DELETE` requests. Bearer/JWT requests are not blocked by this browser-session CSRF check.

Generated apps also expose first-baseline account hardening endpoints:

```http
POST /auth/invites
POST /auth/invites/accept
POST /auth/password-reset/request
POST /auth/password-reset/confirm
GET  /auth/audit
```

Invite and password-reset delivery is manual in this baseline: the API returns
the one-time token to an admin/operator instead of sending email. Integrate an
SMTP or notification provider before enabling self-service recovery for public
users. Password reset and privilege-sensitive user updates invalidate active
sessions for the affected user.

For production behind HTTPS, prefer:

```yaml
auth:
  secure_cookies: true
  cookie_same_site: Strict
```

Keep `SameSite=Lax` only when cross-site login redirects require it.

## TLS And Reverse Proxy

Terminate TLS in a reverse proxy such as Nginx, Caddy, Traefik, or your platform
load balancer. Forward only the generated app port from an internal network and
set:

```yaml
server:
  address: 0.0.0.0
auth:
  enabled: true
  secure_cookies: true
```

Minimal Nginx shape:

```nginx
server {
    listen 443 ssl http2;
    server_name rag.example.com;

    ssl_certificate /etc/letsencrypt/live/rag.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/rag.example.com/privkey.pem;

    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-Proto https;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

    location / {
        proxy_pass http://127.0.0.1:8008;
    }
}
```

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

The runtime image runs as a non-root user and the Compose template drops Linux
capabilities, enables `no-new-privileges`, mounts writable runtime directories
explicitly, and keeps the container filesystem read-only outside those mounts.

Run the generated smoke check after deploy:

```bash
./deploy_smoke.sh http://127.0.0.1:8008
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
- Prefer `qornix_auth` for generated apps exposed on a network: set `auth.enabled: true`, configure `auth.database` through `qornix_orm`, and use SQLite/PostgreSQL/MySQL according to the deployment.
- Use route permissions deliberately: `rag:read` covers read/search/ask, `rag:write` covers indexing/ingestion/QA writes/source writes, `rag:admin` covers diagnostics/metrics/analytics, and `auth:admin` covers user management.
- Keep `auth.csrf.enabled: true` for cookie-authenticated browser sessions; disable it only when all unsafe routes are protected by non-cookie credentials.
- Protect `/api/rag/admin/diagnostics`, `/api/rag/metrics`, upload/delete, QA write, ingestion, and document delete endpoints with `qornix_auth`, `rag.security`, or an upstream auth layer.
- Store LLM API keys outside Git and container images.
- Review upload extension/MIME/size allowlists and indexing path allowlists before enabling arbitrary user-controlled sources.
- Keep `rag.indexing.max_file_size_kb` conservative for shared deployments.


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
