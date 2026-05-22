# Embedding Models

This directory is reserved for optional ONNX semantic retrieval model files.

The public Git repository does not include large model binaries. A fresh clone is expected to work without files in this directory because `qornix_rag/config.yaml` uses:

```yaml
embedding:
  backend: tfidf
  enable_fallback: true
```

That mode uses lexical/TF-IDF retrieval and does not require ONNX model files.

## What Files Are Needed For ONNX

To enable ONNX semantic retrieval, this directory should contain:

```text
qornix_rag/models/
  semantic_model.onnx
  tokenizer.json
```

The current ONNX loader is not a universal model runner. It expects a compatible BERT-style text embedding model:

- `tokenizer.json` contains `model.vocab`;
- special tokens such as `[UNK]`, `[CLS]`, `[SEP]`, `[PAD]` or compatible alternatives are available;
- the ONNX model accepts integer inputs such as `input_ids`, `attention_mask`, and optionally `token_type_ids`;
- the first output is a float tensor shaped `[1, sequence_length, hidden_size]` or `[1, hidden_size]`.

Do not use Ollama models, `.gguf` files, chat LLM weights, image models, or arbitrary ONNX files here.

## Where To Get The Files

Recommended one-command setup from the repository root:

```bash
./qornix_rag/download_onnx_model.sh
```

The script downloads the default model, writes:

```text
qornix_rag/models/semantic_model.onnx
qornix_rag/models/tokenizer.json
```

and updates `qornix_rag/config.yaml` to use `embedding.backend: onnx`.

Script options:

```bash
./qornix_rag/download_onnx_model.sh --help
./qornix_rag/download_onnx_model.sh --dry-run
./qornix_rag/download_onnx_model.sh --force
./qornix_rag/download_onnx_model.sh --no-config-update
```

The default download source is:

```text
Hugging Face repo: Xenova/all-MiniLM-L6-v2
Model file:        onnx/model.onnx
Tokenizer file:    tokenizer.json
License:           apache-2.0
```

Manual options are below.

Option A: download an existing ONNX export from Hugging Face Hub:

```bash
python3 -m pip install --user huggingface_hub
huggingface-cli download <model-repo-id> \
  --include "*.onnx" "tokenizer.json" \
  --local-dir qornix_rag/models/downloaded
cp qornix_rag/models/downloaded/*.onnx qornix_rag/models/semantic_model.onnx
cp qornix_rag/models/downloaded/tokenizer.json qornix_rag/models/tokenizer.json
```

Option B: export a Hugging Face text encoder model yourself:

```bash
python3 -m pip install --user "optimum[onnxruntime]" transformers
optimum-cli export onnx \
  --model <model-repo-id> \
  --task feature-extraction \
  qornix_rag/models/exported
cp qornix_rag/models/exported/*.onnx qornix_rag/models/semantic_model.onnx
cp qornix_rag/models/exported/tokenizer.json qornix_rag/models/tokenizer.json
```

Official references:

- Hugging Face Hub: https://huggingface.co/models
- Optimum ONNX export: https://huggingface.co/docs/optimum/exporters/onnx/usage_guides/export_a_model
- ONNX Runtime install: https://onnxruntime.ai/docs/install/

## Enable ONNX In Config

After adding compatible files, edit `qornix_rag/config.yaml`:

```yaml
embedding:
  backend: onnx
  model_path: qornix_rag/models/semantic_model.onnx
  tokenizer_path: qornix_rag/models/tokenizer.json
  max_seq_len: 256
  onnx_threads: 2
  normalize_embeddings: true
  enable_fallback: true
```

Restart:

```bash
./qornix_rag/run.sh
```

Then check:

```text
http://localhost:8081/api/health
```

Keep `enable_fallback: true` while testing a new model. If ONNX Runtime or the model cannot be loaded, the app can continue with TF-IDF retrieval.
