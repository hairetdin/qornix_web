# Integrating Qornix RAG With qornix_web

Standalone `qornix_rag` and integrated `qornix_web` usage are separate product lines. For the complete feature/dependency/model guide, see [FULL_RAG_GUIDE.md](FULL_RAG_GUIDE.md).

- Standalone mode owns `qornix_rag/config.yaml`, binds locally by default, and can serve the UI at `/`.
- Integrated mode uses the host application's config, should not claim `/`, and defaults to `/rag` plus `/api/rag/*`.

## Targets

Current CMake targets:

```text
qornix_rag_core
qornix_rag_http
qornix_rag_extension
qornix_rag
qornix_rag_route_extension
```

`qornix_web_core` does not link RAG directly. A host application links `qornix_rag_extension` when it wants static RAG integration.

## Static Integration

Configure:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON
cmake --build build --target qornix_web
```

When enabled, host application config is flattened and passed to `RagExtension::configure()`. The extension maps host config into the `RagConfig` runtime contract.

Example host config:

```yaml
rag:
  enabled: true
  route:
    ui_path: /rag
    api_prefix: /api/rag
  llm:
    enabled: true
    api_url: "http://localhost:11434"
    model: "llama3"
  search:
    top_k: 10
  upload:
    enabled: true
    uploads_dir: data/uploads
    max_file_size_kb: 16384
    max_files_per_request: 20
```

Default integrated routes:

```text
GET  /rag
POST /api/rag/ask
POST /api/rag/search
GET  /api/rag/health
GET  /api/rag/admin/diagnostics
POST /api/rag/documents/upload
POST /api/rag/uploads/delete
GET  /api/rag/metrics
GET  /api/rag/sources
GET  /api/rag/qa/list
GET  /api/rag/qa/suggest
GET  /api/rag/qa/categories
```

Protect diagnostics, metrics, QA write, upload, ingestion, and delete routes with `rag.security` or the host application's auth/proxy layer before exposing a generated app outside a trusted network. See generated `doc/operations.md` for the deployment checklist.

## Dynamic Route Extension

Build:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target qornix_rag_route_extension
```

Output:

```text
build/route_extensions/libqornix_rag_route_extension.so
```

`ServerManager` keeps `ExtensionLoader` alive for the server lifetime, and `ExtensionLoader` destroys extension instances before `dlclose`.

## Configuration Boundary

Do not reuse the standalone config file as the host application config.

Instead:

- standalone YAML maps into `RagConfig`;
- host application config maps into `RagConfig`;
- third-party code can construct `RagConfig` programmatically.

This keeps `qornix_rag` reusable without forcing one global config file across products.

## Security Notes

Authentication, authorization, RBAC, multi-user workspaces, and tenant isolation are not part of standalone mode. They belong to generated full web applications or host applications when RAG is exposed beyond localhost.
