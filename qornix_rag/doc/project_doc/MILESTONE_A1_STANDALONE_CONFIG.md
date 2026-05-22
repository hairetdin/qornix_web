# Milestone A1 - standalone config and startup normalization

Status: implemented in patch `qornix_rag_milestone_a1_standalone_config.patch`
Scope: standalone `qornix_rag` startup, local defaults, config key normalization, and startup diagnostics

## Goals

Milestone A1 makes the repository-run standalone service predictable before the UI is rebuilt in A2/A3.

The expected local developer/user flow is:

```bash
./qornix_rag/run.sh
```

The script builds the standalone binary, creates local runtime directories, loads `qornix_rag/config.yaml`, and starts a localhost-only service by default.

## Decisions implemented

- Standalone bind address is local-first: `127.0.0.1` by default.
- Standalone default port is normalized to `8081` in code, config, docs, and run script messaging.
- Canonical config key is now `server.address`.
- Legacy `server.host` is still accepted by `main.cpp` for compatibility.
- `run.sh` now resolves paths from its own location, so it works when called from any current directory.
- `run.sh --help` prints launcher usage and exits before dependency checks, build, or server startup.
- `run.sh` resolves the CMake target binary from `build/qornix_rag/qornix_rag` instead of trying to execute the `build/qornix_rag` directory.
- `run.sh` creates local standalone runtime directories:
  - `qornix_rag/data/`
  - `qornix_rag/logs/`
  - `qornix_rag/knowledge_base/`
- Startup indexing remains enabled by default for local wiki/RAG usage.
- Startup indexing can be disabled with `indexing.auto_index_on_startup: false`.
- LLM-disabled or LLM-unavailable mode is explicitly reported as search-only fallback.

## Config shape

Canonical standalone server config:

```yaml
server:
  address: 127.0.0.1
  port: 8081
  project_path: .

indexing:
  auto_index_on_startup: true
```

Path conventions under `run.sh`:

- current working directory for the process is the repository root;
- `project_path: .` indexes the repository root by default;
- SQLite QA storage is under `qornix_rag/data/rag_kb.db`;
- Markdown knowledge base is under `qornix_rag/knowledge_base`;
- ONNX/tokenizer paths are under `qornix_rag/models`.

## Startup diagnostics

Standalone startup now prints:

- bind address and port;
- local browser URL;
- warning when explicitly binding to all interfaces;
- project path;
- auto-index setting;
- resolved config path;
- embedding backend;
- embedding fallback mode;
- LLM model and API URL when configured;
- clear search-only fallback message when LLM is disabled or unavailable;
- health endpoint URL after server startup.

## Acceptance checks

Recommended checks after applying the patch:

```bash
git diff --check
bash -n qornix_rag/run.sh
./qornix_rag/run.sh --help
./qornix_rag/run.sh --port 8081
curl http://localhost:8081/api/health
```

Expected results:

- help shows address default `127.0.0.1`;
- config uses `server.address`, not `server.host`;
- service binds locally by default;
- `/api/health` is registered and reachable while the server is running;
- unavailable LLM does not crash startup and is reported as search-only fallback.


## Follow-up fix: web UI template path

A follow-up A1 patch fixes repository-root startup where the server process runs
from the repository root but the standalone UI template lives in
`qornix_rag/templates/rag_interface.html`.

Implemented behavior:

- `run.sh` exports `QORNIX_RAG_HOME` and `QORNIX_RAG_TEMPLATES_DIR`.
- `run.sh` validates that `rag_interface.html` exists before launching the server.
- startup diagnostics print the templates directory used by the standalone process.
- `setupRagRoutes()` resolves the template directory from `QORNIX_RAG_TEMPLATES_DIR`,
  then falls back to `qornix_rag/templates` and `templates` relative to the current
  working directory.

Acceptance check:

```bash
./qornix_rag/run.sh --port 8082
curl -i http://localhost:8082/
```

Expected result: `/` returns the standalone UI instead of the fallback 404 page,
and startup logs show the resolved templates directory.

## Remaining work for Milestone A

A1 does not replace the old UI. Next step is A2/A3:

- make Ask/Chat the primary UI flow;
- call `/api/ask` from the web interface;
- display full LLM-generated answer plus sources;
- keep search-only fallback visible and useful;
- replace the old code-search-only UI with local wiki/RAG UI sections.
