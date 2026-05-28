# Embedding Models

This directory is reserved for optional ONNX semantic retrieval model files. It is **not** for Ollama `.gguf` chat models.

A fresh clone works without model files because the default config uses TF-IDF retrieval:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true
```

TF-IDF is dependency-light and good for first run, exact search and code/documentation search. ONNX embeddings are optional and improve meaning-based retrieval when a compatible model and ONNX Runtime C++ SDK are available.

## LLM Model vs Embedding Model

| Model type | Used for | Where it lives | Example |
|---|---|---|---|
| Chat/LLM model | Generates answers | Ollama, LM Studio, vLLM, OpenAI-compatible provider | `llama3.2:3b`, `qwen2.5-coder:7b` |
| Embedding model | Turns chunks into vectors for retrieval | `qornix_rag/models/*.onnx` + `tokenizer.json` | `all-MiniLM-L6-v2` ONNX export |

Do not copy Ollama model files into this directory. The ONNX backend expects a BERT-style text encoder / sentence embedding model.

## Required Files

Typical layout:

```text
qornix_rag/models/
  semantic_model.onnx
  tokenizer.json
```

Auto-discovery also supports subdirectories:

```text
qornix_rag/models/mini-encoder/
  model.onnx
  tokenizer.json
```

A directory with both an `.onnx` file and `tokenizer.json` can be auto-registered when `embedding.auto_discover_models: true`.

## Compatibility Requirements

The current ONNX loader is not a universal model runner. It expects a compatible text embedding model:

- `tokenizer.json` should be a Hugging Face tokenizer file;
- supported vocab layouts include common WordPiece, BPE, WordLevel and Unigram-style JSON shapes;
- common special tokens should exist, such as `[UNK]`, `[CLS]`, `[SEP]`, `[PAD]`, `<s>`, `</s>`, `<pad>`, `<unk>`;
- the ONNX model should accept integer inputs such as `input_ids`, `attention_mask`, and optionally `token_type_ids`;
- the first output should be a float tensor shaped `[1, sequence_length, hidden_size]` or `[1, hidden_size]`;
- `pooling: mean` is the usual default for token-level outputs;
- `normalize_embeddings: true` is recommended for cosine-style retrieval.

Avoid decoder-only chat LLMs, image models, arbitrary ONNX exports, and `.gguf` files.

## Recommended Setup

From the repository root:

```bash
./qornix_rag/download_onnx_model.sh
```

The script downloads the default model, writes:

```text
qornix_rag/models/semantic_model.onnx
qornix_rag/models/tokenizer.json
```

and can update `qornix_rag/config.yaml` to use `embedding.backend: onnx`.

Useful options:

```bash
./qornix_rag/download_onnx_model.sh --help
./qornix_rag/download_onnx_model.sh --dry-run
./qornix_rag/download_onnx_model.sh --force
./qornix_rag/download_onnx_model.sh --no-config-update
```

Default download source:

```text
Hugging Face repo: Xenova/all-MiniLM-L6-v2
Model file:        onnx/model.onnx
Tokenizer file:    tokenizer.json
License:           apache-2.0
```

Always review the model license before using it in your product.

## Manual Download From Hugging Face

```bash
python3 -m pip install --user huggingface_hub
huggingface-cli download Xenova/all-MiniLM-L6-v2 \
  --include "onnx/model.onnx" "tokenizer.json" \
  --local-dir qornix_rag/models/downloaded
cp qornix_rag/models/downloaded/onnx/model.onnx qornix_rag/models/semantic_model.onnx
cp qornix_rag/models/downloaded/tokenizer.json qornix_rag/models/tokenizer.json
```

For another repository, replace `Xenova/all-MiniLM-L6-v2` and check the file layout.

## Manual Export To ONNX

```bash
python3 -m pip install --user "optimum[onnxruntime]" transformers
optimum-cli export onnx \
  --model sentence-transformers/all-MiniLM-L6-v2 \
  --task feature-extraction \
  qornix_rag/models/exported
```

Then copy the exported model/tokenizer to the paths configured in `embedding.registry`.

## ONNX Runtime C++ SDK

The Python package `onnxruntime` is not enough for the C++ build. CMake needs headers and `libonnxruntime.so`.

Example CPU SDK layout:

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

Build with:

```bash
cmake -S . -B build \
  -DQORNIX_BUILD_RAG=ON \
  -DONNXRUNTIME_ROOT=/opt/onnxruntime \
  -DCMAKE_PREFIX_PATH=/opt/onnxruntime
cmake --build build -j$(nproc)
```

If configure prints `ONNX Runtime not found`, the app still works with TF-IDF fallback, but ONNX embeddings are disabled.

## Configuration Example

```yaml
embedding:
  backend: onnx
  active_model_id: local-semantic-v1
  models_dir: qornix_rag/models
  auto_discover_models: true
  validate_model_files: true
  persistent_cache_enabled: true
  enable_fallback: true
  chunk_token_margin: 2
  registry:
    local-semantic-v1:
      backend: onnx
      name: all-MiniLM-L6-v2 local ONNX
      version: v1
      model_path: qornix_rag/models/semantic_model.onnx
      tokenizer_path: qornix_rag/models/tokenizer.json
      tokenizer_type: WordPiece
      pooling: mean
      dimension: 384
      max_seq_len: 256
      onnx_threads: 2
      normalize_embeddings: true
      lowercase_tokens: true
      license: apache-2.0
      source: https://huggingface.co/Xenova/all-MiniLM-L6-v2
```

`model_id` / `active_model_id` is part of embedding cache safety. The model signature also includes backend, model path, tokenizer path, tokenizer type, pooling, dimension, normalization, casing and sequence length. If these change, persisted embeddings and vector snapshots are rejected/rebuilt instead of being silently reused.

## Diagnostics

After restart, check:

```bash
curl http://localhost:8081/api/embedding/models
curl http://localhost:8081/api/health
```

For generated `rag_app` projects, use `/api/rag/embedding/models` and `/api/rag/health`.

Keep `enable_fallback: true` while testing a new model so the application can continue with TF-IDF if ONNX Runtime or the model cannot be loaded.

## References

- Hugging Face models: https://huggingface.co/models
- Hugging Face Hub downloads: https://huggingface.co/docs/hub/en/models-downloading
- Optimum ONNX export: https://huggingface.co/docs/optimum-onnx/onnx/usage_guides/export_a_model
- ONNX Runtime install: https://onnxruntime.ai/docs/install/
- ONNX Runtime C++ guide: https://onnxruntime.ai/docs/get-started/with-cpp.html
