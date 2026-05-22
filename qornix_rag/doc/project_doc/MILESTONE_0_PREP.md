# Milestone 0 preparation for Qornix RAG stabilization

Status: completed preparation step
Scope: repository hygiene, baseline snapshot, and Milestone A execution checklist

## 1. Decisions confirmed

- Standalone `qornix_rag` is a local single-user application and does not need authentication, authorization, RBAC, tenants, or login/logout flows.
- Standalone mode must provide full LLM-generated answers in the web UI after stabilization.
- Standalone mode is intended for local wiki/RAG usage: index a local project or folders, add QA pairs, search, ask questions, and view sources.
- `qornix_rag/run.sh` remains the developer/local-from-repository entry point.
- A separate portable bundle script must be added later for a user-facing folder that can be copied to another compatible machine.
- Full multi-user web application work belongs in `qornix_web/templates` and should be generated through `create_new_project.sh`.
- RAG must later be available both as a dedicated `rag_app` template and as an option for existing templates.
- Project planning documentation belongs in `qornix_rag/doc/project_doc/`.
- User-facing documentation belongs in `README.md` and focused user guides.

## 2. Documentation hygiene completed

- The stabilization roadmap was moved from `qornix_rag/doc/` to `qornix_rag/doc/project_doc/`.
- The top-level `qornix_rag/README.md` was kept user-facing and no longer carries the project roadmap section.
- This preparation note records the baseline and the execution checklist for Milestone A.

## 3. Current standalone baseline

The current standalone implementation has these entry points and behaviors:

- `qornix_rag/run.sh` builds the repository with CMake and launches `./build/qornix_rag --config qornix_rag/config.yaml`.
- `qornix_rag/main.cpp` starts the standalone binary, loads YAML config through a flattened map, indexes the configured project on startup, constructs `LLMClient` from the resolved config path, and registers RAG web/API routes.
- Default CLI values in `main.cpp` are address `0.0.0.0`, port `8081`, project path `.`, and config path `config.yaml`.
- `qornix_rag/config.yaml` currently uses `server.host`, while `main.cpp` reads `server.address`; this mismatch should be fixed in Milestone A.
- `qornix_rag/config.yaml` currently sets port `8082`, while README and code defaults mention `8081`; the expected default must be normalized in Milestone A.
- `qornix_rag/templates/rag_interface.html` is still titled as code search and primarily calls `/api/search`; it is not yet the target local wiki/RAG UI.
- `setupRagRoutes()` registers the web UI on `/` and registers API routes such as `/api/index`, `/api/search`, `/api/stats`, `/api/ask`, `/api/batch`, `/api/health`, `/api/metrics`, `/api/sources`, `/api/qa/*`, analytics, deduplication, and Markdown import routes when the corresponding services are available.
- `/api/ask` already has backend shape for RAG context retrieval and LLM answers, but the standalone UI does not yet expose it as the primary Ask/Chat workflow.
- The integrated `RagExtension` path is not part of Milestone A, but it remains a known later issue because it constructs `LLMClient` with an empty config path and expects `rag.*` keys.

## 4. Milestone A execution order

Milestone A should be executed in small patches in this order.

### A1. Normalize standalone config and startup

Goal: `qornix_rag/run.sh` starts a predictable local service.

Tasks:

- Change standalone default bind address to `127.0.0.1`.
- Normalize config keys, especially `server.host` vs `server.address`.
- Normalize the default standalone port across config, README, console output, and scripts.
- Keep automatic indexing on startup only if it is predictable and documented; otherwise make it explicit through UI/API.
- Add clear startup logging for config path, bind address, project path, LLM model, and fallback mode.
- Keep auth/RBAC out of standalone mode.

Acceptance checks:

- `qornix_rag/run.sh` builds and starts the service from the repository.
- `GET /api/health` works.
- The service binds to localhost by default.
- LLM-disabled mode degrades to search without crashing.
- LLM-enabled mode reports model/API availability clearly.

### A2. Make LLM answers the primary standalone UI flow

Goal: the web UI provides full RAG answers through the configured LLM.

Tasks:

- Add Ask/Chat UI that calls `/api/ask`.
- Display the generated answer, sources, snippets, scores, and useful metadata.
- Display clear errors when the LLM endpoint is unavailable.
- Preserve search-only fallback when LLM is disabled.
- Make sources visible enough that users can trust and inspect the answer.

Acceptance checks:

- User can ask a question from the web UI.
- Backend retrieves context and calls LLM when configured.
- UI shows a full answer plus sources.
- UI shows a useful degraded response when LLM is disabled or unavailable.

### A3. Replace the old code-search-only UI with local wiki/RAG UI

Goal: standalone UI matches the intended local wiki/RAG product.

Tasks:

- Replace the old title and framing around code search.
- Add main sections for Ask, Search, Sources, QA, Indexing, and Health/Settings.
- Keep API calls simple and compatible with current backend routes.
- Avoid adding auth, user management, or multi-tenant concepts.

Acceptance checks:

- The first screen guides the user to ask a question or index local data.
- Search remains available but is no longer the only primary workflow.
- Health and LLM status are visible.

### A4. Complete the local QA/wiki workflow

Goal: users can add and manage local knowledge without editing files manually.

Tasks:

- Add UI for listing QA pairs.
- Add UI for creating QA pairs.
- Add UI for updating and deleting QA pairs if backend support is stable.
- Ensure QA sources participate in search and `/api/ask` answers.
- Make SQLite-backed QA behavior explicit in config and docs.

Acceptance checks:

- A QA pair added through UI is retrievable.
- A question can use QA context in a generated answer.
- QA list/update/delete behavior is either implemented or explicitly marked unavailable in UI.

### A5. Add portable standalone bundle build

Goal: provide a user-facing folder that can be copied and launched on another compatible machine.

Tasks:

- Add `qornix_rag/build_portable.sh` or `qornix_rag/package.sh --portable`.
- Produce `dist/qornix_rag-portable-<platform>-<arch>/`.
- Include binaries, runtime libraries when needed, templates, static files, config examples, launch script, and README.
- Use paths relative to the bundle root.
- Do not include user indexes, secrets, or private data.
- Add a smoke test for portable startup and health check.

Acceptance checks:

- The bundle starts without source files.
- The bundle opens the same standalone UI.
- The bundle can create local data/log/cache directories on first launch.
- Documentation states OS/architecture/libc compatibility limits.

### A6. Update user-facing documentation

Goal: README and user docs describe the stabilized standalone product accurately.

Tasks:

- Update quick start for `qornix_rag/run.sh`.
- Document LLM setup with an Ollama/OpenAI-compatible endpoint.
- Document Ask, Search, Sources, QA, and Indexing workflows.
- Document portable bundle build and launch.
- Document troubleshooting for missing LLM, port conflicts, config paths, and indexing problems.

Acceptance checks:

- A new user can run standalone from the README.
- A new user can configure LLM answers.
- A new user can build or run the portable folder.

## 5. Out of scope for Milestone A

These items belong to later milestones:

- `qornix_web/templates/rag_app`.
- `create_new_project.sh --template rag_app`.
- `create_new_project.sh --with-rag`.
- Multi-user authentication, authorization, RBAC, tenant separation, and deployment security.
- Persistent vector store abstraction.
- Arbitrary document ingestion for PDF, DOCX, XLSX, images, and OCR.
- ONNX model registry, model download, and multiple embedding model support.
- Advanced reranking, query rewriting, evaluation, feedback loops, and production observability.
