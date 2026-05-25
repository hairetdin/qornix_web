# Embedding Models

You do not need to add anything here for the first run.

The generated application starts with lexical/TF-IDF retrieval:

```yaml
rag:
  embedding:
    backend: tfidf
```

That mode works without ONNX, without embedding model files, and is the safest default.

## What ONNX Adds

ONNX semantic retrieval lets the RAG engine compare meaning, not only matching words.
To use it, the app needs two files:

```text
models/
  semantic_model.onnx   # the neural embedding model
  tokenizer.json        # the tokenizer vocabulary used by that model
```

These files are not bundled by default because model size and licenses vary.

## Compatibility Requirements

Current `qornix_rag` ONNX support is intentionally narrow. Do not assume that every ONNX embedding model will work.

The current loader expects:

- a `tokenizer.json` file with `model.vocab`;
- special tokens such as `[UNK]`, `[CLS]`, `[SEP]`, `[PAD]` or compatible alternatives;
- an ONNX model that accepts BERT-style integer inputs:
  - `input_ids`;
  - `attention_mask`;
  - optionally `token_type_ids`;
- a float tensor output shaped either `[1, sequence_length, hidden_size]` or `[1, hidden_size]`.

Models with custom tokenizers, sentence-transformers pooling logic outside the ONNX graph, image inputs, or non-BERT input names may need conversion or code changes.

## How To Enable ONNX Retrieval

1. Get compatible model files.

   Recommended one-command setup from the generated app root:

   ```bash
   ./download_onnx_model.sh
   ```

   The script downloads the default model, writes:

   ```text
   models/semantic_model.onnx
   models/tokenizer.json
   ```

   and updates `config.yaml` to use `rag.embedding.backend: onnx`.

   Script options:

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

   Manual options are below.

   Option A: download an existing ONNX export from Hugging Face Hub.

   ```bash
   python3 -m pip install --user huggingface_hub
   huggingface-cli download <model-repo-id> \
     --include "*.onnx" "tokenizer.json" \
     --local-dir models/downloaded
   ```

   Then copy or rename the files:

   ```bash
   cp models/downloaded/*.onnx models/semantic_model.onnx
   cp models/downloaded/tokenizer.json models/tokenizer.json
   ```

   Option B: export a Hugging Face text encoder model yourself with Optimum.

   ```bash
   python3 -m pip install --user "optimum[onnxruntime]" transformers
   optimum-cli export onnx \
     --model <model-repo-id> \
     --task feature-extraction \
     models/exported
   cp models/exported/*.onnx models/semantic_model.onnx
   cp models/exported/tokenizer.json models/tokenizer.json
   ```

   Use a text encoder / embedding model. Do not use Ollama models, `.gguf` files,
   chat LLM weights, image models, or arbitrary ONNX files here.

   Official references:

   - Hugging Face Hub: https://huggingface.co/models
   - Optimum ONNX export: https://huggingface.co/docs/optimum/exporters/onnx/usage_guides/export_a_model
   - ONNX Runtime install: https://onnxruntime.ai/docs/install/

2. Build the app on a machine where ONNX Runtime development files are available.
   During CMake configure you should not see the warning:

   ```text
   ONNX Runtime not found. Build will use TF-IDF fallback.
   ```

3. Put compatible model files in this directory:

   ```text
   models/semantic_model.onnx
   models/tokenizer.json
   ```

4. Edit `config.yaml`:

   ```yaml
   rag:
     embedding:
       active_model_id: local-semantic-v1
       registry:
         local-semantic-v1:
           backend: onnx
           model_path: models/semantic_model.onnx
           tokenizer_path: models/tokenizer.json
           max_seq_len: 512
           onnx_threads: 2
           normalize_embeddings: true
           enable_fallback: true
           license: model-specific
           source: local
   ```

5. Restart the application.

6. Open the health endpoint:

   ```text
   http://127.0.0.1:8008/api/rag/health
   ```

   Check `rag.embedding_backend`. If ONNX cannot be initialized and fallback is enabled, the app continues with TF-IDF.

## Practical Advice

Keep `enable_fallback: true` while testing a new model. After you confirm that ONNX loads correctly, you can decide whether failures should stop startup by setting it to `false`.

If you are not sure which embedding model is compatible, leave `backend: tfidf` for now. A proper model download/conversion workflow is planned as a later production RAG feature.
