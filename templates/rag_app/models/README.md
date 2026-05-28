# Generated App Embedding Models

This directory is for optional ONNX **embedding** models used by the generated RAG app. It is not for Ollama `.gguf` chat models.

The app works without files in this directory because the default config uses TF-IDF:

```yaml
rag:
  embedding:
    backend: tfidf
    enable_fallback: true
```

To enable semantic embeddings, install ONNX Runtime C++ SDK, add compatible model files, and switch `rag.embedding.backend` to `onnx`.

## Required Files

```text
models/
  semantic_model.onnx
  tokenizer.json
```

or an auto-discovered subdirectory:

```text
models/mini-encoder/
  model.onnx
  tokenizer.json
```

## Recommended Setup

From the generated app root:

```bash
./download_onnx_model.sh
```

The helper downloads a default BERT-style text embedding model and can update `config.yaml`.

Default source:

```text
Hugging Face repo: Xenova/all-MiniLM-L6-v2
Model file:        onnx/model.onnx
Tokenizer file:    tokenizer.json
License:           apache-2.0
```

Review the model license before product use.

## Manual Download

```bash
python3 -m pip install --user huggingface_hub
huggingface-cli download Xenova/all-MiniLM-L6-v2 \
  --include "onnx/model.onnx" "tokenizer.json" \
  --local-dir models/downloaded
cp models/downloaded/onnx/model.onnx models/semantic_model.onnx
cp models/downloaded/tokenizer.json models/tokenizer.json
```

## ONNX Runtime C++ SDK

The Python `onnxruntime` package is not enough for the C++ build. Install the C++ SDK and expose it to CMake, for example:

```bash
cd /tmp
ORT_VERSION=1.24.4
wget https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz
tar -xzf onnxruntime-linux-x64-${ORT_VERSION}.tgz
sudo mkdir -p /opt/onnxruntime
sudo cp -r onnxruntime-linux-x64-${ORT_VERSION}/* /opt/onnxruntime/
echo /opt/onnxruntime/lib | sudo tee /etc/ld.so.conf.d/onnxruntime.conf
sudo ldconfig
```

Then rebuild the generated app:

```bash
rm -rf build
cmake -S . -B build \
  -DONNXRUNTIME_ROOT=/opt/onnxruntime \
  -DCMAKE_PREFIX_PATH=/opt/onnxruntime
cmake --build build -j$(nproc)
```

If CMake says `ONNX Runtime not found`, the app will keep working with TF-IDF fallback but ONNX embeddings will not be active.

## Config Example

```yaml
rag:
  embedding:
    backend: onnx
    active_model_id: local-semantic-v1
    models_dir: models
    auto_discover_models: true
    validate_model_files: true
    enable_fallback: true
    registry:
      local-semantic-v1:
        backend: onnx
        name: all-MiniLM-L6-v2 local ONNX
        model_path: models/semantic_model.onnx
        tokenizer_path: models/tokenizer.json
        tokenizer_type: WordPiece
        pooling: mean
        dimension: 384
        max_seq_len: 256
        onnx_threads: 2
        normalize_embeddings: true
        lowercase_tokens: true
```

Check diagnostics:

```text
http://127.0.0.1:8008/api/rag/embedding/models
http://127.0.0.1:8008/api/rag/health
```

More details are in the framework docs:

```text
qornix_rag/doc/FULL_RAG_GUIDE.md
qornix_rag/models/README.md
```
