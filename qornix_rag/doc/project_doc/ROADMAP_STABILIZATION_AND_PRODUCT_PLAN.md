# Qornix RAG stabilization and product roadmap

Status: planning document
Scope: `qornix_rag` standalone, reusable RAG core, and `qornix_web` template integration
Primary goal: finish the current RAG/LLM stage before expanding into a full production-grade arbitrary-document RAG system.

## 0. Preparation baseline

Before Milestone A, keep project planning documents under `qornix_rag/doc/project_doc/` and keep `qornix_rag/README.md` user-facing.

The preparation baseline and Milestone A execution checklist are recorded in:

```text
qornix_rag/doc/project_doc/MILESTONE_0_PREP.md
```

Current status:

```text
Milestone 0: done
Milestone A1: done
Milestone A2: done
Milestone A3: done
Milestone A4: done
Milestone A5: done
Milestone A6: done
Milestone A7: done
Milestone A8: done
Milestone B: done
Milestone C: done
Milestone D: done
Milestone E: done
Post-stabilization 1: done
Post-stabilization 2: done
Post-stabilization 3: done
Post-stabilization 4: done
Post-stabilization 5: done
Post-stabilization 6: done
Post-stabilization 7: done
Post-stabilization 8: done
Post-stabilization 9: done
Post-stabilization 10: done
Post-stabilization 11: done
Post-stabilization 12: done
Post-stabilization 13: done
Post-stabilization 14: done
Post-stabilization 15: done
Post-stabilization 16: done
Next: select the next backlog item before implementation
```

## 1. Product split

The project should be planned as two related but different product lines.

### 1.1 Standalone `qornix_rag`

Standalone `qornix_rag` is a local single-user application.

Target use cases:

- run locally from the repository with `qornix_rag/run.sh`;
- build a portable folder with binaries and runtime assets;
- index a local project or selected local folders;
- add and manage QA pairs;
- use the system as a local wiki / knowledge base;
- ask questions in the web UI and receive full LLM-generated answers based on local context.

Authentication and authorization are intentionally out of scope for standalone mode. The default deployment model is a local process bound to `127.0.0.1` and used by one user on their own machine.

Standalone mode still needs basic local safety:

- default bind address should be `127.0.0.1`;
- explicit warning when binding to `0.0.0.0`;
- allowlist or explicit confirmation for indexed paths;
- file size limits for indexing and upload flows;
- safe defaults that prevent accidentally indexing `/`, home directories, caches, secrets, or build artifacts;
- no bundled user data, indexes, secrets, or API keys in release packages.

### 1.2 Full web application through `qornix_web/templates`

The full application version belongs in `qornix_web/templates` and must be generated through `create_new_project.sh`.

It should support two modes:

1. Create a dedicated RAG web application.
2. Enable RAG as an option for existing templates.

Example target commands:

```bash
./create_new_project.sh my_rag_app --template rag_app
./create_new_project.sh my_app --template web_app --with-rag
```

Interactive generation should also be supported later:

```text
Select template:
1. default_app
2. dynamic_api_app
3. rag_app

Enable RAG module? [y/N]
Enable LLM answers? [y/N]
Enable document upload? [y/N]
```

Authentication, authorization, multi-user concerns, RBAC, tenant separation, and deployment security belong to this full web application line, not to the local standalone baseline.

## 2. Current stabilization target

The current stage is not complete until standalone mode provides a usable RAG/LLM web UI.

After stabilization, this flow must work:

1. User starts the local service with `qornix_rag/run.sh`.
2. User opens the standalone web UI.
3. User indexes a local project or configured source.
4. User optionally adds QA pairs.
5. User asks a question in the UI.
6. Backend retrieves relevant context from supported sources.
7. Backend builds a prompt and calls the configured LLM.
8. UI displays a full LLM-generated answer with sources and useful metadata.

This is the minimum product expectation. If the UI still only exposes legacy code search after stabilization, the stabilization stage is not done.

## 3. Non-goals for the standalone stabilization stage

The following items are intentionally deferred until after the local standalone RAG/LLM experience is stable:

- authentication and authorization in standalone mode;
- arbitrary document ingestion for all file types;
- PDF, DOCX, XLSX, PPTX, image, and OCR support;
- persistent production vector store;
- multi-model embedding registry;
- automatic embedding model download;
- multi-user workspaces;
- cloud deployment hardening;
- advanced reranking and evaluation pipelines.

## 4. Milestone A: standalone stabilization

Goal: make `qornix_rag` a useful local wiki/RAG application before expanding the architecture into templates and production RAG.

### A1. Standalone startup/config baseline

Status: `done`

Scope:

- make `qornix_rag/run.sh` reliable from the repository;
- normalize local-first defaults;
- make startup diagnostics explicit;
- make the template path and runtime paths unambiguous.

Completed outcome:

- `run.sh --help` exits before build/startup;
- `run.sh` resolves repository/build/config/template paths relative to the script;
- default bind address is local-only: `127.0.0.1`;
- default standalone port is `8081`, with CLI override such as `--port 8082`;
- canonical config key is `server.address`, with legacy `server.host` compatibility;
- `QORNIX_RAG_HOME` and `QORNIX_RAG_TEMPLATES_DIR` are set for standalone runtime;
- the standalone web UI is loaded from `qornix_rag/templates/rag_interface.html`;
- missing libcurl is reported as search-only mode instead of a false LLM-ready state;
- CURL-enabled builds compile;
- startup logs show binary path, config path, templates path, bind address, LLM model/API, embedding backend, and health routes.

### A2. LLM Ask workflow

Status: `done`

Scope:

- make LLM-generated Ask the primary standalone workflow;
- expose a browser flow that calls the LLM, not only search;
- display answer status, timing, and sources.

Current API used by the standalone UI:

```http
POST /api/ask
```

Expected request shape:

```json
{
  "question": "How does the RAG extension configure the LLM client?",
  "top_k": 5
}
```

Expected response shape:

```json
{
  "success": true,
  "answer": "...LLM-generated answer...",
  "sources": [
    {
      "path": "...",
      "snippet": "...",
      "score": 0.91,
      "source": "..."
    }
  ],
  "llm_status": "ok",
  "response_time_ms": 1234
}
```

Completed outcome:

- Ask tab sends requests to `/api/ask`;
- UI displays full generated answers;
- UI displays source/context snippets;
- UI displays LLM status, source count, and response time;
- graceful fallback is shown if the LLM is unavailable;
- Ollama model discovery validates that the configured model exists;
- user-facing diagnostics explain configured model, available models, and suggested config changes.

### A3. RAG/wiki web UI

Status: `done`

Scope:

- replace old code-search-only UI with a local RAG/wiki interface;
- keep Search as a secondary tool;
- expose health and sources in a user-readable way.

Completed UI sections:

- Ask;
- Search;
- QA;
- Sources / Health.

Completed outcome:

- old "search by code" page has been replaced by a RAG/wiki UI;
- Ask is the primary tab;
- Search remains available through `/api/search`;
- Sources / Health shows indexed project status, source list, LLM status, and reindex action;
- LLM diagnostics are shown on the main page;
- LLM diagnostic banners can be dismissed;
- generated answers are rendered with basic Markdown-like formatting;
- health details remain available for technical debugging.

### A4. QA/wiki workflow

Status: `done`

Scope:

- allow users to maintain a local QA/wiki knowledge base;
- make QA entries immediately usable in Ask/Search;
- provide a usable UI for add/list/edit/delete workflows.

Completed outcome:

- QA pairs can be added from the UI;
- QA pairs can be listed;
- QA pairs can be edited;
- QA pairs can be deleted;
- QA pairs participate in Ask context;
- QA pairs participate in Search results;
- QA results are shown as QA/source entries;
- QA table supports local filtering and category filtering;
- QA table supports client-side pagination;
- known scalability limits of client-side QA pagination are documented in the backlog.

### A5. Configuration, routing, and integration boundary hardening

Status: `done`

This milestone preserves important stabilization items that were present in the earlier roadmap but were not completed by A1-A4.

Goal: make the current standalone implementation and future `qornix_web` integration unambiguous and safe.

#### A5.1 RAG configuration contract and mode-specific adapters

Known risks:

- standalone config, integrated `qornix_web` config, and third-party application config can map to different runtime behavior if the RAG boundary is implicit;
- it may be unclear which config source is used by standalone startup;
- `RagExtension::configure()` may expect keys with `rag.` prefixes while integrated setup passes stripped keys;
- parsed LLM settings must be the settings actually used by `LLMClient`;
- standalone `qornix_rag/config.yaml` must not become a required config file for integrated or third-party use;
- host application config must not be treated as the standalone RAG config.

Required outcome:

- one documented RAG runtime configuration contract, for example `RagConfig`;
- standalone mode maps `qornix_rag/config.yaml` and standalone environment overrides into the RAG config contract;
- integrated `qornix_web` mode maps the host application's config into the RAG config contract;
- third-party applications can construct the RAG config contract programmatically without depending on standalone config files;
- each mode owns validation and diagnostics for its own config source;
- startup log must print the current mode and effective config source, such as standalone config path, host application config, or programmatic config;
- startup log must print effective LLM provider/API/model;
- startup log must print effective server address/port where applicable;
- invalid, unsupported, or ignored config keys must produce visible diagnostics in the adapter that owns that config source;
- `RagExtension` and runtime services must apply the parsed RAG config contract to the actual runtime services;
- config handling should not rely on hidden defaults when a user explicitly configured a value.

#### A5.2 Route ownership and prefixing

Standalone may keep the RAG UI at `/`.

Integrated mode must not claim `/` by default. It should register RAG under a prefix, for example:

```text
/rag
/api/rag/*
```

Required outcome:

- standalone root UI works;
- integrated `qornix_web` mode does not override the host application's home page;
- route prefix is configurable;
- default integrated prefix is safe;
- route lists/logs clearly show which mode is active and which prefix is used.

#### A5.3 Dynamic extension lifetime

Known issue to verify and fix: if `ExtensionLoader` is created as a local variable during route setup, the shared library can be unloaded while registered handlers still reference extension code.

Required outcome:

- extension loader lifetime is tied to server/application lifetime;
- registered routes remain valid after setup;
- add a regression test or smoke test for dynamic extension loading.

#### A5.4 Standalone vs integrated diagnostics

Required outcome:

- startup log explicitly says whether the process is running as standalone or integrated extension;
- standalone local-only assumptions are not accidentally applied to generated full web apps;
- integrated mode reports its route prefix and config source.

### A6. Build separation and reusable-target preparation

Status: `done`

Goal: clarify CMake target ownership and prepare for Milestone B reusable core work.

`qornix_rag` should separate:

- reusable RAG core;
- HTTP/API adapter;
- standalone executable;
- dynamic extension integration;
- UI templates/static assets.

Required outcome:

- standalone build remains simple;
- `qornix_web` integration does not require duplicated or conflicting HTTP server code;
- CMake targets have clear ownership;
- tests can target core/service logic without requiring the standalone executable;
- current direct coupling to shared server code is documented and either justified or moved behind a cleaner boundary.

Suggested target direction:

```text
qornix_rag_core
qornix_rag_http
qornix_rag
qornix_rag_extension
```

### A7. Portable standalone release bundle

Status: `done`

Goal: allow a user to build a portable folder and run `qornix_rag` on another compatible machine without copying the full source tree.

This is separate from `run.sh`.

- `run.sh` is for development and source-tree execution.
- `build_portable.sh` or `package.sh --portable` is for creating a user-facing release folder.

#### A7.1 Add portable build script

Preferred script:

```bash
./qornix_rag/build_portable.sh
```

Alternative unified command:

```bash
./qornix_rag/package.sh --portable
```

The script should:

- configure CMake in release mode;
- build the standalone binary and required RAG targets;
- collect runtime libraries when needed;
- copy templates and static assets;
- copy example config files;
- create empty runtime directories;
- generate a portable `run.sh` inside the bundle;
- optionally run a smoke test;
- optionally produce a `.tar.gz` archive.

#### A7.2 Target bundle layout

Suggested layout:

```text
dist/qornix_rag-portable-linux-x86_64/
  bin/
    qornix_rag
  lib/
    *.so
  templates/
    rag_interface.html
  static/
  config/
    config.example.yaml
    config.yaml
  data/
  models/
  logs/
  cache/
  run.sh
  README.md
```

The bundle must use paths relative to its own root. A portable run script should resolve its location and set `QORNIX_RAG_HOME`.

Example internal launcher behavior:

```bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export QORNIX_RAG_HOME="$SCRIPT_DIR"
exec "$SCRIPT_DIR/bin/qornix_rag" \
  --config "$SCRIPT_DIR/config/config.yaml" \
  --address 127.0.0.1 \
  --port 8081 \
  "$@"
```

#### A7.3 Portable bundle constraints

The portable bundle is not a universal binary for every operating system.

Initial support target:

```text
Linux x86_64 -> compatible Linux x86_64 with compatible libc/runtime libraries
```

Later targets may add:

- macOS arm64/x86_64 bundles;
- Windows zip bundles;
- AppImage or similar packaging;
- Docker image.

#### A7.4 Portable acceptance criteria

Portable packaging is done when:

- bundle can be built with one command;
- bundle runs outside the source tree;
- bundle does not require build directories;
- bundle does not include user data or secrets;
- `./run.sh` inside the bundle starts the service;
- UI opens from the bundled templates/static assets;
- `/api/health` or equivalent endpoint succeeds;
- basic Ask/Search smoke test is documented.

### A8. User documentation refresh

Status: `done`

Goal: make standalone usage understandable for ordinary local users and future contributors.

Required user-facing docs:

- `qornix_rag/README.md` updated for local RAG/LLM usage;
- `qornix_rag/doc/CONFIG.md`;
- `qornix_rag/doc/API.md`;
- `qornix_rag/doc/STANDALONE.md`;
- `qornix_rag/doc/INTEGRATION_QORNIX_WEB.md`;
- migration notes from the old code-search UI to the RAG UI.

Documentation should explain:

- how to run standalone from source;
- how to select a port;
- how to configure Ollama/local LLM;
- how to diagnose missing models;
- how to index a project;
- how to add/edit/delete QA pairs;
- how QA affects Ask/Search;
- how to build and run the portable bundle after A7;
- what is intentionally not supported yet.

### Milestone A acceptance criteria

Standalone stabilization is done when all of these are true:

- `qornix_rag/run.sh` builds and starts the local service from a clean checkout;
- default bind is local-only or clearly documented and safe;
- web UI contains Ask / Chat and not only search;
- user receives LLM-generated answers in the browser;
- UI shows sources for the answer;
- QA pairs can be added, listed, edited, and deleted;
- indexing supported local sources works from the UI or documented API;
- health endpoint reports LLM, SQLite/source, and index status;
- missing LLM backend fails gracefully;
- configuration behavior is documented and consistent;
- standalone and integrated route ownership is documented and safe;
- portable bundle can be produced and run outside the source tree;
- documentation explains the local workflow end-to-end.

## 5. Milestone B: reusable RAG core

Status: `done`

Goal: make `qornix_rag` usable as a module by both standalone and `qornix_web` projects.

Required work:

- separate RAG engine from standalone server startup;
- define stable service interfaces for search, ask, QA, sources, indexing, and health;
- define route registration API with configurable prefix;
- use one RAG runtime config contract with mode-specific config adapters;
- remove duplicated HTTP/server ownership where possible;
- provide tests for standalone and extension wiring.

Expected result:

> `qornix_rag` becomes a reusable engine/module. Standalone is only one host for it.

Completed outcome:

- added `RagService` as the reusable non-HTTP service layer in `qornix_rag_core`;
- exposed stable service methods for indexing, search, ask, health, sources, and QA pair CRUD;
- moved non-streaming index/search/ask route behavior onto `RagService`;
- kept HTTP route registration separate from standalone startup and available with configurable route options;
- preserved standalone and extension hosts as separate adapters over the shared RAG core;
- added `test_rag_service` coverage for indexing, search, ask fallback, health, and source listing;
- verified standalone, web host, route extension, and existing RAG tests after the service extraction.

## 6. Milestone C: dedicated `qornix_web/templates/rag_app`

Status: `done`

Goal: generate a full RAG web application through `create_new_project.sh`.

Suggested template path:

```text
qornix_web/templates/rag_app/
```

The template should include:

- application entry point;
- RAG routes under `/rag` and `/api/rag/*`;
- web UI templates;
- static assets;
- config example;
- database/init files if needed;
- README for generated projects;
- optional Ollama/local LLM example;
- optional Docker Compose example.

Suggested generated endpoints:

```text
GET  /rag
GET  /api/rag/health
POST /api/rag/ask
POST /api/rag/search
GET  /api/rag/sources
POST /api/rag/index
GET  /api/rag/qa
POST /api/rag/qa
PUT  /api/rag/qa/{id}
DEL  /api/rag/qa/{id}
```

Acceptance criteria:

- `create_new_project.sh my_rag_app --template rag_app` creates a buildable project;
- generated project has working RAG UI;
- generated project can call the configured LLM;
- generated project documents config, startup, and local LLM setup;
- generated project does not depend on the standalone-only launcher.

Completed outcome:

- added the dedicated `templates/rag_app` generator template;
- `create_new_project.sh` accepts `--template rag_app`, `--template rag-app`, and `--template rag`;
- generated projects link `qornix::web_core` and `qornix::rag_extension`;
- generated projects register the RAG UI at `/rag` and RAG API under `/api/rag/*`;
- generated projects use their own `config.yaml` and do not use `qornix_rag/run.sh`;
- generated projects include RAG UI templates, static assets, docs, `knowledge_base/`, `data/`, and deploy bundle rules;
- generated projects include local Ollama configuration examples and optional Docker Compose support;
- generated projects resolve runtime paths from the generated application root;
- generated projects support REST QA aliases: `GET/POST /api/rag/qa` and `PUT/DELETE /api/rag/qa/{id}`;
- existing QA routes remain available: `/api/rag/qa/list`, `/api/rag/qa/add`, `/api/rag/qa/update`, and `/api/rag/qa/delete`.

Verified:

```bash
./create_new_project.sh "$tmpdir/my_rag_app" --template rag_app
cmake -S "$tmpdir/my_rag_app" -B "$tmpdir/my_rag_app/build"
cmake --build "$tmpdir/my_rag_app/build" -j2
cmake --build build --target qornix_rag qornix_web qornix_rag_route_extension -j2
curl -fsS http://127.0.0.1:8018/api/rag/health
curl -fsS http://127.0.0.1:8018/api/rag/qa
curl -fsS -X POST http://127.0.0.1:8018/api/rag/qa ...
curl -fsS -X PUT http://127.0.0.1:8018/api/rag/qa/{id} ...
curl -fsS -X DELETE http://127.0.0.1:8018/api/rag/qa/{id}
curl -fsS -o /tmp/qornix_rag_app_smoke.html http://127.0.0.1:8018/rag
```

## 7. Milestone D: RAG option for existing templates

Status: `done`

Goal: add RAG to other `qornix_web` templates as an optional feature.

Target command:

```bash
./create_new_project.sh my_app --template web_app --with-rag
```

Generator responsibilities:

- add RAG config section;
- register `/rag` and `/api/rag/*` routes;
- copy or reference RAG templates/static assets;
- add required CMake targets/dependencies;
- initialize local storage if needed;
- document how to enable/disable RAG in the generated app.

Acceptance criteria:

- at least one existing template can be generated with `--with-rag`;
- the host app home page remains intact;
- RAG UI is available under `/rag`;
- Ask/Search endpoints work;
- feature can be disabled through config or build option.

Completed outcome:

- `create_new_project.sh` accepts `--with-rag` for the default `templates/app` application template;
- generated default apps keep their normal host routes, including `/`, `/docs`, `/health`, async examples, and project structure routes;
- generated default apps with RAG link `qornix::web_core` and `qornix::rag_extension`;
- generated default apps with RAG configure the reusable RAG module from their own `config.yaml`;
- RAG UI is mounted at `/rag`;
- RAG API is mounted under `/api/rag/*`;
- generated apps copy RAG UI, static CSS, docs, `knowledge_base/`, `models/`, `data/`, and `download_onnx_model.sh`;
- deploy bundles include the RAG runtime assets when `--with-rag` is used;
- generated apps expose a CMake option such as `-D<PROJECT>_ENABLE_RAG=OFF` to disable embedded RAG routes at build time;
- plain default app generation without `--with-rag` remains buildable.

Verified:

```bash
./create_new_project.sh /tmp/qornix_app_plain_d
./create_new_project.sh /tmp/qornix_app_with_rag_d --with-rag
cmake -S /tmp/qornix_app_plain_d -B /tmp/qornix_app_plain_d/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_plain_d/build -j2
cmake -S /tmp/qornix_app_with_rag_d -B /tmp/qornix_app_with_rag_d/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qornix_app_with_rag_d/build -j2
cmake -S /tmp/qornix_app_with_rag_d -B /tmp/qornix_app_with_rag_off_d/build -DCMAKE_BUILD_TYPE=Release -DQORNIX_APP_WITH_RAG_D_ENABLE_RAG=OFF
cmake --build /tmp/qornix_app_with_rag_off_d/build -j2
curl -fsS http://127.0.0.1:8008/
curl -fsS http://127.0.0.1:8008/docs
curl -fsS http://127.0.0.1:8008/rag
curl -fsS http://127.0.0.1:8008/api/rag/health
curl -fsS -X POST http://127.0.0.1:8008/api/rag/index -H 'Content-Type: application/json' -d '{}'
curl -fsS -X POST http://127.0.0.1:8008/api/rag/search -H 'Content-Type: application/json' -d '{"query":"knowledge base","top_k":3}'
curl -fsS -X POST http://127.0.0.1:8008/api/rag/ask -H 'Content-Type: application/json' -d '{"question":"Say OK in one short sentence.","top_k":1}'
```

## 8. Milestone E: production RAG expansion

Status: `done` for the E1-E6 production RAG baseline.

This milestone starts only after standalone stabilization, reusable core separation, and template integration are clear.

Completed baseline:

- E1 persistent document/chunk/embedding storage through SQLite-backed metadata;
- E2 text/Markdown/code/HTML ingestion pipeline with durable job records and delete flow;
- E3 chunk-level retrieval and persistence for text, Markdown, and code;
- E4 embedding model metadata, ONNX output handling, and stable embedding model ids;
- E5 citation, confidence, and grounding metadata in Ask/Search responses;
- E6 diagnostics, metrics, generated operations docs, and deployment guidance.

The remaining work after E6 is tracked as deferred follow-ups in section 11 and should be planned as post-stabilization work, not as unfinished stabilization.

### E1. Persistent knowledge and vector storage

Status: `done` for the first SQLite-backed persistence baseline.

Current limitation: SQLite source is QA-pair oriented and does not provide a production vector store. HNSW-style index is in-memory and lost on restart.

Needed:

- document table;
- chunk table;
- embedding metadata table;
- vector store abstraction;
- HNSW save/load or replaceable backend;
- rebuild/reindex strategy;
- migrations;
- backup/export/import;
- clarify how `qornix_orm` should be used for metadata/QA/document records where appropriate;
- keep vector storage behind a dedicated vector-store abstraction.

Planned vector backend options, tracked in backlog section 11.6:

- local SQLite-backed metadata + persisted HNSW files;
- Faiss;
- Qdrant;
- pgvector;
- other adapters behind one interface.

Completed baseline:

- added `PersistentIndexStore` as the persistence boundary for indexed documents, chunks, and embedding vectors;
- extended `SQLiteSource` as the first `PersistentIndexStore` implementation while keeping existing QA-pair APIs intact;
- added `rag_documents` for indexed document metadata;
- added `rag_chunks` for persisted document chunks;
- added `rag_embeddings` for persisted vector blobs;
- added `rag_embedding_models` for minimal embedding model metadata;
- added idempotent replacement of a source's persisted index snapshot on reindex;
- persisted one chunk per indexed document as the baseline before E3 chunking strategies;
- persisted embeddings as binary float blobs with backend/model/dimension metadata;
- exposed counts and document lookup helpers for persisted documents, chunks, and embeddings;
- `RagService::indexProject()` now persists the current indexed document snapshot when SQLite is configured;
- added focused tests for SQLite persistence and service-level persistence.

Remaining E1 follow-ups:

- HNSW save/load or a replaceable vector backend that can serve retrieval directly from persisted vectors; see backlog 11.6;
- backup/export/import commands;
- schema migration versioning;
- deeper `qornix_orm` integration for metadata records where appropriate.

### E2. Document ingestion pipeline

Status: `done` for the first text/Markdown/code/HTML ingestion pipeline and durable job baseline.

Needed:

- source registry;
- MIME/type detection;
- parser interface;
- ingestion jobs;
- progress and error reporting;
- reindex and delete flows;
- incremental indexing;
- duplicate detection;
- content hashing.

Document types still to add after the text/Markdown/code/HTML baseline:

- PDF;
- DOCX;
- XLSX/CSV;
- PPTX;
- images with OCR;
- source code repositories with structure-aware chunking.

Completed baseline:

- added `IngestionPipeline` as the reusable filesystem ingestion job layer;
- added extension-based MIME/type/language detection for supported text, Markdown, config, and source-code files;
- added ingestion job results with file counts, imported document counts, skipped counts, duplicate counts, errors, and structured issues;
- added text parser baseline for currently supported plain-text-like files;
- added binary-content detection and skip reporting;
- added file size enforcement and skip reporting;
- added duplicate detection by content hash within an ingestion job;
- added source-root scanning with recursive and non-recursive modes;
- added directory exclusion handling for build/cache/model directories;
- added per-document metadata such as `mime_type`, `ingestion_parser`, and `source_extension`;
- added `DocumentParser` as the parser plugin interface;
- added parser registry in `IngestionPipeline`;
- added plain-text parser for supported text-like files;
- added HTML parser with tag/script/style stripping, title extraction, entity decoding, and metadata capture;
- moved `RagEngine::index_project()` onto the ingestion pipeline so `/api/rag/index` uses the E2 path;
- moved `FileSource` onto the ingestion pipeline so standalone source indexing and reusable source indexing share the same detection/parser behavior;
- added durable SQLite ingestion job records in `rag_ingestion_jobs`;
- added `RagService::ingestProject()` as a job-oriented indexing entrypoint;
- added `/api/rag/ingest`, `/api/rag/ingest/jobs`, and `/api/rag/ingest/{id}` for ingestion job execution/history/status;
- added background ingestion through `POST /api/rag/ingest` with `async: true` or `background: true`;
- added in-memory running job status and progress polling while background ingestion is active;
- added incremental in-memory reindexing that reuses unchanged chunk embeddings by embedding model id, chunk path, and content hash;
- exposed incremental counters for indexed chunks, reused embeddings, generated embeddings, and stale embeddings;
- added persisted document delete flow through `SQLiteSource::deletePersistedDocument()`, `RagService::deletePersistedDocument()`, and `/api/rag/documents/delete`;
- added service and SQLite tests for durable ingestion jobs and persisted document delete;
- added focused ingestion pipeline tests and kept existing data source, service, and SQLite persistence tests passing.

Remaining E2 follow-ups:

- live progress streaming instead of polling-only background ingestion;
- full incremental runtime indexing by modified time and content hash instead of rebuilding the in-memory index;
- parser plugins for PDF, DOCX, XLSX/CSV, PPTX, and images/OCR; see backlog 11.6.

### E3. Chunking strategies

Status: `done` for the first text/Markdown/code chunking baseline.

Needed strategies:

- plain text token-aware chunking;
- markdown heading-aware chunking;
- code symbol/function-aware chunking;
- page-aware PDF chunking;
- table-aware spreadsheet chunking;
- overlap policy;
- metadata preservation.

Completed baseline:

- added `DocumentChunker` as the reusable chunking boundary;
- added plain text token-window chunking with overlap;
- added Markdown heading-aware section chunking before token-window splitting;
- added code symbol/function-aware section chunking before token-window splitting;
- indexed chunk-level documents in `RagEngine` so HNSW/Xapian retrieval targets chunks instead of whole files;
- preserved source document metadata on chunks through `chunk_of`, `chunk_index`, `chunk_count`, `chunk_strategy`, character offsets, token count, heading, and symbol metadata;
- updated context building so answers cite the source file and chunk index instead of only the synthetic chunk path;
- updated SQLite persistence so one source document can own multiple persisted chunks and chunk embeddings;
- added regression coverage for chunking strategies and chunked SQLite persistence.

Remaining E3 follow-ups:

- use tokenizer-specific token counts for ONNX models instead of whitespace-token estimates;
- add richer language-specific code parsers for symbols/classes/functions beyond regex-based section detection;
- add page-aware PDF chunking after PDF parsing exists;
- add table-aware spreadsheet chunking after XLSX/CSV parser adapters exist.

### E4. ONNX embedding expansion

Status: `done` for the first model metadata, dimension, and cache-namespace baseline.

Current limitation: ONNX embedding support is narrow: one model path, fixed output assumptions, and basic tokenization.

Needed:

- model registry;
- model metadata;
- dimension discovery or config;
- tokenizer compatibility layer;
- multiple embedding models;
- model download/install command;
- embedding cache invalidation by model version;
- fallback strategy.

Completed baseline:

- extended `EmbeddingConfig` with `model_id`, `model_name`, `model_version`, `tokenizer_type`, `pooling`, `dimension`, and `lowercase_tokens`;
- added `EmbeddingModelInfo` as the runtime embedding model metadata record;
- added stable embedding model id generation for TF-IDF and ONNX backends;
- added embedding cache namespace exposure through the effective embedding model id;
- made persisted embeddings use `RagEngine::get_embedding_model_id()` so model/version/config changes no longer silently reuse the old backend/dimension id;
- added explicit ONNX output dimension validation when `embedding.dimension` is configured;
- kept `dimension: 0` as automatic output-dimension discovery;
- added `mean` and `cls` pooling modes for 3D ONNX outputs;
- parsed tokenizer type from Hugging Face-style `tokenizer.json` metadata when available;
- added configurable tokenizer lowercasing;
- made ONNX fallback reset the effective model id/dimension to the TF-IDF backend when fallback is active;
- exposed embedding model id and dimension through health/stats responses and startup diagnostics;
- documented expanded ONNX embedding config keys in `qornix_rag/doc/CONFIG.md`.

Remaining E4 follow-ups:

- full model registry loading from a config file or directory rather than only the active configured model;
- runtime model switching and reindex orchestration;
- a first-class model install/download command with registry metadata validation;
- tokenizer implementations beyond the current basic WordPiece-like tokenizer path;
- embedding cache storage beyond model-id namespacing;
- ONNX Runtime availability and real-model integration tests in an environment that has ONNX Runtime installed.

### E5. RAG quality improvements

Status: `done` for the first citation, confidence, and grounding metadata baseline.

Needed:

- citations in answers;
- retrieval confidence;
- reranking;
- query rewriting;
- multi-query retrieval;
- conversation history;
- answer grounding checks;
- user feedback;
- evaluation datasets;
- regression tests for answer quality.

Completed baseline:

- added citation ids for Ask context entries such as `S1` and `Q1`;
- prefixed retrieved context blocks with citation ids so LLM prompts can refer to sources explicitly;
- added source path preservation for chunked project results so UI and API can show the original document path;
- added normalized per-source confidence scores;
- added response-level `retrieval_confidence`;
- added response-level `grounding_status` with `grounded`, `partial`, `weak`, and `no_context` states;
- exposed citation, confidence, and grounding metadata through `/api/ask` and `/api/search`;
- updated the standalone UI to display citations, confidence, and grounding status;
- added service regression coverage for Ask citations, source path, confidence, and grounding status.

Remaining E5 follow-ups:

- add an actual reranker after first-stage retrieval;
- add query rewriting and multi-query retrieval with transparent diagnostics;
- add conversation history with source carryover rules;
- add answer grounding checks that inspect generated output against cited context;
- add user feedback capture and analytics;
- add evaluation datasets and quality regression tests beyond service-level metadata checks;
- add citation rendering/post-processing that verifies generated answers cite known source ids.

### E6. Operations and deployment

Status: `done` for the first generated-app operations baseline.

For full web applications, not local-only standalone baseline:

- auth/RBAC when exposed as a network service;
- deployment docs;
- Docker Compose;
- persistent volumes;
- metrics;
- tracing/logging;
- admin diagnostics;
- rate limits;
- upload limits;
- backup/restore.

Completed baseline:

- added `GET /api/rag/admin/diagnostics` for read-only operational diagnostics in integrated mode;
- made `GET /api/rag/metrics` return Prometheus text metrics through the normal GET path;
- diagnostics include RAG index state, embedding model identity, LLM status, cache stats, prompt-cache stats, rate-limit counters, SQLite persistence counts, and metrics availability;
- diagnostics explicitly report that RAG does not require auth by itself and must be protected by the host application or proxy when exposed on a network;
- updated generated `rag_app` Docker Compose volumes for `knowledge_base/`, `data/`, and `logs/`;
- added generated `doc/operations.md` with persistent volume, health, diagnostics, metrics, rate limit, logging, Docker Compose, backup/restore, and security checklist guidance;
- copied `doc/operations.md` into generated default apps when `--with-rag` is used;
- documented diagnostics and metrics endpoints in RAG API and integration docs.

Remaining E6 follow-ups:

- host-application auth/RBAC integration for diagnostics, metrics, Ask, QA write, ingestion, and delete routes;
- TLS/reverse-proxy examples;
- structured tracing with request ids across host app, RAG retrieval, LLM calls, and persistence;
- first-class backup/restore commands instead of file-based documentation only;
- upload endpoints and upload-size enforcement beyond current indexing file-size limits;
- admin UI for diagnostics and ingestion job management;
- production container hardening, healthcheck directives, and non-root runtime user validation;
- alerting examples for metrics and rate-limit rejection thresholds.

## 9. Suggested implementation order

Completed order through Milestone E:

1. Close A5: configuration, route ownership, and integration boundary hardening.
2. Close A6: build separation and reusable-target preparation.
3. Close A7: portable bundle script and smoke test.
4. Close A8: standalone user documentation refresh.
5. Start Milestone B: reusable RAG core.
6. Add `qornix_web/templates/rag_app`.
7. Add `--with-rag` option for existing templates.
8. Add E1 persistent document/chunk/embedding storage baseline.
9. Add E2 ingestion pipeline and durable ingestion job baseline.
10. Add E3 chunking strategies and chunk-level persistence.
11. Add E4 embedding model metadata and ONNX expansion baseline.
12. Add E5 citation, confidence, and grounding metadata.
13. Add E6 diagnostics, metrics, and generated-app operations baseline.

Recommended post-stabilization order:

1. Done baseline: add a dedicated `VectorStore` interface and local HNSW save/load so retrieval can reuse a persisted local vector index across restarts.
2. Done baseline: add vector index metadata validation, stale-index detection, and explicit rebuild behavior.
3. Done baseline: add asynchronous/background ingestion with progress polling.
4. Done baseline: add incremental in-memory reindexing by content hash with unchanged chunk embedding reuse.
5. Done baseline: add reranking and query expansion after persisted vector retrieval is stable.
6. Done baseline: add evaluation datasets and regression tests for retrieval quality, citation correctness, and refusal behavior.
7. Done baseline: add admin UI and host-application auth/RBAC integration for generated full web apps.
8. Done baseline: stabilize `qornix_auth` before replacing RAG's token/header guard with full host-application authentication.
9. Done baseline: add `qornix_orm`-backed `AuthStore` so durable auth can use SQLite, PostgreSQL, or MySQL through the configured ORM driver.
10. Done baseline: wire generated `rag_app` and `--with-rag` applications to `qornix_auth` plus `QornixOrmAuthStore`.
11. Done baseline: add route-level auth policies for generated RAG apps using `rag:read`, `rag:write`, and `rag:admin`.
12. Done baseline: add generated-app login/logout UI plus `auth:admin` user-management API and Admin-tab controls.
13. Done baseline: harden auth/RBAC checks so active sessions and bearer tokens re-check the current user record, permission changes take effect without re-login, disabled users lose access, and route/cookie matching avoids prefix-substring bypasses.

## 10. Definition of done for the current phase

Status: `done` for the stabilization and E1-E6 baseline described in this roadmap.

The current phase is complete when these statements are true:

- standalone `run.sh` works from a clean checkout;
- standalone web UI provides full LLM-generated answers;
- answers include sources;
- QA management works;
- local indexing works for currently supported source types;
- no auth/RBAC is required for standalone mode;
- configuration behavior is documented and consistent;
- route ownership is safe for standalone and integrated modes;
- documentation explains standalone usage clearly;
- portable bundle can be produced and run outside the source tree;
- integration path with `qornix_web` is documented;
- next-phase work is explicitly separated from stabilization work.

## 11. Stabilization backlog and deferred follow-ups

This section is the single backlog for shortcomings found while implementing the current stabilization roadmap. Do not create separate `FUTURE_WORK_*` documents for issues that belong to this roadmap. If a deferred item is discovered while working on Milestone A, B, C, D, or E, add it here under the appropriate topic.

### 11.1 Retrieval relevance and query normalization

Status: deferred, not blocking A2/A3/A4.

Issues discovered during A2/A3:

- The standalone UI can ask questions and display LLM answers, but retrieval quality is still basic.
- Some answers may use loosely related files when a focused integration document is missing.
- Typos and near-miss terms such as project/module names should not be fixed by hardcoded replacements in C++ code.

Future direction:

- Add a proper `qornix_web` integration document and index it as a first-class knowledge source.
- Improve ranking so integration docs and user QA entries are preferred over loosely related source files when they match the question.
- Add configurable project vocabulary or aliases, owned by config/templates/project data rather than hardcoded in the RAG engine.
- Consider an optional query rewrite step later, but show or log rewritten queries instead of silently changing user intent.
- Improve beyond the current deterministic first-stage reranking with evaluation-backed ranking rules, project vocabularies, or a model-based reranker.

### 11.2 LLM response robustness

Status: partially improved during A2/A3, follow-up still needed.

The Ollama parser should handle escaped quotes, multiline text, code snippets, and non-streaming JSON responses reliably. Remaining backlog:

- Add regression tests for multiline code blocks, quoted includes, JSON snippets, markdown tables, and escaped backslashes.
- Surface parser failures separately from provider availability.
- Add an answer truncation indicator when a provider stops early or returns an incomplete response.
- Keep provider-specific parsing isolated behind the LLM client/provider boundary.

### 11.3 User-facing LLM diagnostics

Status: implemented for the standalone UI baseline, future UX polish remains.

Current standalone UI shows provider/model diagnostics on the main page and in Sources / Health. Future improvements:

- Add a model selection UI backed by provider model discovery.
- Persist the selected local model safely in config or a local settings layer.
- Add copy buttons for suggested commands such as `ollama pull <model>` and for config snippets.
- Keep raw `/api/health` output useful for developers, but ensure ordinary users see human-readable guidance in the UI.

### 11.4 QA/wiki scale: server-side pagination, filtering, and autocomplete

Status: `done` for the first server-side QA pagination/filtering/suggestion baseline.

A4 originally provided a local QA/wiki workflow with client-side table pagination/filtering. Post-stabilization 14 moved the default SQLite-backed QA list flow to server-side pagination, filtering, and suggestions so the UI no longer needs to load the full QA list.

Completed baseline:

- added `GET /api/qa/list?limit=25&offset=0`;
- added `GET /api/qa/list?query=...&category=...&limit=25&offset=0`;
- added `GET /api/qa/suggest?q=...&limit=10`;
- added `GET /api/qa/categories?q=...&limit=10`;
- returned `items`, `total`, `limit`, `offset`, and `has_more` from QA list responses;
- kept `pairs` as a compatibility alias for older callers;
- updated the standalone/generated RAG UI to use server-backed pages, search, categories, and question suggestions;
- added SQLite and service regression coverage.

Remaining follow-ups:

- Prefer the `qornix_web` dynamic API approach for list/filter/sort/page operations.
- Use `qornix_orm` models/query abstractions for data access instead of documenting or baking raw SQL into the roadmap.
- Add tags and tag autocomplete after QA tags exist.
- Add optional FTS/search adapter when the ORM/data layer supports it.
- Add updated-time sorting/filtering once QA updated timestamps are exposed in the service DTO.

Target API shape:

```http
GET /api/qa/list?limit=25&offset=0
GET /api/qa/list?query=порт&category=qornix_rag&limit=25&offset=0
GET /api/qa/suggest?q=por&limit=10
GET /api/qa/categories?q=rag&limit=10
```

Target list response shape:

```json
{
  "success": true,
  "items": [
    {
      "id": "qa_...",
      "question": "...",
      "answer": "...",
      "category": "...",
      "updated_at": "..."
    }
  ],
  "total": 1000000,
  "limit": 25,
  "offset": 0,
  "has_more": true
}
```

Target dynamic API / ORM requirements:

- declarative list/search/suggest operation definitions;
- `qornix_orm`-backed filtering by question, answer, category, tags, and updated time;
- `qornix_orm`-backed sorting and pagination;
- optional FTS/search adapter when the ORM/data layer supports it;
- no raw SQL examples in project roadmap documents unless the task is explicitly about low-level SQLite internals.

### 11.5 Production vector backend adapters

Status: partially implemented after the first two post-stabilization vector-store baselines.

E1 established persistent document/chunk/embedding metadata and a `PersistentIndexStore` boundary. The first post-stabilization baselines added the retrieval-time `VectorStore` boundary, local HNSW save/load, vector index metadata sidecars, stale-index detection, and rebuild-on-stale behavior. The following vector backend capabilities are still not complete:

- Keep SQLite-backed metadata as the local default, but avoid coupling retrieval to raw SQLite BLOB scans.
- Add optional Faiss backend adapter after the local HNSW backend contract is stable.
- Add optional Qdrant backend adapter for deployment scenarios that use an external vector database.
- Add optional pgvector backend adapter for PostgreSQL-backed web applications.
- Document backend selection, config keys, dependency requirements, migration behavior, and fallback behavior.
- Add tests for backend selection, stale index detection, save/load roundtrip, and fallback to rebuild.

### 11.6 Advanced ingestion adapters

Status: deferred after E2 durable text/Markdown/code/HTML ingestion baseline.

E2 established the ingestion pipeline, parser interface, parser registry, durable job records, job status APIs, and persisted document delete flow. The following ingestion capabilities are intentionally not implemented by the E2 baseline and must not be considered closed:

- Add parser plugins for PDF documents.
- Add parser plugins for DOCX documents.
- Add parser plugins for XLSX and richer CSV ingestion.
- Add parser plugins for PPTX documents.
- Add image ingestion with OCR.
- Add source-code repository structure-aware chunking beyond extension-based file parsing.
- Add language-specific code chunkers/parsers for symbols, classes, methods, functions, and namespaces instead of relying only on regex-based section detection from the E3 baseline.
- Add tokenizer-aware chunk sizing for configured embedding models, including ONNX tokenizer limits, instead of relying only on whitespace-token estimates.
- Add page-aware PDF chunking after PDF parser plugins expose page numbers, page text, and page-level metadata.
- Add table-aware spreadsheet chunking after XLSX/CSV parser plugins expose sheets, ranges, headers, and row/column metadata.
- Add parser-to-chunker metadata contracts so headings, symbols, pages, tables, captions, and source offsets survive ingestion, retrieval, persistence, and citation rendering.
- Add chunk quality tests for overlap boundaries, metadata preservation, duplicate/near-duplicate chunks, very small sections, very large sections, and mixed Markdown/code documents.
- Add live progress streaming for background ingestion jobs beyond the current polling baseline.
- Add deeper source-level modified-time shortcuts beyond the current chunk-hash embedding reuse baseline.
- Document dependency requirements, fallback behavior, and parser-specific error reporting.
- Add parser adapter tests using small fixture files for each supported format.

### 11.7 Embedding model registry and ONNX runtime follow-ups

Status: completed baseline in Post-stabilization 15; remaining runtime and installer work is deferred.

E4 added active-model metadata, stable effective model ids, embedding namespaces, dimension validation, pooling selection, and fallback-safe persistence. Post-stabilization 15 added a config-driven embedding model registry with multiple installed model definitions, active model selection, validation warnings, generated-app path resolution, health/admin diagnostics, docs, and regression coverage.

The following embedding capabilities are intentionally not complete and must not be considered closed:

- Add automatic model registry discovery from a models directory, beyond the current config-driven registry.
- Add runtime model switching with explicit reindex/re-embed orchestration and stale index warnings.
- Add a model install/download command that writes registry metadata and validates model/tokenizer compatibility after download.
- Add tokenizer implementations beyond the current basic WordPiece-like path, including compatibility with common Hugging Face tokenizer JSON variants.
- Add tokenizer-aware chunk sizing integration so E3 chunking can use the active embedding tokenizer and max sequence length.
- Add persistent embedding cache storage keyed by model id/version/content hash, beyond the current model-id namespace boundary.
- Add migration/rebuild flows when `model_id`, `model_version`, dimension, tokenizer, pooling mode, or normalization changes.
- Add ONNX Runtime integration tests in an environment with ONNX Runtime installed and a small real model fixture.
- Add user-facing diagnostics for ONNX output shape, selected pooling mode, discovered dimension, tokenizer type, and fallback reason.

### 11.8 RAG quality, citations, and evaluation follow-ups

Status: deferred after E5 citation/confidence/grounding metadata baseline.

E5 added citation ids, source path preservation, normalized confidence fields, response-level retrieval confidence, grounding status, prompt citation hints, API metadata, UI rendering, and service-level regression coverage. Post-stabilization 5 added the first actual deterministic query expansion and reranking baseline. The following quality capabilities are intentionally not complete and must not be considered closed:

- Add model-based or learned reranking beyond the current deterministic path/metadata/exact-phrase boost baseline.
- Add query rewriting and multi-query retrieval beyond the current deterministic lexical query expansion baseline, while exposing rewritten/expanded queries in diagnostics.
- Add conversation history with rules for when previous sources can or cannot ground a new answer.
- Add generated-answer grounding checks that validate claims against cited context.
- Add citation post-processing that verifies generated answers cite only known source ids such as `S1` or `Q1`.
- Add user feedback capture for answer helpfulness, citation usefulness, missing context, and wrong answers.
- Add retrieval and answer-quality analytics tied to feedback and query metadata.
- Expand evaluation datasets beyond the first local docs/QA/refusal baseline to include chunked long documents and generated template apps.
- Expand regression tests beyond the first retrieval/citation/no-context refusal baseline to cover answer completeness and generated-answer claim checking.
- Add UI affordances for reporting bad answers, missing sources, and irrelevant sources.

### 11.9 QA/wiki quality improvements

Status: deferred after A4 baseline.

Future QA improvements:

- Duplicate detection in the add/edit flow.
- Import/export for QA pairs.
- Tags in addition to category.
- Optional markdown rendering in stored answers.
- Better QA search scoring and source attribution.

### 11.10 Operations, deployment, and security follow-ups

Status: partially completed through Post-stabilization 16; remaining production hardening is deferred.

E6 added diagnostics, metrics, generated operations docs, Docker Compose volume updates, and file-based backup/restore guidance. Post-stabilization 7-13 added generated-app auth/RBAC baselines: `qornix_auth`, durable `QornixOrmAuthStore`, generated app auth wiring, route-level RAG permissions, login/user-management UI, and current-user validation for sessions/JWTs. Post-stabilization 16 added CSRF protection for cookie-authenticated browser write/admin routes in generated apps.

The following operations and security capabilities are intentionally not complete and must not be considered closed:

- Add session rotation after login and privilege-sensitive user updates.
- Add auth/security audit events for login, logout, failed login, user-management changes, permission changes, and denied access.
- Add password reset, invite, and account recovery flows for generated networked applications.
- Add optional stricter cookie settings such as configurable `SameSite`, `Secure`, domain, and session lifetime policy.
- Add TLS and reverse-proxy examples for nginx, Caddy, or an equivalent edge proxy.
- Add structured tracing with request ids across host app routing, RAG retrieval, LLM calls, SQLite persistence, and ingestion jobs.
- Add first-class backup and restore commands instead of documentation-only file backup examples.
- Add upload endpoints, upload allowlists, MIME checks, and upload-size enforcement beyond current indexing file-size limits.
- Expand the first Admin tab beyond diagnostics, metrics, and ingestion job history into reindexing, delete flows, and backup/export.
- Add production container hardening such as non-root runtime validation, healthcheck directives, read-only filesystem options, and secret mounting examples.
- Add metrics alert examples for LLM failures, rate-limit rejections, stale indexes, failed ingestion jobs, and storage growth.
- Add deploy smoke tests for generated `rag_app` and `--with-rag` Docker Compose flows.

### 11.11 Explicitly out of scope for standalone stabilization

The following remain out of scope for Milestone A unless the roadmap is explicitly changed:

- Auth/RBAC for standalone local usage.
- Multi-user permissions.
- Production vector store.
- Arbitrary document ingestion such as PDF/DOCX/XLSX/images.
- Full `qornix_web` template integration.
