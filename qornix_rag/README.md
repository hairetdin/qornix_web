# Qornix RAG

Qornix RAG is a local single-user wiki/RAG application for indexing a local project, maintaining QA knowledge, searching local context, and asking an LLM questions with retrieved sources.

Standalone mode is local-first: by default it binds to `127.0.0.1:8081`, uses `qornix_rag/config.yaml`, stores local runtime data under `qornix_rag/data/`, and serves the web UI from `qornix_rag/templates/rag_interface.html`.

## Quick Start

From the repository root:

```bash
./qornix_rag/run.sh
```

Then open:

```text
http://localhost:8081
```

Useful variants:

```bash
./qornix_rag/run.sh --port 8082
./qornix_rag/run.sh --project /path/to/project
./qornix_rag/run.sh --address 127.0.0.1 --port 8081 --project /path/to/project
QORNIX_RAG_CONFIG=/path/to/config.yaml ./qornix_rag/run.sh
```

Use `--address 0.0.0.0` only when you intentionally want network access.

## Web UI

The standalone UI has four main areas:

- `Ask`: asks the configured LLM using retrieved local context.
- `Search`: searches indexed project files and QA entries.
- `QA`: add, list, edit, delete, import/export, tag, filter, and page local QA/wiki entries.
- `Sources / Health`: inspect index status, LLM status, sources, and diagnostics.

If the LLM provider or model is unavailable, Ask falls back gracefully and the UI shows diagnostics, including the configured model and available provider models when the provider reports them.

## Local LLM

The default config is prepared for Ollama:

```yaml
llm:
  api_url: "http://localhost:11434"
  model: "llama3"
```

Example setup:

```bash
ollama pull llama3
ollama serve
./qornix_rag/run.sh
```

If the configured model is missing, either pull it:

```bash
ollama pull llama3
```

or change `llm.model` in `qornix_rag/config.yaml` to a model listed by the UI health diagnostics.

## Retrieval Models

A fresh `git clone` does not include large ONNX embedding model files. GitHub is not a good place to store those binaries, and model licenses vary.

The default standalone config therefore uses TF-IDF retrieval:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true
```

This is enough to run `./qornix_rag/run.sh`, index files, search, manage QA entries, and ask an LLM with retrieved context. It is lexical retrieval, not neural semantic retrieval.

To enable ONNX semantic retrieval, add compatible files:

```text
qornix_rag/models/semantic_model.onnx
qornix_rag/models/tokenizer.json
```

Recommended one-command setup:

```bash
./qornix_rag/download_onnx_model.sh
```

The script downloads a default BERT-style ONNX text embedding model from Hugging Face, saves it under `qornix_rag/models/`, writes an `embedding.registry` entry, and makes it active. Runtime model operations are exposed through `GET /api/embedding/models` and `POST /api/embedding/switch`; switching can reindex immediately with `{"model_id":"...","reindex":true,"force_reembed":true}`.

Manual setup is also possible. See [Embedding models guide](models/README.md) for model sources, compatibility limits, and advanced options.

## API Smoke

```bash
curl http://localhost:8081/api/health
curl http://localhost:8081/api/sources
curl -X POST http://localhost:8081/api/search \
  -H "Content-Type: application/json" \
  -d '{"query":"How does routing work?","top_k":5}'
curl -X POST http://localhost:8081/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question":"How does routing work?","top_k":5}'
```

Reindex the current configured project:

```bash
curl -X POST http://localhost:8081/api/index \
  -H "Content-Type: application/json" \
  -d '{}'
```

## Portable Bundle

Build a portable Linux x86_64 standalone bundle:

```bash
./qornix_rag/build_portable.sh --smoke --archive
```

Output:

```text
dist/qornix_rag-portable-linux-x86_64/
dist/qornix_rag-portable-linux-x86_64.tar.gz
```

Run the bundle:

```bash
cd dist/qornix_rag-portable-linux-x86_64
./run.sh
```

The portable config uses bundle-relative paths such as `models/...`, `data/rag_kb.db`, and `knowledge_base`. Startup auto-indexing is disabled by default so the bundle does not index itself.

Full portable build/run documentation: [Portable bundle guide](doc/PORTABLE.md).

## Documentation

- [Standalone guide](doc/STANDALONE.md)
- [Configuration reference](doc/CONFIG.md)
- [API reference](doc/API.md)
- [Portable bundle guide](doc/PORTABLE.md)
- [qornix_web integration](doc/INTEGRATION_QORNIX_WEB.md)
- [Migration from old code search UI](doc/MIGRATION_FROM_CODE_SEARCH_UI.md)

Project planning documents are under `qornix_rag/doc/project_doc/`.

## Build Targets

Current CMake targets:

- `qornix_rag_core`: reusable RAG services and data sources.
- `qornix_rag_http`: HTTP handlers and route registration.
- `qornix_rag_extension`: qornix_web extension adapter.
- `qornix_rag`: standalone executable.
- `qornix_rag_route_extension`: optional dynamic route extension shared object.

Manual standalone build:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON
cmake --build build --target qornix_rag
```

Dynamic route extension build:

```bash
cmake -S . -B build -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=ON
cmake --build build --target qornix_rag_route_extension
```

## Not Yet Supported

These are intentionally deferred beyond the standalone stabilization stage:

- arbitrary binary document ingestion beyond the explicitly supported parser adapters;
- richer image/OCR metadata beyond the first `tesseract` text baseline;
- richer PPTX structure/metadata extraction beyond the first presentation text baseline;
- richer XLSX/CSV table structure and chunk metadata beyond the first spreadsheet text baseline;
- richer DOCX structure/metadata extraction beyond the first `libzip` text baseline;
- page-aware PDF chunking and PDF metadata extraction beyond the first `pdftotext` text baseline;
- production vector store;
- multi-user workspaces;
- authentication/RBAC for standalone mode;
- cloud deployment hardening;
- advanced reranking and evaluation pipelines.
