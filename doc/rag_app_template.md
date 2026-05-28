# RAG Application Template

`templates/rag_app` generates a Qornix Web application with the reusable `qornix_rag` module embedded.

Generate a project:

```bash
./create_new_project.sh ../my_rag_app --template rag_app
```

Accepted aliases:

```bash
./create_new_project.sh ../my_rag_app --template rag-app
./create_new_project.sh ../my_rag_app --template rag
```

## Build and Run

```bash
cd ../my_rag_app
cmake -S . -B build
cmake --build build
./build/my_rag_app
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/rag
http://127.0.0.1:8008/api/rag/health
```

## What Is Included

- application entry point that registers `qornix_rag` as an integrated Qornix Web module;
- RAG UI under `/` and `/rag`;
- RAG API under `/api/rag/*`;
- project-local `config.yaml`;
- SQLite QA storage at `data/rag_kb.db`;
- Markdown knowledge base directory at `knowledge_base/`;
- optional embedding model directory at `models/`;
- deploy bundle rules under `build/deploy/<app>`;
- `runtime-Dockerfile`;
- optional `docker-compose.yml` with Ollama service.

The generated app links `qornix::web_core` and `qornix::rag_extension`. It does not use the standalone-only `qornix_rag/run.sh` launcher.

## Endpoints

```text
GET  /
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

Compatibility QA routes are also available:

```text
GET  /api/rag/qa/list
POST /api/rag/qa/add
POST /api/rag/qa/update
POST /api/rag/qa/delete
```

## Local LLM

The template is ready for Ollama by default:

```yaml
rag:
  llm:
    enabled: true
    api_url: http://localhost:11434
    model: llama3.2:3b
```

Install and start a local model:

```bash
ollama serve
ollama pull llama3.2:3b
```

If you already have a different local model, change `rag.llm.model` in the generated app's `config.yaml`.

## LLM Response Cache

Generated apps enable an in-process memory cache by default:

```yaml
rag:
  cache:
    enabled: true
    backend: memory
    max_size: 1000
    ttl_seconds: 3600
```

This cache stores repeated LLM answer payloads for the same question/context/model/settings combination. It does not store parsed documents, embeddings, vector indexes, Xapian indexes or SQLite QA records.

Memory cache is simple and fast, but it is cleared on restart and is not shared between multiple app instances.

A Redis configuration block is available as the documented external/shared-cache shape:

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

Redis cache is implemented through the built-in RESP TCP client. Use `backend: memory` for a single local app and `backend: redis` when several app instances should share completed LLM response cache entries. If Redis is unavailable at startup, the app falls back to memory cache.

## Retrieval Models

You do not need an ONNX model for the first run. The default generated app uses TF-IDF retrieval:

```yaml
rag:
  embedding:
    backend: tfidf
    enable_fallback: true
```

This mode works without model files. It is lexical retrieval, not neural semantic retrieval.

ONNX semantic retrieval is optional. It can improve meaning-based matching, but the current `qornix_rag` loader is not a universal ONNX model runner. It expects a compatible BERT-style text embedding model.

The generated app needs two files:

```text
models/
  semantic_model.onnx   # the neural embedding model
  tokenizer.json        # the tokenizer vocabulary used by that model
```

Current compatibility requirements:

- `tokenizer.json` must contain `model.vocab`;
- common special tokens should be present, such as `[UNK]`, `[CLS]`, `[SEP]`, `[PAD]` or compatible alternatives;
- the ONNX model should accept integer inputs such as `input_ids`, `attention_mask`, and optionally `token_type_ids`;
- the first model output should be a float tensor shaped `[1, sequence_length, hidden_size]` or `[1, hidden_size]`.

To enable ONNX retrieval:

1. Get compatible model files.

   Recommended one-command setup from the generated app root:

   ```bash
   ./download_onnx_model.sh
   ```

   The script downloads the default BERT-style ONNX text embedding model from Hugging Face, saves it under `models/`, and updates this app's `config.yaml` to `rag.embedding.backend: onnx`.

   Useful options:

   ```bash
   ./download_onnx_model.sh --help
   ./download_onnx_model.sh --dry-run
   ./download_onnx_model.sh --force
   ./download_onnx_model.sh --no-config-update
   ```

   The default download source is:

   ```text
   Hugging Face repo: Xenova/all-MiniLM-L6-v2
   Model file:        onnx/model.onnx
   Tokenizer file:    tokenizer.json
   License:           apache-2.0
   ```

   Download an existing ONNX export from Hugging Face Hub:

   ```bash
   python3 -m pip install --user huggingface_hub
   huggingface-cli download <model-repo-id> \
     --include "*.onnx" "tokenizer.json" \
     --local-dir models/downloaded
   cp models/downloaded/*.onnx models/semantic_model.onnx
   cp models/downloaded/tokenizer.json models/tokenizer.json
   ```

   Or export a Hugging Face text encoder model yourself:

   ```bash
   python3 -m pip install --user "optimum[onnxruntime]" transformers
   optimum-cli export onnx \
     --model <model-repo-id> \
     --task feature-extraction \
     models/exported
   cp models/exported/*.onnx models/semantic_model.onnx
   cp models/exported/tokenizer.json models/tokenizer.json
   ```

   Use a text encoder / embedding model. Do not use Ollama models, `.gguf` files, chat LLM weights, image models, or arbitrary ONNX files here.

   Official references:

   - Hugging Face Hub: https://huggingface.co/models
   - Optimum ONNX export: https://huggingface.co/docs/optimum/exporters/onnx/usage_guides/export_a_model
   - ONNX Runtime install: https://onnxruntime.ai/docs/install/

2. Build on a machine where ONNX Runtime development files are available. During CMake configure, avoid this warning:

   ```text
   ONNX Runtime not found. Build will use TF-IDF fallback.
   ```

3. Place compatible files in the generated app:

   ```text
   models/semantic_model.onnx
   models/tokenizer.json
   ```

4. Update `config.yaml`:

   ```yaml
   rag:
     embedding:
       backend: onnx
       model_path: models/semantic_model.onnx
       tokenizer_path: models/tokenizer.json
       max_seq_len: 512
       onnx_threads: 2
       normalize_embeddings: true
       enable_fallback: true
   ```

5. Restart the app.

6. Check the health endpoint:

   ```text
   http://127.0.0.1:8008/api/rag/health
   ```

The application resolves model paths relative to its root directory. Model files are not bundled by default because embedding model licensing and size vary. Keep `enable_fallback: true` while testing a new model so the app can continue with TF-IDF if ONNX cannot be initialized.

## Knowledge Base

Generated RAG apps do not scan `knowledge_base/` automatically unless `rag.indexing.scan_path` is configured. Put Markdown files in `knowledge_base/`, then index that directory from the UI or API:

```bash
curl -X POST http://127.0.0.1:8008/api/rag/index \
  -H 'Content-Type: application/json' \
  -d '{"scan_path":"knowledge_base"}'
```

Ask a question:

```bash
curl -X POST http://127.0.0.1:8008/api/rag/ask \
  -H 'Content-Type: application/json' \
  -d '{"question":"What is in this knowledge base?","top_k":5}'
```

## Deploy Bundle

```bash
cmake --build build --target my_rag_app_deploy
cd build/deploy/my_rag_app
./my_rag_app
```

The deploy bundle contains the executable, `config.yaml`, RAG UI templates, static assets, docs, `knowledge_base/`, `models/`, `data/`, and `logs/`.

## Current RAG Feature Set In Generated Apps

The `rag_app` template now includes the same product-facing RAG UI as standalone mode, with app-local paths and `/api/rag/*` route prefixes.

Important included features:

- upload tab with secure multi-file document upload;
- upload validation by extension, MIME type, file size and max file count;
- app-local upload storage under `data/uploads`;
- ingestion/reindex after upload so documents become searchable;
- delete flow for uploaded files;
- local HNSW vector index and Xapian lexical index;
- SQLite QA/wiki storage under `data/rag_kb.db`;
- QA duplicate preview, history, tags and safe Markdown rendering;
- metadata-aware search/ask filters and citation locators;
- LLM provider diagnostics that separate provider availability from missing configured model;
- embedding model registry diagnostics under `/api/rag/embedding/models`;
- optional auth/RBAC integration for write/admin RAG routes.

For the complete feature/dependency/model guide, read:

```text
qornix_rag/doc/FULL_RAG_GUIDE.md
```

## Generated App Profiles

### Lightweight profile

This is the generated default and is suitable for first run:

```yaml
rag:
  embedding:
    backend: tfidf
    enable_fallback: true
  vector_store:
    backend: local_hnsw
  upload:
    enabled: true
    uploads_dir: data/uploads
```

It does not require ONNX Runtime or downloaded embedding files.

### Full semantic profile

After installing ONNX Runtime and placing compatible model files under `models/`, switch to:

```yaml
rag:
  embedding:
    backend: onnx
    active_model_id: local-semantic-v1
    models_dir: models
    auto_discover_models: true
    enable_fallback: true
    registry:
      local-semantic-v1:
        backend: onnx
        model_path: models/semantic_model.onnx
        tokenizer_path: models/tokenizer.json
        tokenizer_type: WordPiece
        pooling: mean
        dimension: 384
        max_seq_len: 256
        normalize_embeddings: true
```

Then rebuild if ONNX Runtime was installed after the app was first configured:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/onnxruntime
cmake --build build -j$(nproc)
```

### LLM model selection

The template default is intentionally small:

```yaml
rag:
  llm:
    api_url: http://localhost:11434
    model: llama3.2:3b
```

Use `ollama list` and copy the exact model id into `rag.llm.model`. If Ollama is reachable but the configured model is missing, the UI will show available models instead of incorrectly reporting that the provider is down.


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
    key_prefix: qornix_rag:
    redis:
      host: 127.0.0.1
      port: 6379
      db: 0
      password: ""
      ttl_seconds: 3600
```
