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
Milestone E: in progress
Next: Milestone E3
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

Status: `in progress`

This milestone starts only after standalone stabilization, reusable core separation, and template integration are clear.

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
- added persisted document delete flow through `SQLiteSource::deletePersistedDocument()`, `RagService::deletePersistedDocument()`, and `/api/rag/documents/delete`;
- added service and SQLite tests for durable ingestion jobs and persisted document delete;
- added focused ingestion pipeline tests and kept existing data source, service, and SQLite persistence tests passing.

Remaining E2 follow-ups:

- asynchronous/background ingestion with live progress streaming instead of synchronous job completion;
- full incremental runtime indexing by modified time and content hash instead of rebuilding the in-memory index;
- parser plugins for PDF, DOCX, XLSX/CSV, PPTX, and images/OCR; see backlog 11.6.

### E3. Chunking strategies

Status: `pending`

Needed strategies:

- plain text token-aware chunking;
- markdown heading-aware chunking;
- code symbol/function-aware chunking;
- page-aware PDF chunking;
- table-aware spreadsheet chunking;
- overlap policy;
- metadata preservation.

### E4. ONNX embedding expansion

Status: `pending`

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

### E5. RAG quality improvements

Status: `pending`

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

### E6. Operations and deployment

Status: `pending`

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

## 9. Suggested implementation order

Completed order through Milestone D:

1. Close A5: configuration, route ownership, and integration boundary hardening.
2. Close A6: build separation and reusable-target preparation.
3. Close A7: portable bundle script and smoke test.
4. Close A8: standalone user documentation refresh.
5. Start Milestone B: reusable RAG core.
6. Add `qornix_web/templates/rag_app`.
7. Add `--with-rag` option for existing templates.

Recommended next order:

1. Add E3 chunking strategies so persisted chunks are not only whole-document chunks.
2. Add HNSW save/load or a replaceable vector backend that can use persisted vectors.
3. Add deeper E2 follow-ups such as asynchronous ingestion and full incremental in-memory reindexing.
4. Improve retrieval quality, citations, reranking, and evaluation after persisted chunks and vectors exist.

## 10. Definition of done for the current phase

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
- Add reranking after first-stage retrieval.

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

Status: deferred, not blocking A4.

A4 currently provides a local QA/wiki workflow and may use client-side table pagination/filtering in the standalone UI. This is acceptable for a local MVP with a small or medium QA base, but it is not a scalable design for very large datasets.

Known limitation:

- Client-side pagination loads the full QA list into the browser and only paginates visually.
- This is not suitable for hundreds of thousands or millions of QA records.

Future direction:

- Implement server-side pagination and filtering for QA list/search.
- Implement autocomplete for questions, categories, and possibly tags.
- Prefer the `qornix_web` dynamic API approach for list/filter/sort/page operations.
- Use `qornix_orm` models/query abstractions for data access instead of documenting or baking raw SQL into the roadmap.
- Keep the API contract reusable by the future `qornix_web/templates/rag_app` template and by `--with-rag` integrations.

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

Status: deferred after E1 SQLite persistence baseline.

E1 established persistent document/chunk/embedding metadata and a `PersistentIndexStore` boundary. The following planned vector backend capabilities are not implemented by the E1 baseline and must not be considered closed:

- Add a dedicated `VectorStore` interface for retrieval-time vector search, separate from metadata persistence.
- Add local HNSW save/load support so the current in-memory HNSW index can survive restart when the persisted document/chunk snapshot is still valid.
- Store and validate vector index metadata such as backend name, model id, embedding dimension, document/chunk snapshot hash, build time, and index file path.
- Load a persisted local vector index at startup when metadata matches the current persisted embedding snapshot.
- Rebuild the vector index automatically or explicitly when metadata is stale.
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
- Add asynchronous/background ingestion with progress streaming or polling against in-flight jobs.
- Add full incremental in-memory reindexing by modified time and content hash instead of rebuilding the current in-memory index.
- Document dependency requirements, fallback behavior, and parser-specific error reporting.
- Add parser adapter tests using small fixture files for each supported format.

### 11.7 QA/wiki quality improvements

Status: deferred after A4 baseline.

Future QA improvements:

- Duplicate detection in the add/edit flow.
- Import/export for QA pairs.
- Tags in addition to category.
- Optional markdown rendering in stored answers.
- Better QA search scoring and source attribution.

### 11.8 Explicitly out of scope for standalone stabilization

The following remain out of scope for Milestone A unless the roadmap is explicitly changed:

- Auth/RBAC for standalone local usage.
- Multi-user permissions.
- Production vector store.
- Arbitrary document ingestion such as PDF/DOCX/XLSX/images.
- Full `qornix_web` template integration.
