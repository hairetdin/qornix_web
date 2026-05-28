# Qornix RAG Standalone

Standalone mode is a local single-user RAG/wiki application. It is intended to run on your machine, index local project files and uploaded documents, maintain a local QA knowledge base, and ask a configured LLM questions with retrieved context. For the complete dependency/model/database guide, see [FULL_RAG_GUIDE.md](FULL_RAG_GUIDE.md).

## Run From Source

From the repository root:

```bash
./qornix_rag/run.sh
```

The script:

- configures/builds the `qornix_rag` target;
- creates `qornix_rag/data`, `qornix_rag/logs`, and `qornix_rag/knowledge_base`;
- sets `QORNIX_RAG_HOME`;
- sets `QORNIX_RAG_TEMPLATES_DIR`;
- starts the server with `qornix_rag/config.yaml`.

Open:

```text
http://localhost:8081
```

## Ports And Bind Address

Default:

```text
127.0.0.1:8081
```

Examples:

```bash
./qornix_rag/run.sh --port 8082
./qornix_rag/run.sh --address 127.0.0.1 --port 8081
```

Binding to all interfaces is explicit:

```bash
./qornix_rag/run.sh --address 0.0.0.0
```

Standalone mode has no authentication. Use `0.0.0.0` only when the network exposure is intentional and controlled.

## Index A Project

At startup:

```bash
./qornix_rag/run.sh --scan-path /path/to/project
```

From the API:

```bash
curl -X POST http://localhost:8081/api/index \
  -H "Content-Type: application/json" \
  -d '{"scan_path":"/path/to/project"}'
```

From the UI, use `Sources / Health` and the reindex action.

## Ask And Search

Ask:

```bash
curl -X POST http://localhost:8081/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question":"How does the RAG extension configure routes?","top_k":5}'
```

Search:

```bash
curl -X POST http://localhost:8081/api/search \
  -H "Content-Type: application/json" \
  -d '{"query":"RAG extension routes","top_k":5}'
```

Ask uses retrieved project snippets and QA/wiki entries as context for the LLM. If the LLM is unavailable, the response falls back to relevant context and reports `llm_status`.

## Retrieval Models

Standalone works after a normal `git clone` without bundled ONNX model files. The default `qornix_rag/config.yaml` uses:

```yaml
embedding:
  backend: tfidf
```

That gives lexical retrieval. It is enough for local startup, indexing, Search, QA, and LLM Ask with retrieved context.

ONNX semantic retrieval is optional. Model binaries are not committed to GitHub because they are large and may have separate licenses. To enable ONNX, add:

```text
qornix_rag/models/semantic_model.onnx
qornix_rag/models/tokenizer.json
```

Recommended setup:

```bash
./qornix_rag/download_onnx_model.sh
```

The script downloads a default ONNX embedding model and changes `embedding.backend` to `onnx` in `qornix_rag/config.yaml`. See `qornix_rag/models/README.md` for manual download/export options and compatibility requirements.


## Upload Documents

The standalone UI includes an **Upload** tab. Uploaded files are stored under `upload.uploads_dir`, validated by extension/MIME/size allowlists, ingested, chunked, embedded, and made searchable.

API example:

```bash
curl -X POST http://localhost:8081/api/documents/upload \
  -F "files=@/path/to/document.pdf" \
  -F "files=@/path/to/notes.md"
```

Delete an uploaded file through the UI or API:

```bash
curl -X POST http://localhost:8081/api/uploads/delete \
  -H "Content-Type: application/json" \
  -d '{"path":"qornix_rag/data/uploads/upload_.../document.pdf","reindex":true}'
```

Deletion is restricted to files inside the configured upload directory.

## QA Knowledge Base

The QA tab supports:

- add QA pair;
- list QA pairs with server-side pagination;
- edit QA pair;
- delete QA pair;
- filter by text/category through the QA API;
- use question and category suggestions.

QA entries are included in Ask context and Search results. This lets local wiki knowledge override or complement project snippets.

API example:

```bash
curl -X POST http://localhost:8081/api/qa/add \
  -H "Content-Type: application/json" \
  -d '{"question":"How do I run standalone RAG?","answer":"Use ./qornix_rag/run.sh","category":"setup"}'
```

## Portable Bundle

Build:

```bash
./qornix_rag/build_portable.sh --smoke --archive
```

Run:

```bash
cd dist/qornix_rag-portable-linux-x86_64
./run.sh
```

The portable bundle uses relative paths and is intended for compatible Linux x86_64 systems.

See the full portable bundle guide: [PORTABLE.md](PORTABLE.md).
