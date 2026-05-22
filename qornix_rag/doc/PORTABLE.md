# Portable Qornix RAG Bundle

This guide explains how to build and run the portable standalone `qornix_rag` bundle.

The portable bundle is intended for compatible Linux x86_64 systems. It is not a universal binary for every OS or libc version.

## Build

From the repository root:

```bash
./qornix_rag/build_portable.sh
```

Recommended full check:

```bash
./qornix_rag/build_portable.sh --smoke --archive
```

This creates:

```text
dist/qornix_rag-portable-linux-x86_64/
dist/qornix_rag-portable-linux-x86_64.tar.gz
```

## Run

From the generated bundle:

```bash
cd dist/qornix_rag-portable-linux-x86_64
./run.sh
```

Open:

```text
http://localhost:8081
```

Use another port:

```bash
./run.sh --port 8082
```

Index a project:

```bash
./run.sh --project /path/to/project
```

## Bundle Layout

```text
dist/qornix_rag-portable-linux-x86_64/
  bin/qornix_rag
  config/config.yaml
  config/config.example.yaml
  templates/rag_interface.html
  models/
  data/
  logs/
  cache/
  knowledge_base/
  lib/ldd.txt
  run.sh
  README.md
```

The portable config uses paths relative to the bundle root:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true
  # Optional ONNX paths when model files are added:
  model_path: models/semantic_model.onnx
  tokenizer_path: models/tokenizer.json

rag:
  sqlite:
    db_path: data/rag_kb.db
  markdown:
    directory_path: knowledge_base
```

The portable bundle may contain a `models/` directory, but large ONNX model files are not bundled by default. Keep `backend: tfidf` for a model-free bundle. To enable ONNX semantic retrieval in the source tree before packaging, run:

```bash
./qornix_rag/download_onnx_model.sh
```

For an already built portable bundle, copy `semantic_model.onnx` and `tokenizer.json` into the bundle's `models/` directory and set `embedding.backend: onnx`.

Startup auto-indexing is disabled by default in the portable config so the bundle does not index itself.

## Options

```bash
./qornix_rag/build_portable.sh --help
```

Useful options:

- `--build-dir <dir>`: custom CMake build directory.
- `--dist-dir <dir>`: custom output directory.
- `--name <name>`: custom bundle directory name.
- `--smoke`: start the bundle and verify `/api/health`.
- `--archive`: create `.tar.gz`.
- `--include-system-libs`: copy ldd-discovered non-glibc shared libraries into `lib/`.

## What Is Not Bundled

The bundle intentionally does not include runtime/user data:

- SQLite QA database created during local use;
- Xapian index directories;
- logs;
- cache files;
- build directories;
- secrets or API keys.
