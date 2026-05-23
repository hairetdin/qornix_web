# ROADMAP_STABILIZATION_AND_PRODUCT_PLAN changelog

This file tracks execution status for `ROADMAP_STABILIZATION_AND_PRODUCT_PLAN.md`.
It is project documentation and must stay under `qornix_rag/doc/project_doc/`.

## Status legend

- `done` - implemented and verified by the local run checks described below.
- `in progress` - currently being implemented.
- `pending` - planned but not started.
- `deferred` - intentionally moved to a later milestone.

## 2026-05-22 - Milestone 0 completed

Status: `done`

Completed preparation work:

- moved project planning documentation under `qornix_rag/doc/project_doc/`;
- kept user-facing documentation separate from project/internal documentation;
- added `qornix_rag/doc/project_doc/INDEX.md`;
- added `qornix_rag/doc/project_doc/MILESTONE_0_PREP.md`;
- confirmed standalone is a local single-user wiki/RAG tool and does not need auth/RBAC;
- confirmed full `qornix_web/templates` integration is a separate product line after standalone stabilization.

## 2026-05-22 - Milestone A1 completed

Status: `done`

Completed standalone startup/config work:

- normalized standalone default bind address to `127.0.0.1`;
- normalized standalone default port to `8081`, while allowing CLI override such as `--port 8082`;
- introduced canonical `server.address` config key and retained legacy `server.host` compatibility;
- fixed `qornix_rag/run.sh` so it resolves paths relative to the script/repository instead of the caller's current directory;
- fixed `run.sh --help` so it exits before dependency checks, build, and startup;
- fixed standalone binary resolution from the CMake build tree;
- added `QORNIX_RAG_HOME` and `QORNIX_RAG_TEMPLATES_DIR` handling;
- fixed `/` template loading from `qornix_rag/templates/rag_interface.html`;
- made LLM availability reporting honest when libcurl is missing;
- fixed CURL-enabled build issue in `LLMClient` timeout setup;
- confirmed local startup reaches the web UI and registers `/api/ask`, `/api/search`, `/api/health`, `/api/stats`, `/api/sources`, and QA endpoints.

Verification observed from local run:

```text
CURL found - LLM client enabled
Bind address: 127.0.0.1:8082
GET  / - Web interface
POST /api/ask - LLM question
GET  /api/health - health check
```

## 2026-05-22 - Milestone A2/A3 completed

Status: `done`

Scope:

- A2: make LLM Ask the primary standalone web workflow.
- A3: replace the old code-search-only UI with a local RAG/wiki UI.

Implemented:

- replaced the old `Qornix RAG - search by code` page with a standalone local wiki/RAG UI;
- made the main tab `Ask` and wired it to `POST /api/ask`;
- displayed the generated answer, LLM status, response time, and source/context snippets;
- kept a dedicated `Search` tab wired to `POST /api/search`;
- added a `QA` tab for adding QA pairs and listing existing local QA entries;
- added `Sources / Health` tab for indexed project stats, source list, LLM status, and reindex action;
- added UI-level handling for LLM-unavailable/search-only fallback responses;
- kept `HandlerBase` method dispatch intact and used the existing handler model;
- added GET handling for the already advertised `/api/sources` and `/api/qa/list` routes;
- extended QA endpoints to work with the default SQLite-backed knowledge base in standalone mode.

Acceptance checks:

```bash
./qornix_rag/run.sh --port 8082
curl http://localhost:8082/api/health
curl http://localhost:8082/api/sources
curl http://localhost:8082/api/qa/list
```

Manual browser checks:

- `http://localhost:8082/` opens the new RAG/wiki UI;
- Ask tab returns an answer from `/api/ask`;
- Search tab returns ranked snippets from `/api/search`;
- QA tab can add a pair and reload the QA list;
- Sources / Health tab shows LLM, RAG, source, and index status.

## 2026-05-22 - A2/A3 LLM fallback status fix

Status: `done`

Reason:

- after A2/A3 the UI could display `LLM: ok` while the answer body was the graceful-degradation text `LLM недоступен...`;
- this happened because `/api/ask` used `LLMClient::is_available()` as a transport/health check after generation, while `LLMClient::ask()` could still fall back when the configured model/request failed.

Implemented:

- Ollama health checks now use `GET /api/tags` and validate that the configured model is present;
- Ollama chat requests now send `stream: false` and Ollama-compatible `options` instead of relying only on OpenAI-style fields;
- `/api/ask` now reports `llm_status: fallback` when the returned answer is a fallback/error answer, so the UI no longer shows `LLM: ok` for fallback output.

## 2026-05-22 - A2/A3 user-friendly LLM diagnostics

Status: `done`

Reason:

- `/api/health` is useful for developers and UI code, but raw JSON is not sufficient for non-programmer standalone users;
- users should see the LLM state immediately on the main page, not only in logs, terminal output, browser devtools, or the Sources / Health tab.

Implemented:

- added a global LLM diagnostic banner directly below the hero block;
- added an Ask-tab warning block before the question field;
- kept the technical Sources / Health details and added a human-readable LLM status block above them;
- explained the configured model, available models, and the concrete `qornix_rag/config.yaml` change when the configured model is missing;
- showed Ollama-specific install guidance such as `ollama pull <model>` when appropriate;
- showed clear guidance when the LLM provider is unavailable or reports no models.

## 2026-05-22 - Milestone A4 completed

Status: `done`

Scope:

- complete the local QA/wiki workflow;
- make QA entries usable in Ask/Search;
- make QA management usable when the list grows beyond a few entries.

Implemented:

- QA pairs can be added from the UI;
- QA pairs can be listed;
- QA pairs can be edited;
- QA pairs can be deleted;
- QA pairs are searched and included in Ask context;
- QA pairs appear as QA/source entries in Search and Ask sources;
- fixed the `escapeJs is not defined` UI error;
- fixed handler wiring so Ask/Search and QA endpoints use the same SQLite-backed QA source in standalone mode;
- added QA table view;
- added local QA filtering/search by question, answer, category, and id;
- added category filtering;
- added client-side QA pagination.

Verified manually:

- adding a QA pair immediately affected Ask answers;
- QA entries displayed in the table;
- QA entries could be edited and deleted from the UI.

Deferred from A4:

- server-side QA pagination/filtering;
- QA autocomplete;
- large-scale QA list APIs through `qornix_web` dynamic API and `qornix_orm`.

## 2026-05-22 - Roadmap numbering cleanup

Status: `done`

Reason:

- the roadmap mixed two numbering schemes;
- portable bundle appeared as `Milestone A2`, while implementation discussion used `A5`;
- important items such as configuration consistency, route ownership, dynamic extension lifetime, and build separation were at risk of being skipped.

Updated unified scheme:

```text
Milestone 0
Milestone A
  A1 startup/config baseline
  A2 LLM Ask workflow
  A3 RAG/wiki web UI
  A4 QA/wiki workflow
  A5 configuration, routing, and integration boundary hardening
  A6 build separation and reusable-target preparation
  A7 portable standalone release bundle
  A8 user documentation refresh
Milestone B reusable RAG core
Milestone C qornix_web/templates/rag_app
Milestone D --with-rag for existing templates
Milestone E production RAG expansion
```

## 2026-05-22 - Milestone A5 completed

Status: `done`

Scope:

- clarify the RAG runtime configuration boundary without forcing one config file across standalone, `qornix_web`, and third-party use;
- make standalone and integrated route ownership explicit;
- fix dynamic extension loader lifetime;
- improve startup diagnostics for mode, config source, LLM, bind address, and route prefix.

Implemented:

- added a `RagConfig` runtime contract with mode-specific adapters for standalone flat YAML config and integrated host application config;
- standalone mode maps `qornix_rag/config.yaml` into `RagConfig` and constructs `LLMClient` from the parsed LLM settings;
- integrated mode maps host config into `RagConfig` and defaults to `/rag` plus `/api/rag/*` instead of claiming `/`;
- `RagExtension` now initializes runtime services from `RagConfig` instead of separately parsing `rag.*` keys and hidden defaults;
- RAG route registration now accepts explicit route options and prints the registered UI/API paths;
- RAG API handlers now support both standalone `/api/*` and prefixed integrated routes such as `/api/rag/*`;
- standalone UI keeps `/api/*`, while prefixed UI can call its configured API prefix through injected `API_BASE`;
- `ServerManager` keeps `ExtensionLoader` for the server lifetime so dynamically loaded route extensions are not unloaded immediately after setup;
- `ExtensionLoader` now destroys extension instances through `destroyExtension` before calling `dlclose`;
- `ServerManager` exposes flattened host config and DI container access needed for integrated RAG setup.

Verified:

```bash
cmake --build build --target qornix_rag -j2
cmake --build build --target qornix_web -j2
./build/qornix_rag/qornix_rag --help
timeout 8s ./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8091 --project qornix_rag/doc/project_doc
```

Observed standalone startup diagnostics:

```text
RAG mode: standalone
Config source: qornix_rag/config.yaml
Bind address: 127.0.0.1:8091
GET  / - Web interface
POST /api/ask - LLM question
GET  /api/health - health check
```

## 2026-05-22 - Milestone A6 completed

Status: `done`

Scope:

- clarify CMake target ownership before reusable-core work;
- separate reusable RAG services from qornix_web HTTP routing and extension glue;
- keep standalone and integrated builds working after the split.

Implemented:

- split the old monolithic `qornix_rag_lib` target into:
  - `qornix_rag_core` for reusable RAG services, LLM client, config contract, data sources, analytics, deduplication, cache, rate limit, batch, prompt cache, and metrics;
  - `qornix_rag_http` for `RagApiHandler`, `RagWebHandler`, and `setupRagRoutes`;
  - `qornix_rag_extension` for the qornix_web extension adapter;
  - `qornix_rag` for the standalone executable;
- kept `qornix_rag_lib` as an interface compatibility target for older consumers;
- removed direct compilation of `../server/http_server.cpp` from the RAG library;
- changed root qornix_web integration so `qornix_web_core` no longer links RAG; the optional demo app links `qornix_rag_extension` instead;
- added PIC target properties needed for dynamic route extension builds;
- kept the legacy `QORNIX_BUILD_RAG_EXTENSION` CMake option while adding the clearer `QORNIX_BUILD_RAG_ROUTE_EXTENSION`;
- updated RAG tests to link `qornix_rag_core` when available instead of manually compiling partial implementation files.

Verified:

```bash
cmake --build build --target qornix_rag qornix_web -j2
cmake --build build --target test_llm_client test_rag_api test_health_check test_data_sources test_sqlite_source -j2
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target qornix_rag_route_extension -j2
ctest --test-dir build -R "test_(llm_client|health_check|rag_api|data_sources|sqlite_source)" --output-on-failure
./build/qornix_rag/qornix_rag --help
```

## 2026-05-22 - Milestone A7 completed

Status: `done`

Scope:

- add a one-command portable standalone bundle builder;
- produce a source-tree-independent runtime folder;
- keep bundled paths relative to the bundle root;
- avoid bundling user data, indexes, secrets, or build directories.

Implemented:

- added `qornix_rag/build_portable.sh`;
- the script configures a Release CMake build for standalone RAG with demo app, tests, ORM, dynamic API, and auth disabled;
- the script builds the `qornix_rag` standalone executable;
- the script creates `dist/qornix_rag-portable-linux-x86_64/`;
- the bundle contains:
  - `bin/qornix_rag`;
  - `templates/rag_interface.html`;
  - `config/config.example.yaml`;
  - `config/config.yaml`;
  - `models/semantic_model.onnx`;
  - `models/tokenizer.json`;
  - empty `data/`, `logs/`, `cache/`, and `knowledge_base/` runtime directories;
  - `lib/ldd.txt` runtime dependency manifest;
  - portable `run.sh`;
  - bundle `README.md`;
- portable config rewrites source-tree paths to bundle-relative paths such as `models/...`, `data/rag_kb.db`, and `knowledge_base`;
- portable config disables startup auto-indexing by default so the bundle can start without indexing itself;
- `--smoke` checks `./run.sh --help` and, when `curl` is available, starts the service and verifies `/api/health`;
- `--archive` creates `dist/qornix_rag-portable-linux-x86_64.tar.gz`;
- runtime files created by smoke tests are removed before final archive creation.

Verified:

```bash
./qornix_rag/build_portable.sh --smoke --archive
test ! -e dist/qornix_rag-portable-linux-x86_64/data/rag_kb.db
test ! -d dist/qornix_rag-portable-linux-x86_64/xapian_index
tar -tzf dist/qornix_rag-portable-linux-x86_64.tar.gz | rg '(^|/)rag_kb\\.db$|(^|/)xapian_index/' || true
```

Output bundle:

```text
dist/qornix_rag-portable-linux-x86_64/
dist/qornix_rag-portable-linux-x86_64.tar.gz
```

## 2026-05-22 - Milestone A8 completed

Status: `done`

Scope:

- refresh user-facing standalone documentation after A1-A7;
- document current RAG/wiki UI instead of the old code-search-first UI;
- document standalone config, API, portable bundle, and qornix_web integration boundaries.

Implemented:

- replaced `qornix_rag/README.md` with a concise current standalone guide;
- added `qornix_rag/doc/STANDALONE.md`;
- added `qornix_rag/doc/CONFIG.md`;
- added `qornix_rag/doc/API.md`;
- added `qornix_rag/doc/PORTABLE.md`;
- added `qornix_rag/doc/INTEGRATION_QORNIX_WEB.md`;
- added `qornix_rag/doc/MIGRATION_FROM_CODE_SEARCH_UI.md`;
- documented source run, port selection, local bind behavior, Ollama setup, missing-model diagnostics, project indexing, Ask/Search, QA management, portable bundle build/run, build targets, and deferred non-goals;
- documented that `POST /api/index` replaces old `/api/reindex` examples;
- documented standalone vs integrated config ownership and the `/rag` plus `/api/rag/*` integrated route boundary.

Verified:

```bash
rg -n "api/reindex|8082|server.host|0\\.0\\.0\\.0.*default|qornix_rag_lib|old code-search|search by code" \
  qornix_rag/README.md \
  qornix_rag/doc/STANDALONE.md \
  qornix_rag/doc/CONFIG.md \
  qornix_rag/doc/API.md \
  qornix_rag/doc/INTEGRATION_QORNIX_WEB.md \
  qornix_rag/doc/MIGRATION_FROM_CODE_SEARCH_UI.md
```

Remaining matches are intentional examples or migration warnings.

## 2026-05-22 - Milestone B completed

Status: `done`

Scope:

- make `qornix_rag` usable as a reusable module, not only as a standalone server;
- introduce a service boundary above `RagEngine` and below HTTP route handlers;
- keep standalone and integrated hosts as adapters over the same core runtime.

Implemented:

- added `qornix_rag/rag_service.h`;
- added `qornix_rag/rag_service.cpp`;
- included `RagService` in the `qornix_rag_core` target;
- added reusable DTOs for index, search, ask, health, source, and QA responses;
- added `RagService` methods for:
  - project indexing;
  - semantic search;
  - non-streaming Ask;
  - health reporting;
  - source listing;
  - QA pair listing, creation, update, and deletion;
- updated HTTP index/search/ask handlers to use the service layer for non-streaming flows;
- added route registration overload accepting a shared `RagService`;
- kept the standalone server and dynamic route extension as host-specific adapters;
- added `qornix_rag/tests/test_rag_service.cpp`;
- registered `test_rag_service` in `qornix_rag/tests/CMakeLists.txt`.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON
cmake --build build --target test_rag_service -j2
ctest --test-dir build -R "test_(rag_service|llm_client|health_check|rag_api|data_sources|sqlite_source)" --output-on-failure
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension -j2
```

## 2026-05-22 - Milestone C completed

Status: `done`

Scope:

- add a dedicated `qornix_web/templates/rag_app` template;
- make `create_new_project.sh <name> --template rag_app` generate a buildable RAG web application;
- host RAG under integrated routes instead of the standalone root route;
- document generated app startup, config, and local LLM setup.

Implemented:

- added `templates/rag_app/CMakeLists.txt.in`;
- added `templates/rag_app/main.cpp`;
- added `templates/rag_app/config.yaml`;
- added `templates/rag_app/README.md.in`;
- added `templates/rag_app/doc/rag_app.md`;
- added `templates/rag_app/runtime-Dockerfile`;
- added `templates/rag_app/docker-compose.yml`;
- added RAG UI template, static assets, `knowledge_base/`, `data/`, and `logs/` runtime layout;
- updated `create_new_project.sh` to accept `--template rag_app`, `--template rag-app`, and `--template rag`;
- generated RAG apps link `qornix::web_core` and `qornix::rag_extension`;
- generated RAG apps disable the standalone `qornix_rag` executable target and use the reusable RAG extension target;
- generated RAG apps configure RAG from their own `config.yaml`;
- generated RAG apps expose `GET /rag` and `/api/rag/*` routes;
- generated RAG apps use the Ollama base URL `http://localhost:11434`, allowing the RAG LLM client to select the correct Ollama chat endpoint;
- generated RAG apps resolve SQLite, Markdown, template, `data/`, and `logs/` paths from the generated application root;
- added REST QA aliases for generated API compatibility:
  - `GET /api/rag/qa`;
  - `POST /api/rag/qa`;
  - `PUT /api/rag/qa/{id}`;
  - `DELETE /api/rag/qa/{id}`;
- kept existing QA routes used by the current UI:
  - `GET /api/rag/qa/list`;
  - `POST /api/rag/qa/add`;
  - `POST /api/rag/qa/update`;
  - `POST /api/rag/qa/delete`.

Verified:

```bash
rm -rf /tmp/qornix_rag_app_c
./create_new_project.sh /tmp/qornix_rag_app_c --template rag_app
sed -i 's/model: llama3.2:3b/model: llama3:latest/' /tmp/qornix_rag_app_c/config.yaml
cmake -S /tmp/qornix_rag_app_c -B /tmp/qornix_rag_app_c/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_rag_app_c/build -j2
./qornix_rag_app_c
curl http://127.0.0.1:8008/api/rag/health
curl -o /tmp/qornix_rag_app_c_rag.html http://127.0.0.1:8008/rag
curl -X POST http://127.0.0.1:8008/api/rag/index -H 'Content-Type: application/json' -d '{}'
curl -X POST http://127.0.0.1:8008/api/rag/ask -H 'Content-Type: application/json' -d '{"question":"Say OK in one short sentence.","top_k":1}'
```

Observed smoke result:

```text
GET /api/rag/health -> {"status":"ok",...}
GET /rag -> 200
POST /api/rag/ask -> {"success":true,...,"answer":"OK!","llm_status":"ok",...}
RAG module configured (mode=integrated, source=application config.yaml, ui=/rag, api_prefix=/api/rag)
SQLiteSource initialized: /tmp/qornix_rag_app_c/data/rag_kb.db
MarkdownSource initialized: /tmp/qornix_rag_app_c/knowledge_base
```

## 2026-05-22 - Milestone D completed

Status: `done`

Scope:

- add `--with-rag` as an optional feature for an existing Qornix Web template;
- preserve the host application's existing routes and home page;
- mount embedded RAG under `/rag` and `/api/rag/*`;
- copy the required RAG runtime assets into generated applications and deploy bundles.

Implemented:

- added `--with-rag` to `create_new_project.sh`;
- enabled `--with-rag` for the default `templates/app` template;
- generated apps with RAG set `QORNIX_BUILD_RAG=ON`, keep the standalone RAG executable disabled, and link `qornix::rag_extension`;
- generated apps with RAG define a compile-time RAG flag and initialize `RagExtension` after the normal host route setup;
- generated apps with RAG expose a generated CMake option such as `-D<PROJECT>_ENABLE_RAG=OFF` to disable embedded RAG routes at build time;
- generated apps with RAG resolve RAG SQLite, Markdown, templates, `data/`, and `logs` paths from the generated application root;
- generated apps with RAG add a `rag:` config section using `/rag` and `/api/rag`;
- generated apps with RAG copy `templates/rag_interface.html`, `static/rag_app.css`, `doc/rag_app.md`, `knowledge_base/`, `models/`, and `download_onnx_model.sh`;
- generated deploy bundles include the RAG assets and empty runtime `data/` directory;
- generator output now prints RAG URLs when `--with-rag` is used;
- plain default app generation without `--with-rag` remains supported and buildable.

Verified:

```bash
bash -n create_new_project.sh
rm -rf /tmp/qornix_app_plain_d /tmp/qornix_app_with_rag_d
./create_new_project.sh /tmp/qornix_app_plain_d
./create_new_project.sh /tmp/qornix_app_with_rag_d --with-rag
cmake -S /tmp/qornix_app_plain_d -B /tmp/qornix_app_plain_d/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_plain_d/build -j2
cmake -S /tmp/qornix_app_with_rag_d -B /tmp/qornix_app_with_rag_d/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_with_rag_d/build -j2
cmake -S /tmp/qornix_app_with_rag_d -B /tmp/qornix_app_with_rag_off_d/build -DCMAKE_BUILD_TYPE=Release -DQORNIX_APP_WITH_RAG_D_ENABLE_RAG=OFF
cmake --build /tmp/qornix_app_with_rag_off_d/build -j2
```

Smoke checked generated `--with-rag` app:

```text
GET / -> 200
GET /docs -> 200
GET /rag -> 200
GET /api/rag/health -> {"status":"ok",...,"embedding_backend":"tfidf",...}
POST /api/rag/index -> {"success":true,...}
POST /api/rag/search -> {"success":true,...,"count":3}
POST /api/rag/ask -> {"success":true,...,"llm_status":"fallback",...}
RAG module configured (mode=integrated, source=application config.yaml, ui=/rag, api_prefix=/api/rag)
```

Smoke checked generated `--with-rag` app built with RAG disabled:

```text
GET / -> 200
GET /rag -> 404
GET /api/rag/health -> 404
```

## 2026-05-22 - Milestone E1 persistence baseline completed

Status: `done`

Scope:

- start Milestone E with persistent indexed document/chunk/vector storage;
- keep the existing SQLite QA source behavior intact;
- add a storage boundary that can later be replaced by Faiss, Qdrant, pgvector, or another backend;
- make normal project indexing persist an index snapshot when SQLite is configured.

Implemented:

- added `qornix_rag/persistent_index_store.h`;
- added `PersistentIndexStore` with methods for persisting indexed documents and counting/looking up persisted documents, chunks, and embeddings;
- made `SQLiteSource` implement `PersistentIndexStore` in addition to the existing `DataSource` API;
- added SQLite migration tables:
  - `rag_documents`;
  - `rag_chunks`;
  - `rag_embeddings`;
  - `rag_embedding_models`;
- persisted indexed document metadata including path, relative path, type, language, hash, size, line count, metadata JSON, and last modified time;
- persisted one baseline chunk per document, with content hash and chunk metadata;
- persisted embedding vectors as binary float blobs with model/backend/dimension metadata;
- made persistence idempotent per source by replacing the previous persisted snapshot for the source on reindex;
- added `RagEngine::get_documents_snapshot()` for service-layer persistence without exposing mutable engine internals;
- updated `RagService::indexProject()` so a successful project index writes the persistent snapshot through the SQLite-backed store when available;
- updated persisted snapshots to upsert current documents and delete stale documents for the same source, cascading stale chunks and embeddings;
- added SQLiteSource tests for persisted index snapshots, counts, metadata lookup, and idempotent re-persistence;
- added RagService test coverage that verifies `indexProject()` persists documents, chunks, and embeddings.

Verified:

```bash
cmake --build build --target test_sqlite_source test_rag_service -j2
ctest --test-dir build -R "test_(sqlite_source|rag_service)" --output-on-failure
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension -j2
```

Observed result:

```text
100% tests passed, 0 tests failed out of 2
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## 2026-05-22 - Milestone E2 ingestion baseline completed

Status: `done`

Scope:

- add a reusable document ingestion pipeline for currently supported text-like files;
- make project indexing use the ingestion pipeline instead of ad hoc filesystem scanning;
- keep arbitrary binary/PDF/OCR-style ingestion deferred until parser plugins exist.

Implemented:

- added `qornix_rag/ingestion_pipeline.h`;
- added `qornix_rag/ingestion_pipeline.cpp`;
- added `IngestionPipeline` with a filesystem job config, recursive/non-recursive scanning, directory exclusions, file size checks, and structured job results;
- added extension-based MIME/type/language detection for supported source, Markdown, text, and config files;
- added `DocumentParser` as the parser plugin interface;
- added parser registry in `IngestionPipeline`;
- added `PlainTextParser` for currently supported text-like files;
- added `HtmlParser` for `.html` and `.htm` files, including tag stripping, script/style removal, title extraction, entity decoding, and parser metadata;
- added `IngestedDocument` records with content, hash, line count, last-modified time, MIME type, parser metadata, and relative path;
- added structured ingestion issues with severity, code, path, and message;
- added binary-content detection and skip reporting;
- added duplicate detection by content hash within an ingestion job;
- moved `RagEngine::index_project()` onto `IngestionPipeline`, so `/api/rag/index` uses the new ingestion path;
- moved `FileSource` onto `IngestionPipeline`, so reusable filesystem sources and direct project indexing share the same detection/parser logic;
- added `test_ingestion_pipeline` coverage for type detection, binary skip, excluded directories, metadata, hashes, HTML extraction, and imported document counts.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON
cmake --build build --target test_ingestion_pipeline test_data_sources test_rag_service test_sqlite_source qornix_rag -j2
ctest --test-dir build -R "test_(ingestion_pipeline|data_sources|rag_service|sqlite_source)" --output-on-failure
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension -j2
```

Observed result:

```text
100% tests passed, 0 tests failed out of 4
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## 2026-05-22 - Milestone E2 durable ingestion jobs and delete flow

Scope:

- close the remaining E2 baseline around durable ingestion jobs/status APIs and persisted document deletion;
- keep heavyweight format adapters and asynchronous/background ingestion explicitly deferred.

Implemented:

- added `rag_ingestion_jobs` to the SQLite migration;
- added `SQLiteSource::recordIngestionJobStarted()`, `recordIngestionJobFinished()`, `findIngestionJob()`, and `listIngestionJobs()`;
- added `SQLiteSource::deletePersistedDocument()` for persisted index document removal with chunk/embedding cascade;
- added `RagEngine::get_last_ingestion_result()` so service-level ingestion can persist job counters;
- added `RagService::ingestProject()`, `findIngestionJob()`, `listIngestionJobs()`, and `deletePersistedDocument()`;
- added `POST /api/rag/ingest` for synchronous job-oriented ingestion;
- added `GET /api/rag/ingest/jobs` and `GET /api/rag/ingest/{id}` for ingestion history/status;
- added `POST /api/rag/documents/delete` for deleting a persisted document by `relative_path` and optional `source_id`;
- updated SQLite and service tests for durable job history and persisted document deletion;
- moved advanced parser adapters, async/background ingestion, and full incremental in-memory reindexing into the backlog.

Verified:

```bash
cmake --build build --target test_sqlite_source test_rag_service test_ingestion_pipeline qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R "test_(ingestion_pipeline|data_sources|rag_service|sqlite_source)" --output-on-failure
git diff --check
```

Observed result:

```text
100% tests passed, 0 tests failed out of 4
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## 2026-05-23 - Milestone E3 chunking strategies completed

Status: `done`

Scope:

- replace the E1 baseline of one whole-document chunk with chunk-level retrieval and persistence;
- add reusable chunking strategies for currently supported text, Markdown, and code documents;
- preserve source document metadata and chunk offsets for future citations and vector backend work.

Implemented:

- added `qornix_rag/document_chunker.h`;
- added `qornix_rag/document_chunker.cpp`;
- added `DocumentChunker` with configurable `max_tokens`, `overlap_tokens`, and `min_chunk_tokens`;
- added plain text token-window chunking with overlap;
- added Markdown heading-aware section splitting before token-window chunking;
- added code symbol/function-aware section splitting before token-window chunking;
- added chunk metadata:
  - `chunk_of`;
  - `chunk_index`;
  - `chunk_count`;
  - `chunk_strategy`;
  - `chunk_char_start`;
  - `chunk_char_end`;
  - `chunk_token_count`;
  - optional `chunk_heading`;
  - optional `chunk_symbol`;
- updated `RagEngine::index_project()` and `RagEngine::indexSources()` so chunk-level records receive embeddings and are indexed into HNSW/Xapian;
- updated context building so the source file and chunk index are visible in generated context;
- updated SQLite persistence so chunk-level records are grouped under one source document in `rag_documents` while all chunks and embeddings are stored in `rag_chunks` and `rag_embeddings`;
- made snapshot replacement remove stale chunks/embeddings for the source before writing the new chunk set;
- added `qornix_rag/tests/test_document_chunker.cpp`;
- added SQLite regression coverage for persisting multiple chunks and embeddings under one source document.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension test_document_chunker test_sqlite_source test_rag_service test_ingestion_pipeline -j2
ctest --test-dir build -R "test_(document_chunker|ingestion_pipeline|sqlite_source|rag_service)$" --output-on-failure
git diff --check
```

Observed result:

```text
100% tests passed, 0 tests failed out of 4
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## 2026-05-23 - Milestone E4 ONNX embedding expansion completed

Status: `done`

Scope:

- add model metadata and stable model identity for embedding backends;
- make persisted embedding namespaces change when embedding model/version/config changes;
- improve ONNX output handling without requiring ONNX Runtime in every development environment.

Implemented:

- extended `EmbeddingConfig` with `model_id`, `model_name`, `model_version`, `tokenizer_type`, `pooling`, `dimension`, and `lowercase_tokens`;
- added `EmbeddingModelInfo` runtime metadata;
- added `RagEngine::get_embedding_model_id()`, `get_embedding_model_info()`, and `get_embedding_cache_namespace()`;
- added deterministic model id generation for TF-IDF and ONNX backends;
- changed `RagService::indexProject()` so SQLite persisted embeddings use the effective embedding model id;
- made ONNX fallback report and persist the effective TF-IDF model id/dimension when fallback is active;
- added `dimension: 0` autodiscovery and positive-dimension validation for ONNX outputs;
- added `mean` and `cls` pooling modes for 3D ONNX outputs;
- parsed tokenizer type from Hugging Face-style `tokenizer.json` when available;
- added configurable tokenizer lowercasing;
- exposed embedding model id and dimension through health/stats responses and startup diagnostics;
- added expanded ONNX embedding config examples to standalone and generated RAG app configs;
- documented model id/version/dimension behavior in `qornix_rag/doc/CONFIG.md`;
- added `qornix_rag/tests/test_embedding_config.cpp`.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON
cmake --build build --target test_embedding_config test_rag_service test_sqlite_source qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R "test_(embedding_config|rag_service|sqlite_source)$" --output-on-failure
```

Observed result:

```text
100% tests passed, 0 tests failed out of 3
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## 2026-05-23 - Milestone E5 RAG quality baseline completed

Status: `done`

Scope:

- add first-pass citation and confidence metadata to Ask/Search responses;
- make chunked retrieval source paths readable in API/UI;
- expose grounding status without attempting full answer verification yet.

Implemented:

- added `citation_id`, `source_path`, and normalized `confidence` fields to service search/ask context items;
- added `retrieval_confidence`, `grounding_status`, and `citations` to `RagServiceAskResponse`;
- added prompt context citation markers such as `[S1]` and `[Q1]`;
- added prompt guidance asking the LLM to cite bracketed source ids and admit insufficient context;
- exposed citation, confidence, source path, retrieval confidence, and grounding status fields through `/api/ask`;
- exposed source path, citation id, and confidence through `/api/search`;
- updated standalone UI Ask status to show grounding status and retrieval confidence;
- updated Ask/Search source rendering to show citation ids and confidence;
- added service-level regression coverage for Ask citations, source path, retrieval confidence, and grounding status.

Verified:

```bash
cmake --build build --target test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R "test_rag_service$" --output-on-failure
```

Observed result:

```text
100% tests passed, 0 tests failed out of 1
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## 2026-05-23 - Milestone E6 operations and deployment baseline completed

Status: `done`

Scope:

- add operational diagnostics and metrics endpoints for generated/full web applications;
- document deployment volumes, logging, backup/restore, rate limits, and security ownership;
- keep auth/RBAC as host-application responsibility instead of adding standalone auth.

Implemented:

- added `GET /api/rag/admin/diagnostics` through the configured RAG API prefix;
- made `GET /api/rag/metrics` return Prometheus text metrics through the GET handler path;
- diagnostics include:
  - RAG index state;
  - project root;
  - embedding backend/model id/dimension;
  - LLM provider/model/status;
  - cache stats;
  - prompt-cache stats;
  - rate-limit counters;
  - SQLite QA/persisted document/chunk/embedding counts;
  - metrics availability;
  - network exposure warning;
- updated `templates/rag_app/docker-compose.yml` to persist `logs/` in addition to `data/` and `knowledge_base/`;
- added `templates/rag_app/doc/operations.md`;
- updated generated RAG app README and `doc/rag_app.md` to reference operations docs;
- updated `create_new_project.sh` so default apps generated with `--with-rag` also receive `doc/operations.md`;
- documented admin diagnostics and metrics endpoints in `qornix_rag/doc/API.md` and `qornix_rag/doc/INTEGRATION_QORNIX_WEB.md`.

Verified:

```bash
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension test_rag_service -j2
./build/qornix_rag/qornix_rag --port 8097 --project qornix_rag/doc/project_doc
curl -fsS http://127.0.0.1:8097/api/admin/diagnostics
curl -fsS http://127.0.0.1:8097/api/metrics
ctest --test-dir build -R "test_(rag_service|document_chunker|embedding_config|sqlite_source)$" --output-on-failure
git diff --check
```

Observed result:

```text
GET /api/admin/diagnostics returned metrics_enabled, rate_limit, and storage diagnostics.
GET /api/metrics returned qornix_rag_* Prometheus metrics.
100% tests passed, 0 tests failed out of 4
Built target qornix_rag
Built target qornix_web
Built target qornix_rag_route_extension
```

## Current milestone state

- Milestone 0: `done`
- Milestone A1 - startup/config baseline: `done`
- Milestone A2 - LLM Ask workflow: `done`
- Milestone A3 - RAG/wiki web UI: `done`
- Milestone A4 - QA/wiki workflow: `done`
- Milestone A5 - configuration, routing, and integration boundary hardening: `done`
- Milestone A6 - build separation and reusable-target preparation: `done`
- Milestone A7 - portable standalone release bundle: `done`
- Milestone A8 - user documentation refresh: `done`
- Milestone B - reusable RAG core: `done`
- Milestone C - `qornix_web/templates/rag_app`: `done`
- Milestone D - `--with-rag` for existing templates: `done`
- Milestone E - production RAG expansion: `done`
  - E1 persistent knowledge and vector storage baseline: `done`
  - E2 document ingestion pipeline baseline: `done`
  - E3 chunking strategies: `done`
  - E4 ONNX embedding expansion: `done`
  - E5 RAG quality improvements: `done`
  - E6 operations and deployment: `done`

## Deferred / known follow-ups

These items are intentionally not closed by A2/A3/A4 or the E1 persistence baseline:

- serving retrieval directly from persisted vectors, including HNSW save/load or a replaceable vector backend;
- local SQLite metadata plus persisted HNSW index files;
- optional Faiss backend adapter;
- optional Qdrant backend adapter;
- optional pgvector backend adapter;
- vector backend selection/configuration docs and tests;
- arbitrary document ingestion beyond current text/Markdown/QA flows;
- PDF/DOCX/XLSX/images/OCR support;
- auth/RBAC, which is not needed for standalone and belongs only to networked application templates if required later;
- retrieval relevance and query normalization;
- server-side QA pagination/filtering/autocomplete;
- model selection UI and additional LLM diagnostics polish.
