# ROADMAP_STABILIZATION_AND_PRODUCT_PLAN changelog

This file tracks execution status for `ROADMAP_STABILIZATION_AND_PRODUCT_PLAN.md`.
It is project documentation and must stay under `qornix_rag/doc/project_doc/`.

## Status legend

- `done` - implemented and verified by the local run checks described below.
- `in progress` - currently being implemented.
- `pending` - planned but not started.
- `deferred` - intentionally moved to a later milestone.

## 2026-05-27 - Generated rag_app root route fix

Status: `done`

Reason:

- dedicated generated `rag_app` projects registered the RAG UI at `/rag`, but the shared server startup banner printed `http://localhost:8008`;
- opening that root URL returned 404 even though `/rag` and `/api/rag/health` were healthy.

Implemented:

- added integrated config support for `rag.route.expose_root_ui`;
- enabled `rag.route.expose_root_ui: true` only in the dedicated `templates/rag_app/config.yaml`;
- when root exposure is enabled and the configured UI path is not `/`, RAG route setup now registers `/` as a UI alias in addition to `/rag`;
- updated generator output and generated RAG app docs to list `http://127.0.0.1:8008/`.

Validation:

```text
generated rag_app:
  GET / -> 200
  GET /rag -> 200
  GET /api/rag/health -> 200
```

## 2026-05-27 - Vector backend production hardening completed

Status: `done`

Scope:

- complete `0.1` item 3, Vector backend production hardening.

Implemented:

- added `VectorStoreDiagnostics` and `VectorStore::diagnostics()`;
- implemented diagnostic statuses for local HNSW, Faiss, Qdrant, and pgvector;
- exposed vector diagnostics through RAG service health, API health, stats, and admin diagnostics JSON;
- added `PersistentIndexStore::listPersistedEmbeddings()` and SQLite-backed persisted embedding export;
- added `vector_store.upsert_batch_size` config parsing and examples;
- added Qdrant batch upsert pagination and synchronized full-target rebuild behavior;
- wrapped pgvector rebuilds in a transaction while retaining truncate-based stale delete synchronization;
- added opt-in live vector backend tests for Qdrant and PostgreSQL+pgvector through `QORNIX_TEST_QDRANT_URL` and `QORNIX_TEST_PGVECTOR_DSN`;
- added a dedicated Faiss roundtrip test that is registered when Faiss is available at configure time;
- added `qornix_rag_vector_migrate` for migrating persisted SQLite embeddings to local HNSW/Faiss, Qdrant, or pgvector;
- documented the new vector diagnostic fields.

Validation:

```text
cmake --build build --target test_vector_store test_vector_store_live test_faiss_vector_store test_embedding_config test_sqlite_source qornix_rag_vector_migrate qornix_rag -j2
ctest --test-dir build -R 'test_(vector_store|vector_store_live|faiss_vector_store|embedding_config|sqlite_source)$' --output-on-failure
```

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

## 2026-05-23 - Roadmap/changelog stabilization sync

Status: `done`

Reason:

- the roadmap top-level current status and changelog showed Milestone E as complete, while section 8 still said `in progress`;
- the suggested implementation order still listed E3-era work as the next step even though E3, E4, E5, and E6 are complete.

Updated:

- synchronized section 8 to `done` for the E1-E6 production RAG baseline;
- added a short E1-E6 completed-baseline summary to the roadmap;
- changed the implementation order section from "through Milestone D" to "through Milestone E";
- replaced the stale recommended next order with a post-stabilization plan that starts with a local vector backend and HNSW save/load;
- marked the current phase definition of done as complete for the stabilization and E1-E6 baseline.

Verified:

```bash
rg -n <stale-roadmap-status-and-order-patterns> \
  qornix_rag/doc/project_doc/ROADMAP_STABILIZATION_AND_PRODUCT_PLAN.md \
  qornix_rag/doc/project_doc/ROADMAP_STABILIZATION_AND_PRODUCT_PLAN_changelog.md
git diff --check
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension test_rag_service test_document_chunker test_embedding_config test_sqlite_source test_ingestion_pipeline -j2
ctest --test-dir build -R 'test_(rag_service|document_chunker|embedding_config|sqlite_source|ingestion_pipeline)$' --output-on-failure
./build/qornix_rag/qornix_rag --port 8097 --project qornix_rag/doc/project_doc
curl -fsS http://127.0.0.1:8097/api/health
curl -fsS http://127.0.0.1:8097/api/admin/diagnostics
curl -fsS http://127.0.0.1:8097/api/metrics
```

Observed result:

```text
No stale roadmap status/order matches were found.
git diff --check passed.
100% tests passed, 0 tests failed out of 5.
Built target qornix_rag, qornix_web, qornix_rag_route_extension, and focused RAG test binaries.
Standalone smoke returned health, admin diagnostics, and Prometheus metrics.
```

## 2026-05-23 - Post-stabilization vector store baseline completed

Status: `done`

Scope:

- start the `Recommended post-stabilization order`;
- add a retrieval-time vector store boundary separate from SQLite metadata persistence;
- make the local HNSW index saveable/loadable through configuration;
- keep deeper metadata validation and stale-index rebuild policy as the next post-stabilization item.

Implemented:

- added `qornix_rag/vector_store.h`;
- added `qornix_rag/vector_store.cpp`;
- added `VectorStore`, `VectorRecord`, and `VectorSearchHit`;
- added `LocalHnswVectorStore` with build, search, save, load, clear, readiness, size, and dimension operations;
- moved `RagEngine` hybrid vector retrieval onto `VectorStore` instead of owning HNSW pointers directly;
- added `VectorStoreConfig` to `RagEngineConfig`;
- added `vector_store.backend`, `vector_store.index_path`, `vector_store.auto_load`, and `vector_store.auto_save` config parsing;
- added standalone and generated-app config examples for `local_hnsw` and `data/hnsw_index.bin`;
- updated portable bundle config rewriting and cleanup for `hnsw_index.bin`;
- ignored `qornix_rag/data/` runtime artifacts in Git;
- exposed `vector_store_backend` and `vector_store_status` through health and admin diagnostics responses;
- documented vector store config and API health/diagnostics fields;
- added `qornix_rag/tests/test_vector_store.cpp`;
- added vector-store config parsing coverage to `test_embedding_config`;
- fixed a latent missing `<numeric>` include in `qa_source.cpp` that blocked a fresh rebuild after CMake reconfiguration.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target test_vector_store test_embedding_config test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config|rag_service|document_chunker|sqlite_source|ingestion_pipeline)$' --output-on-failure
timeout 8s ./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8098 --project qornix_rag/doc/project_doc
timeout 8s ./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8098 --project qornix_rag/doc/project_doc
curl -fsS http://127.0.0.1:8098/api/health
curl -fsS http://127.0.0.1:8098/api/admin/diagnostics
./create_new_project.sh /tmp/qornix_app_with_vector_store --with-rag
cmake -S /tmp/qornix_app_with_vector_store -B /tmp/qornix_app_with_vector_store/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_with_vector_store/build -j2
git diff --check
```

Observed result:

```text
100% tests passed, 0 tests failed out of 6.
First standalone smoke saved qornix_rag/data/hnsw_index.bin.
Second standalone smoke loaded qornix_rag/data/hnsw_index.bin.
/api/health and /api/admin/diagnostics reported vector_store_backend=local_hnsw and vector_store_status=loaded.
Generated --with-rag app included vector_store config and built successfully.
```

## 2026-05-23 - Post-stabilization vector index metadata validation completed

Status: `done`

Scope:

- close the second `Recommended post-stabilization order` item;
- prevent stale local HNSW index reuse when the embedding model, dimension, vector count, or indexed document/chunk snapshot changes.

Implemented:

- added `vector_store.metadata_path` config parsing;
- added default metadata sidecar paths to standalone, `rag_app`, and `--with-rag` generated configs;
- added vector index metadata sidecar writing after local HNSW save;
- metadata records include backend, index path, embedding model id/backend, embedding dimension, vector count, document/chunk snapshot hash, and build time;
- load now validates metadata before loading the local HNSW index;
- stale or missing metadata causes load skip and explicit rebuild/save;
- portable bundle config rewriting and cleanup now handles `hnsw_index.meta.json`;
- `test_rag_service` now verifies save, metadata creation, same-snapshot load, and changed-snapshot rebuild behavior.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target test_vector_store test_embedding_config test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config|rag_service|document_chunker|sqlite_source|ingestion_pipeline)$' --output-on-failure
timeout 8s ./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8098 --project qornix_rag/doc/project_doc
timeout 8s ./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8098 --project qornix_rag/doc/project_doc
curl -fsS http://127.0.0.1:8098/api/health
curl -fsS http://127.0.0.1:8098/api/admin/diagnostics
./create_new_project.sh /tmp/qornix_app_with_vector_store --with-rag
cmake -S /tmp/qornix_app_with_vector_store -B /tmp/qornix_app_with_vector_store/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_with_vector_store/build -j2
git diff --check
```

Observed result:

```text
100% tests passed, 0 tests failed out of 6.
First standalone smoke skipped the old persisted vector index because metadata was missing, rebuilt, and wrote metadata.
Second standalone smoke loaded qornix_rag/data/hnsw_index.bin with matching metadata.
/api/health and /api/admin/diagnostics reported vector_store_status=loaded.
Generated --with-rag app included index and metadata paths and built successfully.
```

## 2026-05-23 - Post-stabilization background ingestion baseline completed

Status: `done`

Scope:

- close the third `Recommended post-stabilization order` item with a polling-based background ingestion baseline;
- keep the existing synchronous ingestion behavior intact.

Implemented:

- added `RagService::startBackgroundIngestProject()`;
- added in-memory active ingestion job tracking with `progress_percent` and `background` fields;
- updated job lookup/listing so active background jobs are visible before durable completion records are written;
- kept completed/failed job history backed by existing SQLite durable ingestion job records;
- extended `POST /api/ingest` to accept `async: true` or `background: true`;
- async ingestion returns `202 Accepted` with the queued/running job object;
- `GET /api/ingest/{id}` and `GET /api/ingest/jobs` can be used for polling;
- added service regression coverage for background ingestion status/progress through `test_rag_service`;
- documented async ingestion request/response behavior in the API docs.

Verified:

```bash
cmake --build build --target test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_rag_service$' --output-on-failure
cmake --build build --target test_vector_store test_embedding_config test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config|rag_service|document_chunker|sqlite_source|ingestion_pipeline)$' --output-on-failure
./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8099 --project qornix_rag/doc/project_doc
curl -fsS -X POST http://127.0.0.1:8099/api/ingest -H 'Content-Type: application/json' -d '{"project_path":"qornix_rag/doc/project_doc","async":true}'
curl -fsS http://127.0.0.1:8099/api/ingest/{job_id}
curl -fsS 'http://127.0.0.1:8099/api/ingest/jobs?limit=3'
git diff --check
```

Observed result:

```text
100% tests passed, 0 tests failed out of 6.
Built target qornix_rag, qornix_web, and qornix_rag_route_extension.
POST /api/ingest with async=true returned a queued background job.
GET /api/ingest/{id} returned completed job counters and progress_percent=100.
GET /api/ingest/jobs returned the completed job in history.
```

## 2026-05-23 - Post-stabilization incremental in-memory reindex baseline completed

Status: `done`

Scope:

- close the fourth `Recommended post-stabilization order` item with an in-memory incremental reindex baseline;
- avoid recomputing embeddings for unchanged chunks during repeated indexing of the same engine.

Implemented:

- added reusable embedding keys based on effective embedding model id, chunk relative path, and chunk content hash;
- `RagEngine::index_project()` now captures reusable embeddings before replacing the in-memory document snapshot;
- `RagEngine::indexSources()` uses the same unchanged-chunk embedding reuse path;
- unchanged chunks reuse previous embeddings;
- changed or new chunks generate fresh embeddings;
- removed chunks naturally become stale when the new in-memory snapshot replaces the old one;
- added incremental counters to `ProjectStats`: `indexed_chunks`, `reused_embeddings`, `generated_embeddings`, and `stale_embeddings`;
- exposed incremental counters through index/ingest responses, `/api/health`, `/api/stats`, and admin diagnostics;
- added regression coverage in `test_rag_service` for first index, unchanged reindex, and changed snapshot behavior.

Verified:

```bash
cmake --build build --target test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_rag_service$' --output-on-failure
cmake --build build --target test_vector_store test_embedding_config test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config|rag_service|document_chunker|sqlite_source|ingestion_pipeline)$' --output-on-failure
./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8100 --project qornix_rag/doc/project_doc
curl -fsS -X POST http://127.0.0.1:8100/api/index -H 'Content-Type: application/json' -d '{"project_path":"qornix_rag/doc/project_doc"}'
curl -fsS http://127.0.0.1:8100/api/stats
curl -fsS http://127.0.0.1:8100/api/health
git diff --check
```

Observed result:

```text
100% tests passed, 0 tests failed out of 1.
100% focused tests passed, 0 tests failed out of 6.
Built target qornix_rag, qornix_web, and qornix_rag_route_extension.
Repeated POST /api/index reused all unchanged chunk embeddings.
/api/stats and /api/health returned indexed_chunks, reused_embeddings, generated_embeddings, and stale_embeddings.
```

## 2026-05-24 - Post-stabilization reranking and query expansion baseline completed

Status: `done`

Scope:

- close the fifth `Recommended post-stabilization order` item with a deterministic retrieval-quality baseline;
- keep query expansion visible in API responses instead of silently changing user intent;
- avoid adding model-based reranking or evaluation datasets in this step.

Implemented:

- added `search.use_query_expansion`, `search.use_reranking`, `search.rerank_input_multiplier`, and rerank boost config parsing;
- added deterministic lexical query expansion with normalized terms, simple singular variants, identifier splitting, and path-like stem extraction;
- added a reranking pass after first-stage vector/Xapian retrieval using path, chunk metadata, and exact content phrase boosts;
- reranking now collects a larger first-stage candidate pool and trims back to requested `top_k` after rerank;
- exposed `expanded_query`, `query_expansion_applied`, and `reranking_applied` through Search and Ask service/API responses;
- exposed query expansion and reranking flags through health, stats, and admin diagnostics;
- updated standalone, generated `rag_app`, and generated `--with-rag` config defaults;
- documented retrieval-quality config and API response fields;
- added service/config regression coverage.

Verified:

```bash
cmake --build build --target test_rag_service test_embedding_config qornix_rag -j2
ctest --test-dir build -R 'test_(rag_service|embedding_config)$' --output-on-failure
cmake --build build --target test_vector_store test_embedding_config test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config|rag_service|document_chunker|sqlite_source|ingestion_pipeline)$' --output-on-failure
bash -n create_new_project.sh
./create_new_project.sh /tmp/qornix_app_ps5 --with-rag
cmake -S /tmp/qornix_app_ps5 -B /tmp/qornix_app_ps5/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_ps5/build -j2
git diff --check
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 6.
qornix_rag, qornix_web, and qornix_rag_route_extension rebuilt successfully.
Standalone smoke returned query_expansion/reranking health fields and expanded_query/search flags.
Generated --with-rag app included the retrieval-quality config and built successfully.
create_new_project.sh syntax check passed.
git diff --check passed.
```

## 2026-05-24 - Post-stabilization evaluation regression baseline completed

Status: `done`

Scope:

- close the sixth `Recommended post-stabilization order` item with a first local evaluation dataset;
- add deterministic regression coverage for retrieval quality, citation correctness, QA citation ordering, and no-context refusal behavior;
- avoid depending on an external LLM for quality regression tests.

Implemented:

- added `qornix_rag/tests/eval/retrieval_quality.json` as a small fixture dataset with local docs, QA pairs, and expected evaluation cases;
- added `qornix_rag/tests/test_rag_quality_eval.cpp`;
- the quality eval test creates a temporary project from the dataset and indexes it through `RagService`;
- retrieval quality case verifies expected top source, snippet content, confidence, query expansion, and reranking flags;
- citation case verifies Ask context source paths, `S*` citation ids, grounding status, and LLM-unavailable fallback behavior;
- QA case verifies matching QA pairs are placed first in Ask context with `Q*` citation ids when SQLite storage is available;
- refusal case verifies unrelated questions produce empty context, no citations, `grounding_status=no_context`, and `llm_status=unavailable`;
- registered `test_rag_quality_eval` in `qornix_rag/tests/CMakeLists.txt`.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target test_rag_quality_eval -j2
ctest --test-dir build -R 'test_rag_quality_eval$' --output-on-failure
cmake --build build --target test_vector_store test_embedding_config test_rag_service test_rag_quality_eval qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config|rag_service|rag_quality_eval|document_chunker|sqlite_source|ingestion_pipeline)$' --output-on-failure
git diff --check
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 7.
qornix_rag, qornix_web, and qornix_rag_route_extension rebuilt successfully.
git diff --check passed.
```

## 2026-05-24 - Post-stabilization admin UI and route auth baseline completed

Status: `done`

Scope:

- close the seventh `Recommended post-stabilization order` item with a first generated-app admin/security baseline;
- keep standalone local mode usable without mandatory auth;
- add an upstream-friendly host auth/RBAC integration point without implementing full user/session management inside standalone RAG.

Implemented:

- added `RagRouteAuthOptions` and `RagConfig.security`;
- parsed `security.*` and `rag.security.*` config keys;
- added optional RAG route guard modes:
  - `admin_token`, accepting `Authorization: Bearer <token>` or `X-Qornix-RAG-Admin-Token`;
  - `host_header`, accepting a configured role header from a trusted host app or reverse proxy;
- protected admin routes and write routes when `security.enabled` is true;
- exposed route auth status through admin diagnostics;
- added `security` defaults to standalone, dedicated `rag_app`, and generated `--with-rag` configs;
- added an Admin tab to the RAG UI with local admin-token storage, diagnostics, ingestion job history, and Prometheus metrics views;
- synced the dedicated `templates/rag_app/templates/rag_interface.html` UI copy;
- documented route security in API, config, generated README, operations, and integration docs;
- added config parsing coverage for `rag.security.*`.

Verified:

```bash
cmake --build build --target test_embedding_config test_rag_service test_rag_quality_eval qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
QORNIX_RAG_ADMIN_TOKEN=secret ./build/qornix_rag/qornix_rag --config <tmp-security-enabled-config> --port 8102 --project qornix_rag/doc/project_doc
curl http://127.0.0.1:8102/api/admin/diagnostics
curl -H 'X-Qornix-RAG-Admin-Token: secret' http://127.0.0.1:8102/api/admin/diagnostics
./create_new_project.sh /tmp/qornix_app_ps7b --with-rag
cmake -S /tmp/qornix_app_ps7b -B /tmp/qornix_app_ps7b/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_ps7b/build -j2
git diff --check
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 3.
qornix_rag, qornix_web, and qornix_rag_route_extension rebuilt successfully.
Standalone auth smoke returned 401 without token and 200 with token.
Generated --with-rag app included security config and Admin tab.
Generated app auth smoke returned /rag 200, diagnostics 401 without token, and diagnostics 200 with token.
git diff --check passed.
```

## 2026-05-25 - Post-stabilization qornix_auth baseline completed

Status: `done`

Scope:

- stabilize `qornix_auth` before replacing RAG's token/header guard with full host-application authentication;
- keep standalone local RAG mode free of mandatory authentication;
- provide reusable auth primitives that generated `qornix_web` apps can use for sessions, JWT, roles, and permissions.

Implemented:

- added `AuthStore` and `InMemoryAuthStore` as the persistence boundary for users;
- changed the default password hasher to PBKDF2-SHA256 through OpenSSL while retaining legacy SHA-256 verification fallback;
- changed session id generation to cryptographically random OpenSSL bytes;
- added structured `AuthResult` fields for username, session id, JWT token, roles, and permissions;
- added user roles and permissions plus role/permission helpers;
- added `AuthContext` for authenticated request identity;
- added `AuthManager` helpers for session/JWT authentication, role checks, permission checks, role assignment, and permission grants;
- updated `AuthMiddleware` to validate sessions or bearer tokens and enforce optional role/permission requirements;
- added `AuthApiHandler` and `setupAuthRoutes()` for `/auth/login`, `/auth/logout`, `/auth/me`, and optional `/auth/register`;
- added `qornix_auth/README.md`;
- added `auth_manager_test` and `auth_routes_test`.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_TESTS=ON -DENABLE_AUTH=ON -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target auth_manager_test auth_routes_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(manager|routes)_test|test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 5.
qornix_web, qornix_rag, and qornix_rag_route_extension rebuilt successfully.
```

## 2026-05-25 - Post-stabilization qornix_orm auth store baseline completed

Status: `done`

Scope:

- add durable auth storage through `qornix_orm`, not a SQLite-only auth backend;
- keep SQLite as the self-contained test backend while allowing generated applications to select PostgreSQL or MySQL through normal ORM driver configuration;
- preserve the `AuthStore` boundary added by the previous auth baseline.

Implemented:

- added `qornix_auth/include/auth_orm_store.h`;
- added `QornixOrmAuthStore`, backed by `DatabaseInterface`;
- added auth schema migration for:
  - `auth_users`;
  - `auth_roles`;
  - `auth_permissions`;
  - `auth_user_roles`;
  - `auth_user_permissions`;
- kept backend-specific SQL details inside the store:
  - `?` placeholders for SQLite;
  - `$n` placeholders for PostgreSQL and MySQL driver param substitution;
  - `ON CONFLICT DO NOTHING` for SQLite/PostgreSQL;
  - `INSERT IGNORE` for MySQL;
- updated `qornix_auth/README.md` to document `QornixOrmAuthStore` and SQLite/PostgreSQL/MySQL backend selection;
- added `auth_orm_store_test` as a self-contained ORM-backed SQLite integration test.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_TESTS=ON -DENABLE_AUTH=ON -DQORNIX_ENABLE_ORM=ON -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target auth_orm_store_test auth_manager_test auth_routes_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(manager|routes|orm_store)_test|test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 6.
qornix_web, qornix_rag, and qornix_rag_route_extension rebuilt successfully.
```

## 2026-05-25 - Post-stabilization generated app auth wiring completed

Status: `done`

Scope:

- wire generated `rag_app` and default `--with-rag` applications to `qornix_auth`;
- use `QornixOrmAuthStore` for durable user/role/permission storage;
- keep runtime auth opt-in through config so local generated apps still start without login by default.

Implemented:

- generated `rag_app` projects now build with `qornix_auth` and `qornix_orm` enabled by default;
- generated default app projects now expose compile definitions for auth/ORM integration and keep RAG-compatible auth wiring available;
- generated apps can create `AuthManager` from `auth.*` config;
- generated apps can create `QornixOrmAuthStore` from `auth.database.*` config;
- added `/auth/login`, `/auth/logout`, `/auth/me`, and optional `/auth/register` route registration in generated apps when `auth.enabled: true`;
- added auth middleware registration in generated apps when `auth.enabled: true`;
- added optional bootstrap admin creation through `auth.bootstrap_admin.*` and `QORNIX_ADMIN_PASSWORD`;
- added generated config examples for SQLite plus PostgreSQL/MySQL connection strings;
- added DSN-gated `auth_orm_store_dsn_test` for PostgreSQL/MySQL/other ORM driver smoke coverage when environment variables are present.

Verified:

```bash
./create_new_project.sh /tmp/qornix_rag_auth_app --template rag_app
./create_new_project.sh /tmp/qornix_with_rag_auth_app --with-rag
cmake -S /tmp/qornix_rag_auth_app -B /tmp/qornix_rag_auth_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_rag_auth_app/build -j2
cmake -S /tmp/qornix_with_rag_auth_app -B /tmp/qornix_with_rag_auth_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_with_rag_auth_app/build -j2
cmake --build build --target auth_orm_store_dsn_test auth_orm_store_test auth_manager_test auth_routes_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(manager|routes|orm_store|orm_store_dsn)_test|test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
```

Runtime smoke:

```text
auth.enabled=true and bootstrap admin via QORNIX_ADMIN_PASSWORD
GET /rag without cookie -> 401
POST /auth/login -> 200 with session_id cookie
GET /auth/me with cookie -> 200
GET /rag with cookie -> 200
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 7.
Generated rag_app and --with-rag applications built successfully.
```

## 2026-05-25 - Post-stabilization auth route policies completed

Status: `done`

Scope:

- replace coarse generated-app auth requirements with route-level policies;
- enforce separate RAG read, write, and admin permissions through `qornix_auth`;
- keep the generated-app runtime auth opt-in through `auth.enabled`.

Implemented:

- added `AuthRoutePolicy` to `AuthMiddleware`;
- added method/path exact or prefix matching for route policies;
- added per-policy `authentication_required`, `any_roles`, and `any_permissions`;
- kept global middleware roles/permissions as fallback when no route policy matches;
- wired generated `rag_app` and `--with-rag` RAG policies:
  - `rag:read` for `/rag`, health, sources, stats, search, ask, and batch;
  - `rag:write` for indexing, ingestion, document delete, QA writes, imports, and source writes;
  - `rag:admin` for diagnostics, metrics, analytics, and admin routes;
- exposed `auth.rag_read_permissions`, `auth.rag_write_permissions`, and `auth.rag_admin_permissions` in generated config;
- documented route permissions in `qornix_auth/README.md` and generated operations docs;
- added `auth_middleware_policy_test`.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_TESTS=ON -DENABLE_AUTH=ON -DQORNIX_ENABLE_ORM=ON -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target auth_middleware_policy_test auth_orm_store_dsn_test auth_orm_store_test auth_manager_test auth_routes_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(manager|routes|middleware_policy|orm_store|orm_store_dsn)_test|test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
./create_new_project.sh /tmp/qornix_policy_rag_app --template rag_app
./create_new_project.sh /tmp/qornix_policy_with_rag_app --with-rag
cmake --build /tmp/qornix_policy_rag_app/build -j2
cmake --build /tmp/qornix_policy_with_rag_app/build -j2
```

Runtime smoke:

```text
auth.enabled=true and bootstrap admin via QORNIX_ADMIN_PASSWORD
GET /rag without cookie -> 401
POST /auth/login -> 200 with session_id cookie
GET /api/rag/admin/diagnostics with admin cookie -> 200
POST /api/rag/index with admin cookie -> 200
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 8.
Generated rag_app and --with-rag applications built successfully.
```

## 2026-05-25 - Post-stabilization auth admin UI and API completed

Status: `done`

Scope:

- add generated-app login/logout controls in the RAG Admin tab;
- add `auth:admin` user-management API routes on top of `qornix_auth`;
- keep RAG route permissions separate from auth administration permissions.

Implemented:

- added `GET /auth/users`, `POST /auth/users`, and `PATCH /auth/users/{id}`;
- protected user-management routes through generated-app `auth.admin_permissions`, defaulting to `auth:admin`;
- added `auth:admin` to generated bootstrap admin permissions;
- added Admin-tab session login/logout controls;
- added Admin-tab user listing, user creation, role/permission editing, active-state update, and password update controls;
- documented the admin API, UI flow, and permission split in `qornix_auth/README.md` and generated operations docs;
- extended `auth_routes_test` with admin create/list/update coverage.

Verified:

```bash
cmake -S . -B build -DQORNIX_BUILD_TESTS=ON -DENABLE_AUTH=ON -DQORNIX_ENABLE_ORM=ON -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target auth_routes_test auth_middleware_policy_test auth_orm_store_test auth_orm_store_dsn_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(manager|routes|middleware_policy|orm_store|orm_store_dsn)_test|test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
./create_new_project.sh /tmp/qornix_auth_ui_rag_app --template rag_app
cmake -S /tmp/qornix_auth_ui_rag_app -B /tmp/qornix_auth_ui_rag_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_auth_ui_rag_app/build -j2
./create_new_project.sh /tmp/qornix_auth_ui_with_rag_app --with-rag
cmake -S /tmp/qornix_auth_ui_with_rag_app -B /tmp/qornix_auth_ui_with_rag_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_auth_ui_with_rag_app/build -j2
```

Runtime smoke:

```text
auth.enabled=true and bootstrap admin via QORNIX_ADMIN_PASSWORD
GET /rag without cookie -> 401
POST /auth/login as admin -> 200 with session_id cookie
GET /auth/users with admin cookie -> 200
POST /auth/users with admin cookie -> 201
POST /auth/login as reader -> 200
GET /rag with reader cookie -> 200
GET /api/rag/admin/diagnostics with reader cookie -> 403
PATCH /auth/users/{id} with admin cookie -> 200
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 8.
Generated rag_app and --with-rag applications built successfully.
Generated rag_app auth admin runtime smoke passed.
```

## 2026-05-25 - Post-stabilization auth/RBAC hardening completed

Status: `done`

Scope:

- make session and bearer-token authorization reflect the current user record;
- revoke access for disabled users without requiring generated applications to restart;
- tighten route policy and cookie matching around auth-sensitive routes.

Implemented:

- `AuthManager::authenticateSession()` now reloads the current user from `AuthStore`;
- active sessions now use the latest stored roles and permissions instead of stale session snapshots;
- disabled or missing users now cause session authentication to fail and invalidate the session;
- `AuthManager::authenticateBearerToken()` now validates the token subject against `AuthStore`;
- bearer-token requests now use current stored roles and permissions instead of stale JWT claims;
- disabled or missing users now cause bearer-token authentication to fail;
- auth middleware exclude paths and non-exact route policies now require a path boundary, so `/api/rag/admin` does not match `/api/rag/administrator` and `/auth/login` does not match `/auth/login-extra`;
- auth cookie extraction now matches the exact `session_id` cookie name instead of substrings such as `other_session_id`;
- added regression coverage for session permission refresh, disabled-user session/JWT rejection, route prefix boundaries, exclude path boundaries, and exact cookie-name parsing.

Verified:

```bash
cmake --build build --target auth_manager_test auth_routes_test auth_middleware_policy_test -j2
ctest --test-dir build -R 'auth_(manager|routes|middleware_policy)_test$' --output-on-failure
cmake --build build --target auth_manager_test auth_routes_test auth_middleware_policy_test auth_orm_store_test auth_orm_store_dsn_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(manager|routes|middleware_policy|orm_store|orm_store_dsn)_test|test_(embedding_config|rag_service|rag_quality_eval)$' --output-on-failure
./create_new_project.sh /tmp/qornix_auth_harden_rag_app --template rag_app
./create_new_project.sh /tmp/qornix_auth_harden_with_rag_app --with-rag
cmake -S /tmp/qornix_auth_harden_rag_app -B /tmp/qornix_auth_harden_rag_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_auth_harden_rag_app/build -j2
cmake -S /tmp/qornix_auth_harden_with_rag_app -B /tmp/qornix_auth_harden_with_rag_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_auth_harden_with_rag_app/build -j2
git diff --check
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 8.
qornix_web, qornix_rag, and qornix_rag_route_extension rebuilt successfully.
Generated rag_app and --with-rag applications built successfully.
git diff --check passed.
```

## 2026-05-25 - Post-stabilization QA server-side pagination baseline completed

Status: `done`

Scope:

- close backlog item 11.4 with a scalable QA list baseline;
- stop loading the full QA list into the browser for normal standalone/generated UI use;
- keep existing `/api/qa/list` compatibility while adding the target `items/total/limit/offset/has_more` contract.

Implemented:

- added SQLite-backed QA list filtering by `query` and `category`;
- added server-side `limit` and `offset` pagination with total counts;
- added QA question suggestions through `GET /api/qa/suggest?q=...&limit=...`;
- added QA category suggestions through `GET /api/qa/categories?q=...&limit=...`;
- added reusable `RagService` DTOs for QA list, suggestions, and categories;
- updated `/api/qa/list` and `/api/rag/qa/list` to return `items`, `total`, `limit`, `offset`, and `has_more`;
- kept `pairs` as a compatibility alias for older UI/API clients;
- updated the standalone RAG UI and generated `rag_app` UI to use server-backed pages, filters, categories, and question suggestions;
- documented the updated QA API shape;
- updated roadmap backlog 11.4 from deferred to completed baseline with remaining ORM/FTS/tag follow-ups.

Verified:

```bash
cmake --build build --target test_sqlite_source test_rag_service qornix_rag -j2
ctest --test-dir build -R 'test_(sqlite_source|rag_service)$' --output-on-failure
cmake --build build --target test_sqlite_source test_rag_service test_embedding_config test_rag_quality_eval qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(sqlite_source|rag_service|embedding_config|rag_quality_eval)$' --output-on-failure
./create_new_project.sh /tmp/qornix_qa_list_rag_app --template rag_app
./create_new_project.sh /tmp/qornix_qa_list_with_rag_app --with-rag
cmake -S /tmp/qornix_qa_list_rag_app -B /tmp/qornix_qa_list_rag_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_qa_list_rag_app/build -j2
cmake -S /tmp/qornix_qa_list_with_rag_app -B /tmp/qornix_qa_list_with_rag_app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_qa_list_with_rag_app/build -j2
./build/qornix_rag/qornix_rag --config qornix_rag/config.yaml --port 8110 --project qornix_rag/doc/project_doc
curl -fsS 'http://127.0.0.1:8110/api/qa/list?query=smoke&category=qa-scale-smoke&limit=1&offset=0'
curl -fsS 'http://127.0.0.1:8110/api/qa/suggest?q=paginate&limit=5'
curl -fsS 'http://127.0.0.1:8110/api/qa/categories?q=qa-scale&limit=5'
git diff --check
```

Observed result:

```text
100% focused tests passed, 0 tests failed out of 4.
Built qornix_rag, qornix_web, and qornix_rag_route_extension.
Generated rag_app and --with-rag applications built successfully.
Standalone smoke returned filtered QA list, QA suggestions, and QA categories.
git diff --check passed.
```

## 2026-05-25 - Post-stabilization embedding model registry baseline completed

Goal:
- close backlog item 11.7 with a config-driven embedding model registry baseline;
- support multiple installed embedding model definitions without adding runtime switching or installer scope;
- surface active model selection and registry validation through health/admin diagnostics.

Implemented:
- added `EmbeddingModelRegistry` to `RagEngineConfig` and applied `embedding.active_model_id` before embedding backend initialization;
- added flat config parsing for `embedding.registry.<id>.*` and `rag.embedding.registry.<id>.*`;
- validated registry backend, ONNX model/tokenizer paths, pooling mode, dimensions, tokenizer settings, and license/source metadata;
- preserved generated-app path resolution for active registry model paths;
- exposed registry active id, size, model ids, and warnings through RAG service health, `/api/health`, and admin diagnostics;
- documented the registry shape in standalone and generated-app config/docs;
- added regression tests for registry parsing, active model application, missing active id warnings, and engine model info.

Deferred:
- runtime model switching and explicit reindex/re-embed orchestration;
- model install/download command that writes registry metadata;
- tokenizer implementations beyond the current basic WordPiece-like path;
- automatic registry discovery from a models directory.

Validation:
```text
cmake --build build --target test_embedding_config test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(embedding_config|rag_service|sqlite_source|rag_quality_eval)$' --output-on-failure
./create_new_project.sh /tmp/qornix_model_registry_rag_app --template rag_app
./create_new_project.sh /tmp/qornix_model_registry_with_rag_app --with-rag
cmake -S /tmp/qornix_model_registry_rag_app -B /tmp/qornix_model_registry_rag_app/build && cmake --build /tmp/qornix_model_registry_rag_app/build -j2
cmake -S /tmp/qornix_model_registry_with_rag_app -B /tmp/qornix_model_registry_with_rag_app/build && cmake --build /tmp/qornix_model_registry_with_rag_app/build -j2
git diff --check
```

## 2026-05-25 - Post-stabilization generated-app CSRF baseline completed

Goal:
- close the first 11.10 security hardening baseline after generated-app auth/RBAC;
- protect cookie-authenticated browser write/admin routes from CSRF without blocking bearer/JWT/API-token clients;
- keep generated `rag_app` and `--with-rag` templates buildable with auth enabled or disabled.

Implemented:
- added per-session CSRF tokens to `qornix_auth` session metadata;
- exposed `csrf_token` from `/auth/login` and `/auth/me`;
- added optional `AuthMiddleware` CSRF validation for `POST`, `PUT`, `PATCH`, and `DELETE` when the authenticated credential type is `session`;
- wired generated apps to enable `auth.csrf.enabled` by default and use `X-CSRF-Token`;
- updated bundled RAG Admin UI fetch helpers to retain and send CSRF tokens for unsafe browser requests;
- documented the generated-app CSRF config and security checklist guidance;
- added regression coverage for CSRF token issuance and middleware enforcement.

Deferred:
- session rotation after login and privilege-sensitive user updates;
- audit events for login/logout/failed login/user-management/denied access;
- password reset, invite, account recovery, MFA, and stricter cookie policy;
- TLS/reverse-proxy examples and broader production container hardening.

Validation:
```text
cmake --build build --target auth_routes_test auth_middleware_policy_test qornix_web -j2
ctest --test-dir build -R 'auth_(routes|middleware_policy|manager|orm_store|orm_store_dsn)_test' --output-on-failure
cmake --build build --target auth_routes_test auth_middleware_policy_test qornix_web qornix_rag qornix_rag_route_extension -j2
ctest --test-dir build -R 'auth_(routes|middleware_policy|manager|orm_store|orm_store_dsn)_test|test_(embedding_config|rag_service|sqlite_source|rag_quality_eval)$' --output-on-failure
./create_new_project.sh /tmp/qornix_csrf_rag_app --template rag_app
./create_new_project.sh /tmp/qornix_csrf_with_rag_app --with-rag
cmake -S /tmp/qornix_csrf_rag_app -B /tmp/qornix_csrf_rag_app/build && cmake --build /tmp/qornix_csrf_rag_app/build -j2
cmake -S /tmp/qornix_csrf_with_rag_app -B /tmp/qornix_csrf_with_rag_app/build && cmake --build /tmp/qornix_csrf_with_rag_app/build -j2
git diff --check
```

## 2026-05-26 - Post-stabilization LLM response robustness baseline completed

Goal:
- close backlog item 11.2 with a shared JSON parser baseline;
- stop hand-scanning provider JSON answer strings in `LLMClient`;
- surface parser failures and truncation separately from provider availability.

Implemented:
- moved LLM answer parsing to `Boost.JSON`, matching the JSON library already used by `qornix_web`;
- added `LLMGenerationResult`, `parse_response_result()`, and `ask_with_metadata()` while keeping the existing string-returning `parse_response()` and `ask()` compatibility APIs;
- parsed Ollama chat `message.content`, Ollama generate `response`, OpenAI-compatible `choices[].message.content`, and streaming-style `choices[].delta.content`;
- supported newline-delimited JSON and SSE-style `data:` payload lines for accidental/streaming provider responses;
- added parser-specific status metadata: `parser_error`, `provider_error`, `truncated`, `finish_reason`, and `truncated` flag;
- exposed `llm_truncated`, `llm_finish_reason`, and `llm_parser_error` through `/api/ask`;
- updated the standalone/generated RAG UI status line to show truncation and parser diagnostics;
- avoided treating arbitrary answer text containing the word `error` as a provider failure by parsing provider error payloads as JSON;
- added LLM parser regression coverage for multiline code blocks, quoted includes, JSON snippets, markdown tables, escaped backslashes, SSE-style chunks, parser errors, and truncation metadata;
- documented the new Ask response fields.

Deferred:
- replace remaining non-answer JSON helpers such as model-list extraction and token usage parsing with `Boost.JSON`;
- add parser fixtures from real Ollama/OpenAI/vLLM/LM Studio responses;
- route streaming SSE callback parsing through the same structured parser path end-to-end.

Validation:
```text
cmake --build build --target test_llm_client test_rag_service qornix_rag qornix_web qornix_rag_route_extension -j2
ctest --test-dir build -R 'test_(llm_client|rag_service|rag_quality_eval|embedding_config|sqlite_source)$' --output-on-failure
git diff --check
```

## 2026-05-26 - Forward backlog priority policy clarified

Reason:
- `qornix_rag` is primarily a standalone reusable RAG library/core;
- the standalone user application should remain a local single-user mode that may use `qornix_web` for HTTP/UI plumbing but should not be blocked by production web security;
- full network-application security belongs to generated/host `qornix_web` applications, not to standalone RAG core.

Updated priority order:
- Priority 1: finish `qornix_rag` as a standalone reusable RAG library/core.
- Priority 2: keep standalone user mode complete and ergonomic with local safety defaults, not production auth/RBAC.
- Priority 3: support generated `rag_app` and `--with-rag` integration after core and standalone flows are solid.
- Priority 4: implement CSRF/session rotation/audit/password recovery/cookie/TLS/reverse-proxy hardening as `qornix_web` / `qornix_auth` host-application capabilities.

Recommended next execution order:
- advanced ingestion adapters, starting with PDF;
- parser-to-chunker metadata contracts and richer chunking;
- RAG quality, citation post-processing, grounding checks, and expanded evals;
- local embedding/model operations such as installer, tokenizer upgrades, and explicit reindex/re-embed switching;
- QA/wiki quality improvements;
- production vector backend adapters;
- generated-app/web security hardening.

## 2026-05-26 - Post-stabilization PDF ingestion baseline completed

Goal:
- start the advanced ingestion adapter priority with PDF support for standalone/library RAG;
- keep the parser behind the existing `DocumentParser` registry;
- avoid adding mandatory PDF library dependencies to the portable/standalone build.

Implemented:
- added `.pdf` detection as `application/pdf` with document type `pdf`;
- added `PdfParser` as a first PDF text extraction adapter;
- uses the optional `pdftotext` command through `fork`/`exec` without shell interpolation;
- records a structured warning and skips the file when `pdftotext` is unavailable, extraction fails, or the PDF has no extractable text;
- stores PDF metadata fields including `mime_type`, `ingestion_parser=pdf_pdftotext`, `source_extension=.pdf`, and `pdf_text_extractor=pdftotext`;
- increased default standalone/generated indexing file-size limit to `4096 KB` so ordinary small PDFs are not skipped by the old source-code-sized limit;
- added a generated valid PDF fixture to `test_ingestion_pipeline` and verified PDF text ingestion when `pdftotext` is installed;
- updated README/config docs and roadmap backlog 11.6 to mark the PDF text baseline complete while keeping richer PDF page/metadata parsing deferred.

Deferred:
- page-aware PDF parsing and chunk metadata;
- PDF document metadata/outlines/attachments;
- DOCX, XLSX/CSV, PPTX, image, and OCR parser adapters;
- dependency documentation for optional parser tools beyond the first `pdftotext` note.

Validation:
```text
cmake --build build --target test_ingestion_pipeline test_rag_service qornix_rag -j2
ctest --test-dir build -R 'test_(ingestion_pipeline|rag_service)$' --output-on-failure
```

## 2026-05-26 - Post-stabilization DOCX ingestion baseline completed

Goal:
- continue the advanced ingestion adapter priority with DOCX support for standalone/library RAG;
- use a real ZIP library instead of shelling out to `unzip`;
- keep DOCX support optional so standalone builds still work without the development package.

Implemented:
- added optional `libzip` detection to `qornix_rag_core` through CMake;
- added `.docx` detection as `application/vnd.openxmlformats-officedocument.wordprocessingml.document` with document type `docx`;
- added `DocxParser` as a first DOCX text extraction adapter;
- reads `word/document.xml` from the DOCX archive through `libzip`;
- extracts plain text from WordprocessingML paragraphs/runs and decodes basic XML entities;
- records a structured warning and skips the file when `libzip` support is unavailable, the DOCX archive cannot be opened, `word/document.xml` is missing, or no text is extractable;
- stores DOCX metadata fields including `mime_type`, `ingestion_parser=docx_libzip`, `source_extension=.docx`, and `docx_archive_backend=libzip`;
- added `.docx` to reusable `FileSource` defaults;
- extended ingestion tests with a generated DOCX fixture and full extraction assertions when `QORNIX_HAS_LIBZIP` is enabled;
- updated README/config/API docs and roadmap backlog 11.6 to mark the DOCX text baseline complete while keeping richer DOCX structure/metadata parsing deferred.

Deferred:
- DOCX headings, tables, footnotes/endnotes, comments, styles, document properties, and structure metadata;
- XLSX/CSV, PPTX, image, and OCR parser adapters;
- parser-to-chunker metadata contracts for Office document structures.

Validation:
```text
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target test_ingestion_pipeline qornix_rag -j2
ctest --test-dir build -R 'test_ingestion_pipeline$' --output-on-failure
```

## 2026-05-26 - Post-stabilization XLSX/CSV ingestion baseline completed

Goal:
- continue the advanced ingestion adapter priority with spreadsheet support for standalone/library RAG;
- reuse dependency choices already present in related Qornix components;
- avoid ad-hoc ZIP/XML parsing for XLSX.

Implemented:
- added built-in `.csv` detection as `text/csv` with document type `csv`;
- added a `CsvParser` with delimiter detection, quoted-field handling, escaped quote handling, multiline field handling, and row/column metadata;
- added optional `.xlsx` detection as `application/vnd.openxmlformats-officedocument.spreadsheetml.sheet` with document type `xlsx`;
- added optional `pugixml` detection to `qornix_rag_core` and used it with the existing optional `libzip` Office document path;
- added `XlsxParser` backed by `libzip + pugixml` for workbook relationships, shared strings, worksheet rows, sheet names, and row/cell metadata;
- records structured warnings and skips XLSX files when `libzip + pugixml` support is unavailable or no extractable text is present;
- stores spreadsheet metadata fields such as `csv_row_count`, `csv_column_count`, `csv_delimiter`, `xlsx_sheet_count`, `xlsx_row_count`, `xlsx_cell_count`, `xlsx_sheet_names`, and parser backend fields;
- added `.csv` and `.xlsx` to reusable `FileSource` defaults;
- extended ingestion tests with generated CSV and XLSX fixtures and extraction assertions when `QORNIX_HAS_XLSX` is enabled;
- updated README/config/API docs and roadmap backlog 11.6 to mark the first XLSX/CSV text baseline complete while keeping richer spreadsheet structure/chunking deferred.

Deferred:
- XLSX formulas, merged cells, workbook properties, formatting signals, typed cell metadata, and multiple table-region detection;
- table-aware spreadsheet chunking and citation metadata contracts;
- PPTX, image, and OCR parser adapters.

Validation:
```text
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target test_ingestion_pipeline -j2
ctest --test-dir build -R 'test_ingestion_pipeline$' --output-on-failure
```

## 2026-05-26 - Post-stabilization PPTX ingestion baseline completed

Goal:
- continue the advanced ingestion adapter priority with presentation support for standalone/library RAG;
- reuse the same `libzip + pugixml` Open XML dependency path used for XLSX;
- keep PPTX support optional and non-fatal when dependencies are unavailable.

Implemented:
- added `.pptx` detection as `application/vnd.openxmlformats-officedocument.presentationml.presentation` with document type `pptx`;
- added `PptxParser` backed by `libzip + pugixml`;
- reads `ppt/presentation.xml`, presentation relationships, and referenced slide XML files;
- extracts plain text from DrawingML text runs on slides;
- records structured warnings and skips PPTX files when `libzip + pugixml` support is unavailable, the presentation relationship graph is missing, or no text is extractable;
- stores PPTX metadata fields including `mime_type`, `ingestion_parser=pptx_libzip_pugixml`, `source_extension=.pptx`, `pptx_archive_backend=libzip`, `pptx_xml_parser=pugixml`, `pptx_slide_count`, and `pptx_text_run_count`;
- added `.pptx` to reusable `FileSource` defaults;
- extended ingestion tests with a generated two-slide PPTX fixture and extraction assertions when `QORNIX_HAS_OPENXML` is enabled;
- updated README/config/API docs and roadmap backlog 11.6 to mark the first PPTX text baseline complete while keeping richer presentation structure/metadata parsing deferred.

Deferred:
- slide titles, notes, comments, speaker metadata, alt text, media captions, layout metadata, and richer per-slide citations;
- image ingestion with OCR;
- parser-to-chunker metadata contracts for Office document structures.

Validation:
```text
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target test_ingestion_pipeline -j2
ctest --test-dir build -R 'test_ingestion_pipeline$' --output-on-failure
```

## 2026-05-26 - Post-stabilization image OCR ingestion baseline completed

Goal:
- continue the advanced ingestion adapter priority with raster image OCR support for standalone/library RAG;
- keep OCR optional and non-fatal when the local OCR tool is unavailable;
- reuse the existing `DocumentParser` registry and structured ingestion issue behavior.

Implemented:
- added image extension detection for `.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.webp`, `.pbm`, `.pgm`, `.ppm`, and `.pnm` with document type `image`;
- added `ImageOcrParser` backed by the optional `tesseract` command through `fork`/`exec` without shell interpolation;
- records structured warnings and skips image files when `tesseract` is unavailable, OCR fails, or OCR produces no extractable text;
- stores image OCR metadata fields including `mime_type`, `ingestion_parser=image_tesseract_ocr`, `source_extension`, `image_ocr_engine=tesseract`, and `image_ocr_language=default`;
- added image extensions to reusable `FileSource` defaults;
- extended ingestion tests with a generated PGM fixture, image type detection assertions, and optional OCR metadata assertions when OCR produces text;
- updated README/config/API docs and roadmap backlog 11.6 to mark the first image OCR text baseline complete while keeping richer image metadata, OCR confidence, coordinates, language selection, and diagnostics deferred.

Deferred:
- image dimensions, EXIF metadata, OCR confidence, page/region coordinates, detected language, captions, and richer parser diagnostics;
- parser-to-chunker metadata contracts for OCR regions and captions;
- configurable OCR language/options.

Validation:
```text
cmake --build build --target test_ingestion_pipeline -j2
ctest --test-dir build -R 'test_ingestion_pipeline$' --output-on-failure
```

## 2026-05-26 - Post-stabilization parser-to-chunker metadata baseline completed

Goal:
- close the next core RAG item after image/OCR ingestion;
- preserve parser structure through chunking so retrieval/citations can reason about pages, rows, OCR regions, and code symbols;
- keep the baseline dependency-light and compatible with existing persisted chunk metadata.

Implemented:
- added stable parser-to-chunker structure hints including `structure_contract`, `pdf_page_count`, `table_*`, and `ocr_*`;
- changed PDF extraction to preserve page breaks and page labels from `pdftotext`, then chunk PDFs with `chunk_strategy=pdf_page`;
- added PDF chunk metadata: `chunk_page`, `chunk_page_start`, and `chunk_page_end`;
- changed XLSX extraction to preserve sheet/row line boundaries and added spreadsheet structure metadata for CSV/XLSX;
- added spreadsheet row chunking with `chunk_strategy=spreadsheet_table`, `chunk_sheet`, `chunk_row_start`, and `chunk_row_end`;
- added image OCR region/caption propagation with `chunk_strategy=image_ocr_region`, `chunk_ocr_region`, and optional `chunk_ocr_caption`;
- expanded code symbol detection across common declaration forms for C/C++, Python, JavaScript/TypeScript, Go, Rust, and Java/C-style functions/classes;
- added `chunk_symbol_name` and `chunk_symbol_kind` alongside the existing `chunk_symbol` display text;
- added tokenizer-limit-aware chunk budgets through `DocumentChunker::Config::tokenizer_max_tokens`, document metadata keys `tokenizer_max_tokens` / `embedding_token_limit`, and chunk metadata `chunk_token_budget`;
- extended `test_document_chunker` for PDF pages, spreadsheet rows, OCR region/caption metadata, code symbol names/kinds, and tokenizer budget limits;
- extended ingestion tests for PDF, spreadsheet, and OCR structure metadata.

Deferred:
- exact model-tokenizer tokenization beyond the current whitespace-token estimate and configured token budgets;
- AST-grade language parsers beyond the expanded regex-based baseline;
- PDF page boxes, outlines, coordinates, OCR confidence, spreadsheet merged-cell/formula metadata, and richer citation post-processing.

Validation:
```text
cmake --build build --target test_document_chunker test_ingestion_pipeline -j2
ctest --test-dir build -R 'test_(document_chunker|ingestion_pipeline)$' --output-on-failure
```

## 2026-05-26 - Post-stabilization RAG quality and grounding baseline completed

Goal:
- close the next core RAG quality item after parser-to-chunker metadata;
- make generated-answer grounding more explicit than retrieval confidence alone;
- define conversation-history rules before adding broader chat workflows;
- expand the local evaluation dataset beyond the first retrieval/QA/refusal cases.

Implemented:
- added answer citation extraction from generated/fallback answers into `answer_citations`;
- added `missing_citations` for answer citations that do not exist in retrieved context;
- added `uncited_context_citations` for retrieved source ids not cited by the answer;
- added `citations_post_processed` and source-id appending for generated answers that omit citations while using retrieved context;
- refined post-answer `grounding_status` with `unsupported_citations` and `uncited` states while preserving existing `grounded`, `partial`, `weak`, and `no_context`;
- added bounded conversation history support to `RagService::ask` and `/api/ask` through a `history` array;
- conversation history accepts only recent `user`/`assistant` turns, drops system/tool content, sanitizes text, and is prompt-only continuity context rather than a citable source;
- exposed `answer_citations`, `missing_citations`, `uncited_context_citations`, `citations_post_processed`, and `conversation_turns_used` through `/api/ask`;
- expanded `retrieval_quality.json` with citation post-processing and conversation-history rules documents/cases;
- extended `test_rag_service` and `test_rag_quality_eval` coverage for answer citations, missing citation checks, and conversation-history filtering.

Deferred:
- model-based claim verification against cited context beyond citation-id consistency checks;
- answer completeness scoring;
- generated-template and long-document evaluation fixtures;
- user feedback capture and answer-quality analytics.

Validation:
```text
cmake --build build --target test_rag_service test_rag_quality_eval qornix_rag -j2
ctest --test-dir build -R 'test_(rag_service|rag_quality_eval)$' --output-on-failure
```

## 2026-05-26 - Post-stabilization embedding/model operations baseline completed

Goal:
- close the local embedding/model operations item after answer grounding;
- provide a first-class install/download path that writes registry metadata;
- support runtime model switching with explicit reindex/re-embed orchestration;
- improve ONNX tokenization beyond exact whole-token lookup for BERT-like tokenizers.

Implemented:
- extended `download_onnx_model.sh` with `--model-id`, metadata, dimension, pooling, tokenizer, sequence-length, thread, and active-selection options;
- changed the downloader config update to write an `embedding.registry` entry and set `active_model_id` by default;
- copied the updated downloader behavior into generated RAG app templates;
- added `RagEngine::switch_active_embedding_model()` and `force_reembed_on_next_index()` so model changes can invalidate unchanged-chunk embedding reuse explicitly;
- added greedy WordPiece-style token splitting for ONNX tokenizers declared as `WordPiece` or `BertWordPiece`;
- added `RagService::embeddingModels()` and `RagService::switchEmbeddingModel()`;
- added `GET /api/embedding/models` and `POST /api/embedding/switch`;
- protected `/api/embedding/switch` as a write/admin route;
- extended embedding config/service tests for registry switching and forced full re-embedding.

Deferred:
- automatic model registry discovery from a models directory;
- deeper model/tokenizer compatibility validation after download;
- broader Hugging Face tokenizer JSON variants beyond the current greedy WordPiece path;
- persistent embedding cache storage beyond the existing model-id/content-hash namespace;
- ONNX Runtime integration tests with a real small model fixture.

Validation:
```text
cmake --build build --target qornix_rag test_embedding_config test_rag_service test_rag_api -j2
ctest --test-dir build -R 'test_(embedding_config|rag_service|rag_api)$' --output-on-failure
```

## 2026-05-26 - Post-stabilization generated-app hardening baseline completed

Status: `done`

Goal:
- close the generated-app hardening item owned by `qornix_web` / `qornix_auth`;
- keep standalone RAG local-first while improving generated networked app defaults;
- add testable auth/account/container/deploy primitives without pretending to provide full production operations.

Implemented:
- added session invalidation by user and login-time session rotation;
- added session invalidation after password changes, password resets, and admin user updates;
- added bounded auth audit events for login/logout/register/access denial/session denial/invite/password-reset/session invalidation flows;
- added manual invite-token and password-reset-token helpers in `AuthManager`;
- added `/auth/invites`, `/auth/invites/accept`, `/auth/password-reset/request`, `/auth/password-reset/confirm`, and `/auth/audit` routes;
- added configurable generated-app cookie policy for `secure_cookies`, `cookie_same_site`, and `cookie_path`;
- wired generated `app` and `rag_app` templates to protect invite and audit admin routes with `auth:admin`;
- added generated deploy smoke scripts;
- hardened generated runtime containers with non-root execution, healthchecks, and RAG Compose read-only/no-new-privileges/cap-drop settings;
- documented manual-token delivery, stricter cookie settings, TLS/reverse-proxy shape, container hardening, and smoke checks.

Deferred:
- durable audit-event storage/export;
- SMTP/notification-provider delivery for invites and password resets;
- cookie domain/max-age policy controls;
- Caddy/Traefik examples and complete Docker Compose flow automation;
- structured tracing, backup/restore commands, alert examples, SBOM/scanning, and secret-mounting guidance.

Validation:
```text
cmake --build build --target auth_manager_test auth_routes_test auth_middleware_policy_test qornix_web -j2
ctest --test-dir build -R 'auth_(manager|routes|middleware_policy)_test' --output-on-failure
./create_new_project.sh /tmp/qornix_hardening_rag_app --template rag_app
cmake -S /tmp/qornix_hardening_rag_app -B /tmp/qornix_hardening_rag_app/build
cmake --build /tmp/qornix_hardening_rag_app/build -j2
./create_new_project.sh /tmp/qornix_hardening_with_rag --with-rag
cmake -S /tmp/qornix_hardening_with_rag -B /tmp/qornix_hardening_with_rag/build
cmake --build /tmp/qornix_hardening_with_rag/build -j2
```

## 2026-05-26 - Post-stabilization production vector backend adapter baseline completed

Status: `done`

Goal:
- close the next production vector backend adapter item;
- keep local HNSW as the default while exposing Faiss, Qdrant, and pgvector through the existing `VectorStore` boundary;
- make optional dependency behavior explicit instead of silently falling back.

Implemented:
- extended `VectorStore` with backend options, `lastError()`, and a `createVectorStore()` factory;
- added optional `FaissVectorStore` with save/load label sidecar support when Faiss is available at build time;
- added `QdrantVectorStore` backed by CURL JSON collection creation, point upsert, and search requests;
- added `PgVectorStore` backed by libpq and PostgreSQL `vector` extension table creation, inserts, and nearest-neighbor search;
- added `vector_store.endpoint`, `api_key`, `collection`, `connection_string`, `table`, `distance`, and `recreate` config parsing;
- added optional CMake detection for Faiss and libpq while preserving the existing required local HNSW path;
- updated standalone/generated config examples and vector-store docs;
- added backend factory and config parsing regression coverage.

Deferred:
- live service integration tests for Qdrant and PostgreSQL+pgvector containers;
- Faiss CI coverage in a build image with Faiss installed;
- migration tooling from local HNSW/Faiss vectors into Qdrant or pgvector;
- large-corpus paginated upsert/delete synchronization and richer backend health diagnostics.

Validation:
```text
cmake --build build --target test_vector_store test_embedding_config -j2
ctest --test-dir build -R 'test_(vector_store|embedding_config)$' --output-on-failure
```

## 2026-05-26 - Post-stabilization QA/wiki quality baseline completed

Goal:
- close the next local QA/wiki quality item after embedding/model operations;
- make QA entries better structured for filtering, export, and attribution;
- improve QA result scoring without replacing the existing SQLite-backed workflow.

Implemented:
- added `tags` to `QASource::QAPair` and propagated tags through documents, service DTOs, Search results, Ask context, and API JSON;
- replaced placeholder `QASource::loadFromJson()` / `toJson()` with Boost.JSON import/export for `pairs`, aliases, tags, and string metadata;
- parsed SQLite QA aliases and metadata JSON back into `QAPair` objects;
- added tag filtering to SQLite-backed QA list queries and service list queries;
- added `RagService::listQaTags()`, `GET /api/qa/tags`, `POST /api/qa/import`, and `GET/POST /api/qa/export`;
- added conservative server-side Markdown rendering metadata as `answer_html`;
- added richer QA scoring that boosts exact question/answer/alias matches and keeps QA attribution fields in Search/Ask responses;
- preserved existing semantic duplicate detection endpoints while adding tags/import/export coverage around the QA data model.

Deferred:
- add/edit UI duplicate warnings before save;
- production-grade Markdown sanitizer and richer Markdown extensions;
- optional FTS/ORM-backed QA search and larger-scale tag autocomplete;
- full provenance/version history beyond string metadata fields.

Validation:
```text
cmake --build build --target qornix_rag test_sqlite_source test_rag_service test_data_sources test_deduplication -j2
ctest --test-dir build -R 'test_(sqlite_source|rag_service|data_sources|deduplication)$' --output-on-failure
```

## 2026-05-26 - Roadmap open-work refresh

Purpose:
- make the roadmap reflect that Post-stabilization 25-28 completed the embedding/model operations, QA/wiki quality, production vector adapter, and generated-app hardening baselines;
- keep baseline-complete items from looking like active pending work;
- add an explicit current backlog with concrete tasks that remain after the completed baselines.

Updated:
- added `0.1 Current explicit open work` to the roadmap;
- kept `Next - Generated app RAG UI polish` as the next pending item;
- made the remaining work explicit for generated app UI polish, vector backend hardening, advanced ingestion metadata, code/chunking depth, embedding/model registry hardening, RAG quality, QA/wiki quality, operations/security depth, and LLM/provider diagnostics;
- refreshed the deferred follow-up wording so it refers to the completed Post-stabilization 25-28 baselines.

No code changes were made.

## 2026-05-26 - Post-stabilization generated-app RAG UI polish completed

Status: `done`

Goal:
- close `0.1 Current explicit open work` item 1;
- make generated `rag_app` and `--with-rag` RAG views expose the capabilities added by the recent auth, QA, embedding/model, ingestion, and grounding baselines;
- keep the standalone RAG UI copy synchronized with the generated app template.

Implemented:
- added generated UI controls for QA tags, tag filtering, JSON import/export, and duplicate checks;
- added embedding model registry display and runtime model switching with explicit `force_reembed` and optional immediate reindex controls;
- expanded Ask/Search source attribution with tags, chunk/language metadata, answer citation counts, missing citation counts, grounding status, and confidence;
- added ingestion job progress bars to the Admin output;
- added auth session status to the page-level status toolbar;
- added generated app route policies for `/api/rag/embedding/models`, `/api/rag/embedding/switch`, `/api/rag/qa/tags`, `/api/rag/qa/import`, `/api/rag/qa/export`, and `/api/rag/ingest/jobs`;
- kept `qornix_rag/templates/rag_interface.html` and `templates/rag_app/templates/rag_interface.html` byte-identical after the UI update.

Deferred:
- deeper visual redesign beyond the existing single-file generated UI;
- inline duplicate warnings before save, which remain tracked under QA/wiki deeper quality;
- live browser-driven authenticated smoke tests for the generated UI.

Validation:
```text
cmp -s qornix_rag/templates/rag_interface.html templates/rag_app/templates/rag_interface.html
node --check <extracted script from templates/rag_app/templates/rag_interface.html>
cmake --build build --target qornix_web qornix_rag -j2
./create_new_project.sh /tmp/qornix_ui_polish_rag_app --template rag_app
cmake -S /tmp/qornix_ui_polish_rag_app -B /tmp/qornix_ui_polish_rag_app/build
cmake --build /tmp/qornix_ui_polish_rag_app/build -j2
./create_new_project.sh /tmp/qornix_ui_polish_with_rag --with-rag
cmake -S /tmp/qornix_ui_polish_with_rag -B /tmp/qornix_ui_polish_with_rag/build
cmake --build /tmp/qornix_ui_polish_with_rag/build -j2
authenticated generated rag_app smoke:
  unauthenticated /api/rag/health -> 401
  /auth/login -> csrf token returned
  authenticated /api/rag/health -> 200
  authenticated /api/rag/embedding/models -> success
  authenticated+admin-token /api/rag/qa/add -> success
  authenticated+admin-token /api/rag/admin/diagnostics -> 200
```

## 2026-05-27 - Advanced ingestion metadata completed

Status: `done`

Goal:
- close `0.1 Current explicit open work` item 4;
- promote the PDF/DOCX/XLSX/CSV/PPTX/image parser baselines from plain text extraction to a stable advanced metadata contract;
- keep optional parser dependencies graceful when `pdftotext`, `tesseract`, `libzip`, or `pugixml` are unavailable.

Implemented:
- added `advanced_ingestion_metadata_v1` metadata contract markers for rich parser outputs;
- expanded PDF ingestion with `pdftotext -layout`, extraction diagnostics, raw metadata hints, `pdfinfo -box` page-box metadata, optional attachment listing via `pdfdetach`, and optional outline probing via `mutool`;
- expanded DOCX ingestion with paragraph/heading/table counts, footnote/endnote/comment/style counts, document relationship count, and core/app document properties when `libzip` + `pugixml` are available;
- expanded CSV and XLSX ingestion with typed cell counters, formulas/formula coordinates, merged ranges, dimensions, styles, workbook defined names, table metadata, and `spreadsheet_rows_v2`;
- expanded PPTX ingestion with notes, comments, alt text, media relationships, slide layouts, presentation properties, and textless-slide diagnostics;
- expanded image OCR ingestion with image dimensions, EXIF-presence hints, TSV-based OCR confidence/region coordinates/word and line counters, fallback plain OCR, and `ocr_regions_v2`;
- fixed the HTML parser regexes to use portable C++ `std::regex` patterns instead of unsupported inline `(?is)` flags;
- updated ingestion pipeline tests for the new metadata contracts and parser diagnostics.

Validation:
```text
g++ -std=c++20 -I. -I qornix_rag qornix_rag/tests/test_ingestion_pipeline.cpp qornix_rag/ingestion_pipeline.cpp -o /tmp/test_ingestion_pipeline_noopt
/tmp/test_ingestion_pipeline_noopt
# IngestionPipeline tests passed

cmake -S . -B build
# blocked in this sandbox because yaml-cpp is not installed: yaml-cpp not found. Install with: apt-get install libyaml-cpp-dev
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
- Post-stabilization 1 - VectorStore and local HNSW save/load baseline: `done`
- Post-stabilization 2 - vector index metadata validation and stale rebuild baseline: `done`
- Post-stabilization 3 - background ingestion with progress polling baseline: `done`
- Post-stabilization 4 - incremental in-memory reindex baseline: `done`
- Post-stabilization 5 - reranking and query expansion baseline: `done`
- Post-stabilization 6 - evaluation datasets and retrieval-quality regression tests: `done`
- Post-stabilization 7 - admin UI and host-application auth/RBAC integration baseline: `done`
- Post-stabilization 8 - `qornix_auth` stabilization baseline before full RAG host auth integration: `done`
- Post-stabilization 9 - `qornix_orm`-backed durable auth store baseline: `done`
- Post-stabilization 10 - generated app auth wiring with `QornixOrmAuthStore`: `done`
- Post-stabilization 11 - route-level auth policies for generated RAG apps: `done`
- Post-stabilization 12 - generated-app login and auth admin user management: `done`
- Post-stabilization 13 - auth/RBAC current-user validation and route/cookie matching hardening: `done`
- Post-stabilization 14 - QA server-side pagination/filtering/suggestions baseline: `done`
- Post-stabilization 15 - embedding model registry and ONNX config diagnostics baseline: `done`
- Post-stabilization 16 - generated-app CSRF protection baseline: `done`
- Post-stabilization 17 - LLM response robustness and Boost.JSON parser baseline: `done`
- Post-stabilization 18 - PDF text ingestion adapter baseline: `done`
- Post-stabilization 19 - DOCX text ingestion adapter baseline: `done`
- Post-stabilization 20 - XLSX/CSV spreadsheet text ingestion adapter baseline: `done`
- Post-stabilization 21 - PPTX presentation text ingestion adapter baseline: `done`
- Post-stabilization 22 - image OCR ingestion adapter baseline: `done`
- Post-stabilization 23 - parser-to-chunker metadata and richer chunking baseline: `done`
- Post-stabilization 24 - RAG quality and grounding baseline: `done`
- Post-stabilization 25 - embedding/model operations for local use: `done`
- Post-stabilization 26 - QA/wiki knowledge quality baseline: `done`
- Post-stabilization 27 - production vector backend adapter baseline: `done`
- Post-stabilization 28 - generated-app hardening baseline: `done`
- Post-stabilization 29 - generated-app RAG UI polish: `done`
- Vector backend production hardening: `done`
- Advanced ingestion metadata: `done`
- Code and chunking depth: `done`
- Embedding/model registry hardening: `done`
- RAG quality beyond deterministic baseline: `done`
- QA/wiki deeper quality: `done`
- Next - Operations, deployment, and security depth: `pending`

## Deferred / known follow-ups

These items are intentionally not closed by the completed stabilization baselines through generated-app UI polish, vector backend production hardening, and advanced ingestion metadata. Post-stabilization 25-29 plus the current production-hardening entries completed the first embedding/model operations, QA/wiki quality, production vector backend, generated-app hardening/UI, and parser-metadata baselines; the items below are deeper follow-ups beyond those baselines:

- arbitrary document ingestion beyond current text/Markdown/code/HTML/PDF-text/DOCX-text/CSV/XLSX-text/PPTX-text/image-OCR/QA flows;
- deeper document understanding beyond current metadata-aware chunking, such as semantic table extraction, layout-aware OCR ordering, and full compiler-grade code AST extraction;
- production security hardening beyond the generated-app hardening baseline, such as durable audit storage, delivered account recovery, MFA, tracing, alerting, and secret-management docs;
- retrieval relevance and query normalization;
- ORM-backed QA list abstractions beyond the current SQLite FTS/tag/history baseline;
- model selection UI, automatic model discovery, deeper tokenizer compatibility validation, and additional LLM diagnostics polish;
- remaining LLM parser follow-ups such as model-list/token-usage parsing through `Boost.JSON`, real-provider response fixtures, and unified structured parsing for streaming callbacks.

## 2026-05-27 - Code and chunking depth completed

Scope:

- close `0.1 Current explicit open work` item 5;
- preserve advanced ingestion metadata through chunking, persisted chunks, retrieval, API responses, and citation labels;
- add metadata-aware retrieval filters for `type`, `language`, `source_path`, `page`, `sheet`, `row`, `slide`, `symbol`, `ocr_confidence`, and raw metadata keys;
- expose `filters_applied`, normalized `filters`, `source_locator`, `citation_label`, and curated structural `metadata` in `/api/search` and `/api/ask`;
- extend chunking to DOCX heading sections and PPTX slide sections in addition to existing Markdown, code symbol, PDF page, spreadsheet row, and OCR region chunking;
- improve source-code symbol metadata with namespace/class/struct/impl scope tracking;
- index metadata text into Xapian so parser/chunk metadata participates in hybrid retrieval before exact filter checks;
- document the metadata filter API and update chunk quality coverage.

Validation:

- `test_document_chunker` passes with DOCX heading, PPTX slide, symbol scope, PDF page, spreadsheet row, OCR region, and tokenizer-budget coverage;
- `rag_service.cpp` and `web.cpp` pass C++20 syntax checks in the sandbox with local Xapian stubs because the sandbox lacks libxapian headers.

## 2026-05-27 - Embedding/model registry hardening completed

Scope:

- close `0.1 Current explicit open work` item 6;
- add local ONNX model auto-discovery from `embedding.models_dir` for directories containing `.onnx` plus `tokenizer.json`;
- validate model/tokenizer files at config load and surface tokenizer compatibility diagnostics through the embedding model API;
- support Hugging Face tokenizer JSON vocab layouts for WordPiece, BPE, WordLevel, and Unigram-style arrays, including `added_tokens` and common special-token names;
- apply the active embedding tokenizer budget to document chunking and persist `embedding_token_limit`, `tokenizer_type`, `embedding_estimated_tokens`, and `embedding_model_signature` in chunk metadata;
- strengthen embedding model/cache signatures so model, tokenizer, dimension, pooling, sequence length, casing, and normalization changes invalidate stale vector reuse and saved vector snapshots;
- extend `/api/embedding/models` responses with `tokenizer_vocab_size`, `effective_chunk_token_limit`, `files_present`, `discovered`, `persistent_cache_enabled`, `tokenizer_status`, `model_status`, and `model_signature`;
- store embedding model signature metadata in SQLite `rag_embedding_models` rows during persisted index snapshots;
- document auto-discovery, tokenizer compatibility, and cache/rebuild behavior.

Validation:

- `rag_config.cpp`, `rag_service.cpp`, `sqlite_source.cpp`, and `web.cpp` pass C++20 syntax checks in the sandbox with local Xapian stubs;
- `test_embedding_config.cpp` was expanded with parser/config assertions and a local auto-discovery fixture;
- full linked test execution was not run in the sandbox because this environment does not provide the same Boost.JSON/Xapian system libraries as the target development machine.

## 2026-05-27 - RAG quality beyond deterministic baseline completed

Scope:

- close `0.1 Current explicit open work` item 7;
- add bounded multi-query retrieval over original, expanded, rewritten, and compact keyword variants;
- expose `rewritten_query`, `multi_query_applied`, `retrieval_strategy`, `retrieval_queries`, and `reranker_type` in `/api/search` and non-streaming `/api/ask`;
- add an embedding-model semantic reranker layer that deduplicates and ranks multi-query candidates while preserving metadata-aware filters and citations;
- add claim/citation grounding diagnostics for Ask responses with `grounding_evaluator`, `claim_grounding_status`, and `grounded_claims`;
- add `/api/feedback` and analytics feedback capture for helpfulness, missing context, bad citations, and wrong-answer workflows;
- extend analytics reports/exports with feedback totals and category counts;
- expand the retrieval-quality fixture with long-document and generated-template-app cases;
- document quality diagnostics and feedback capture in the API docs.

Validation:

- `test_analytics` passes with feedback capture/report/export coverage;
- `rag_service.cpp`, `web.cpp`, `test_rag_service.cpp`, and `test_rag_quality_eval.cpp` pass C++20 syntax checks in the sandbox with local Xapian stubs because the sandbox lacks libxapian headers.


## 2026-05-27 - QA/wiki deeper quality completed

Scope:

- close `0.1 Current explicit open work` item 8;
- add `/api/qa/duplicate-check` for bounded duplicate preview before add/edit saves;
- wire duplicate warnings into the standalone QA UI with a confirm-before-save flow;
- harden richer QA Markdown rendering through a safe server-side subset: escaped raw HTML, safe inline code/strong text, fenced code, lists/headings, and allowlisted links only;
- add optional SQLite FTS5-backed QA search with bounded `LIKE` fallback when FTS5 is unavailable;
- add normalized `qa_tags` storage plus indexed tag listing/filtering/autocomplete;
- add append-only `qa_pair_history` provenance with create/update/delete entries and expose it through `/api/qa/history`;
- document duplicate preview, sanitized Markdown rendering, FTS/tag indexing, and QA history APIs.

Validation:

- `sqlite_source.cpp`, `rag_service.cpp`, `web.cpp`, `test_sqlite_source.cpp`, and `test_rag_service.cpp` pass C++20 syntax checks in the sandbox with local Xapian stubs;
- `test_sqlite_source.cpp` now includes QA auxiliary quality coverage for tag indexing, QA search, and version history;
- full linked SQLite/Boost.JSON execution was not run in the sandbox because this environment does not provide the same Boost.JSON/Xapian system libraries as the target development machine.
## 2026-05-27 - Remaining LLM/provider diagnostics completed

Scope:

- close `0.1 Current explicit open work` item 10;
- move provider model-list parsing from string scanning to structured `Boost.JSON` parsing;
- support real model-list shapes for Ollama `/api/tags`, OpenAI-compatible `/v1/models`, vLLM, LM Studio, flat arrays, and single-model objects;
- move token-usage parsing from `find`/`stoul` offsets to structured `Boost.JSON` parsing for OpenAI-compatible `usage` objects and Ollama final response metrics;
- parse token usage from SSE/NDJSON final chunks with the same structured parser path;
- unify streaming SSE/NDJSON content extraction with the same structured payload parser used by non-streaming responses;
- fix streaming request JSON generation so `stream: true` is emitted inside the request object;
- add provider fixtures for Ollama, OpenAI-compatible APIs, vLLM, and LM Studio.

Validation:

- `llm_client.cpp`, `rag_service.cpp`, `web.cpp`, and `test_llm_client.cpp` pass C++20 syntax checks in the sandbox with local Xapian stubs;
- linked `test_llm_client` execution was not run in the sandbox because this environment does not provide the Boost.JSON runtime library used by the target development environment.

## 2026-05-28 - Xapian language-aware retrieval hardening completed

Scope:

- close added item 11 after the explicit 0.1 work list was completed;
- add config-driven Xapian controls for lexical search enablement, language, stemming, stemming strategy, CJK ngrams, word-break mode, spelling flag, and metadata prefixes;
- replace the hardcoded English-only Xapian stemmer path with shared index/query configuration;
- add config-driven Xapian language selection with a lightweight Cyrillic/default auto heuristic plus explicit Xapian language names, ISO 639 aliases, and `none`;
- add fielded Xapian prefixes for path, source, type, language, metadata, symbol, page, sheet, and slide lookup;
- keep legacy exact metadata terms while adding parser prefixes, preserving older metadata-aware retrieval behavior;
- surface Xapian diagnostics through `/api/health`, `/api/stats`, and `/api/admin/diagnostics`;
- document standalone and generated-app Xapian language-aware configuration.

Validation:

- `core.h`, `rag_config.cpp`, `rag_service.cpp`, `web.cpp`, and `test_embedding_config.cpp` pass C++20 syntax checks in the sandbox with local Xapian stubs because the sandbox lacks libxapian headers;
- `test_embedding_config.cpp` was expanded with Xapian config parsing and invalid-option warning coverage;
- linked Xapian execution was not run in the sandbox because this environment does not provide the target system libxapian package.

## 2026-05-28 - Redis LLM response cache backend completed

Scope:

- complete the Phase 3 roadmap promise that LLM response caching supports both memory and Redis backends;
- replace the previous Redis placeholder with a lightweight Redis RESP TCP client without adding a hiredis dependency;
- support Redis `SELECT`, `PING`, optional `AUTH`, `GET`, `SETEX`, `DEL`, and prefix-scoped `SCAN` clear;
- parse `cache.redis.*` and `cache.key_prefix` from standalone and generated-app config;
- expose cache backend/availability in diagnostics;
- update config examples, generated app config, dependency docs, and RAG product guide;
- keep safe startup behavior: if Redis is configured but unavailable, the cache factory logs a warning and falls back to the in-process memory cache.

Validation:

- `llm_cache.cpp` passes C++20 syntax check;
- `test_phase3` passes with memory cache, rate limiter, cache factory, and Redis-unavailable fallback coverage;
- `depend_install.sh` passes `bash -n`.
