# Qornix RAG - Retrieval-Augmented Generation for C++ projects

**Qornix RAG** is a C++ codebase intelligence and search system. It uses a hybrid approach: TF-IDF vectorization, a vector search backend, and **Xapian** full-text search to improve the accuracy of retrieving relevant documents.

The current runtime configuration is controlled through `config.yaml`. Depending on build/runtime availability, semantic embeddings can be provided by ONNX Runtime, with TF-IDF fallback when enabled.

## Contents

- [Description](#description)
- [Features](#features)
- [Quick start](#quick-start)
- [Installation and build](#installation-and-build)
- [Run](#run)
- [Usage](#usage)
- [Architecture](#architecture)
- [Algorithms](#algorithms)
- [Examples](#examples)
- [Configuration](#configuration)
- [Performance](#performance)
- [LLM integration](#llm-integration)
- [FAQ](#faq)
- [Extension](#extension)
- [Contributing](#contributing)
- [Contacts](#contacts)
- [Conclusion](#conclusion)

## Description

Qornix RAG solves the problem of finding information quickly in large codebases.

### Problems without RAG

- Manual file search is slow.
- It is difficult to find where a feature is implemented.
- Understanding project architecture takes time.
- Context is easily lost while working with code.

### Solution with RAG

- Instant search across the whole project.
- Retrieval of relevant code fragments.
- Automatic context construction.
- Integration with LLM models.

## Features

- **Pure C++20 implementation**: no Python dependency for the core service.
- **Hybrid search**: combination of vector and text search.
- **TF-IDF vectorization**: document representation as vectors.
- **HNSW-style nearest-neighbor search**: fast approximate retrieval where the vector backend is enabled.
- **Full-text search**: Xapian search engine with stemming.
- **Result recombination**: weighted score fusion from multiple retrieval methods.
- **Web interface**: convenient UI for search.
- **REST API**: integration with other systems.
- **Fast indexing**: optimized project indexing flow.
- **Language support**: C, C++, Java, Python, JavaScript, TypeScript, Go and Rust.
- **Cross-platform design**: Linux, macOS and Windows-oriented codebase.
- **Lightweight dependency profile**: Boost, Xapian and optional ONNX Runtime/vector backend.

## Quick start

### 1. Build, one command

```bash
./build_app.sh
```

Or manually:

```bash
mkdir -p build
cd build
cmake .. -DQORNIX_BUILD_RAG=ON
cmake --build . --target qornix_rag
```

### 2. Run

```bash
./qornix_rag --project /path/to/project
```

### 3. Open in a browser

```text
http://localhost:8081
```

Done. You can now search the project code.

## Installation and build

### Requirements

- **Compiler**: GCC 10+ or Clang 10+ with C++20 support.
- **CMake**: 3.10+.
- **Boost libraries**: system, filesystem, url and json, version 1.75+.
- **Xapian**: full-text search engine.
- **Optional vector backend**: HNSW/NMSLIB-style vector retrieval depending on the configured build.
- **Optional ONNX Runtime**: semantic embedding backend.
- **OS**: Linux, macOS or Windows.

### Install dependencies, Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y build-essential cmake libboost-all-dev libxapian-dev
```

### Install HNSWLIB or another vector backend

Use the package manager or the source installation path supported by your target environment. Header-only backends should be installed where CMake can locate their headers.

### Check installation

```bash
cmake --version
g++ --version
xapian-config --version
```

### Install dependencies, macOS

```bash
brew install cmake boost xapian
```

### Install dependencies, Windows

```powershell
# Through vcpkg
vcpkg install boost xapian
```

### Step-by-step build

```bash
# 1. Go to the project directory
cd qornix_web

# 2. Create a build directory
mkdir -p build
cd build

# 3. Run CMake
cmake .. -DQORNIX_BUILD_RAG=ON

# 4. Build the project
cmake --build . --target qornix_rag

# 5. Check the binary
./qornix_rag --help
```

## Run

### Basic run

```bash
./qornix_rag --project /path/to/project
```

By default, the server starts on `http://localhost:8081`.

### Command-line options

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--port` | `-p` | HTTP server port | `8081` |
| `--address` | `-a` | Server address | `0.0.0.0` |
| `--project` | `-P` | Project path to index | `.` current directory |
| `--config` | `-c` | Path to `config.yaml` | `./config.yaml` |
| `--help` | `-h` | Show help | - |

### Usage examples

#### Example 1: run on port 3000

```bash
./qornix_rag --port 3000
```

#### Example 2: index a specific project

```bash
./qornix_rag --project ~/dev/qornix_web
```

#### Example 3: run on localhost only

```bash
./qornix_rag --address 127.0.0.1
```

#### Example 4: full configuration

```bash
./qornix_rag --address 127.0.0.1 --port 8081 --project ~/dev/qornix_web --config ./config.yaml
```

## Usage

### Web interface

1. **Open the browser**

   ```text
   http://localhost:8081
   ```

2. **Reindex the project**, if needed

   Press the reindex button and wait for completion.

3. **Enter a query**

   Examples:

   - "How does Dependency Injection work?"
   - "HTTP server architecture"
   - "Middleware request processing"
   - "Where is the HandlerBase class defined?"

4. **Inspect results**

   Results are sorted by relevance. Code snippets and line numbers are shown where available.

### Search from the console, curl

#### Get project statistics

```bash
curl http://localhost:8081/api/stats | jq
```

Example response:

```json
{
  "indexed_files": 128,
  "indexed_bytes": 540000,
  "index_duration_ms": 820,
  "embedding_backend": "tfidf",
  "onnx_status": "disabled"
}
```

#### Run a search

```bash
curl -X POST http://localhost:8081/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "How does the DI container work?", "top_k": 5}' | jq
```

Example response:

```json
{
  "query": "How does the DI container work?",
  "results": [
    {
      "file": "include/di_container.h",
      "score": 0.92,
      "vector_score": 0.84,
      "text_score": 0.98,
      "fused_score": 0.90,
      "snippet": "class DIContainer ...",
      "line": 12
    }
  ]
}
```

#### Reindex a project

```bash
curl -X POST http://localhost:8081/api/reindex | jq
```

## API

### `GET /api/stats`

Returns project statistics.

**Response:**

```json
{
  "indexed_files": 128,
  "indexed_bytes": 540000,
  "index_duration_ms": 820,
  "embedding_backend": "tfidf",
  "onnx_status": "disabled"
}
```

### `POST /api/reindex`

Indexes the project.

**Request:**

```bash
curl -X POST http://localhost:8081/api/reindex
```

**Response:**

```json
{
  "status": "ok",
  "indexed_files": 128,
  "index_duration_ms": 820
}
```

### `POST /api/search`

Searches the project.

**Request:**

```json
{
  "query": "How does Dependency Injection work?",
  "top_k": 5
}
```

**Response:**

```json
{
  "query": "How does Dependency Injection work?",
  "results": [
    {
      "file": "include/di_container.h",
      "score": 0.92,
      "vector_score": 0.84,
      "text_score": 0.98,
      "fused_score": 0.90,
      "snippet": "...",
      "line": 12
    }
  ]
}
```

## Architecture

### Project structure

```text
qornix_rag/
├── core.h                      # RAG system core
│   ├── RagEngine               # Main engine
│   │   ├── index_project()     # Project indexing
│   │   ├── generate_tfidf_embedding() # Vector generation
│   │   ├── build_hybrid_index()       # Vector + Xapian index construction
│   │   ├── hybrid_search()            # Hybrid search
│   │   └── search()                   # Search, hybrid or BM25
│   ├── TfidfVectorizer         # TF-IDF vectorizer
│   ├── Document                # Document structure with embedding
│   ├── SearchResult            # Search result
│   └── ProjectStats            # Project statistics
├── web.h                       # Web interface
│   ├── RagApiHandler           # API handlers
│   ├── RagWebHandler           # Web page
│   └── setupRagRoutes          # Route setup
├── templates/
│   └── rag_interface.html      # HTML interface
├── main.cpp                    # Entry point
├── CMakeLists.txt              # Build configuration
├── run.sh                      # Quick run script
└── README.md                   # Documentation
```

### System components

#### `RagEngine`

Main system class with hybrid search:

```cpp
class RagEngine {
public:
    void index_project(const std::string& path);   // Indexing
    std::vector<float> generate_tfidf_embedding(const std::string& text); // Embeddings
    std::vector<SearchResult> search(const std::string& query, size_t top_k); // Search
    std::string build_context(const std::vector<SearchResult>& results); // Context
};
```

Internal state includes:

```cpp
std::vector<Document> documents_;                  // Documents with embeddings
std::unique_ptr<nmslib::Index<float>> nmslib_index_; // Vector index, when enabled
std::unique_ptr<Xapian::WritableDatabase> xapian_db_; // Text index
HybridSearchConfig search_config_;                 // Search settings
```

#### `TfidfVectorizer`

Vectorization and relevance calculation:

```cpp
class TfidfVectorizer {
public:
    std::vector<std::string> tokenize(const std::string& text);
    std::vector<float> transform(const std::string& text);
    float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);
};
```

#### `Document`

Document structure:

```cpp
struct Document {
    std::string path;
    std::string content;
    std::vector<float> embedding;
    size_t line_count;
};
```

## Algorithms

### Hybrid search

The system uses two independent search methods followed by result recombination.

#### 1. Vector search

**Document representation:**

- each document is converted into a TF-IDF vector of size 256;
- vectors are L2-normalized for cosine-like similarity;
- terms are hash-distributed across dimensions.

**HNSW-style algorithm:**

Parameters:

```text
M = 16                 # Number of links per node
efConstruction = 200   # Candidate list size during construction
Cosine similarity      # Similarity metric where supported
```

Advantages:

- logarithmic search complexity, approximately O(log N);
- high approximate-search accuracy;
- scalability for large collections.

#### 2. Full-text search, Xapian

Capabilities:

- stemming;
- weighted indexing, for example file path with x2 weight;
- support for complex queries.

**BM25 formula:**

```text
score(D, Q) = sum(IDF(q_i) * (f_i * (k1 + 1)) / (f_i + k1 * (1 - b + b * |D| / avgdl)))
```

where:

- `f_i` is term frequency in the document;
- `|D|` is document length;
- `avgdl` is average document length;
- `k1=1.5`, `b=0.75` are tuning parameters.

#### 3. Result recombination

```text
fused_score = w_vector * vector_score + w_text * text_score
```

where:

- `w_vector = 0.6`, vector-search weight;
- `w_text = 0.4`, text-search weight.

Score normalization:

```text
vector_score = 1 / (1 + distance)
text_score = raw_bm25_score / max_bm25_score
```

### Tokenization

1. Split text into tokens by whitespace and punctuation.
2. Convert to lowercase.
3. Remove stop words, both Russian and English sets may be configured.
4. Filter short tokens, fewer than 3 characters.

Example stop words:

```text
English: the, and, is, of, to, in...
Russian: i, v, ne, chto, on, na...
```

## Examples

### Example 1: search project architecture

**Query:**

```text
Tell me about the HTTP server structure
```

**Expected matches:**

- `include/http_server.h` - main class;
- `server/http_server.cpp` - implementation;
- `include/handler_base.h` - base handler class.

**Result:**

```text
Files found: 5
Relevance: 0.85
File: include/http_server.h
```

### Example 2: search DI implementation

**Query:**

```text
Where is Dependency Injection used?
```

**Expected matches:**

- `include/di_container.h` - container class;
- `server/server_manager.cpp` - service registration;
- `handlers/user_handler.cpp` - DI usage.

**Result:**

```text
Files found: 3
Relevance: 0.92
File: server/server_manager.cpp
```

### Example 3: search middleware

**Query:**

```text
Middleware for logging
```

**Expected matches:**

- `include/logging_middleware.h` - implementation;
- `include/middleware.h` - base class;
- `server/server_manager.cpp` - attachment.

## Configuration

### Hybrid search settings

In `core.h`, structure `HybridSearchConfig`:

```cpp
float vector_weight = 0.6f;       // Vector search weight, 0.0-1.0
float text_weight = 0.4f;         // Text search weight, 0.0-1.0
size_t top_k = 10;                // Number of results
float min_score_threshold = 0.1f; // Minimum relevance
bool use_hybrid = true;           // Enable hybrid search
```

Weight recommendations:

| Scenario | Vector weight | Text weight | Comment |
|----------|---------------|-------------|---------|
| Semantic search | 0.8 | 0.2 | Search by meaning, not only keywords |
| Exact search | 0.3 | 0.7 | Search for specific terms and names |
| Balanced | 0.6 | 0.4 | Recommended default |
| Vector only | 1.0 | 0.0 | Semantics only |
| Text only | 0.0 | 1.0 | Keywords only |

### NMSLIB/HNSW parameter settings

In `build_hybrid_index()`:

```cpp
{"M", 16},              // 10-50, higher means more accurate but slower
{"efConstruction", 200} // 100-500, higher means better graph quality
```

Recommendations:

- `M = 8-12` - fast search, lower accuracy;
- `M = 16-24` - balanced, recommended;
- `M = 32-48` - high accuracy, more memory;
- `efConstruction = 100-200` - fast build;
- `efConstruction = 300-500` - high-quality graph, recommended for accuracy.

### Embedding dimensionality

In the `RagEngine` class:

```cpp
static constexpr size_t EMBEDDING_DIM = 256;  // Change the value
```

Recommendations:

- `64` - small projects, fewer than 100 files, memory saving;
- `128` - medium projects, 100-500 files;
- `256` - large projects, more than 500 files, recommended;
- `512+` - very large projects, high detail.

### Disable hybrid search

For BM25 only:

```cpp
search_config_.use_hybrid = false;  // BM25-only search
```

### Change relevance threshold

In `core/rag_core.h`, method `search()`:

```cpp
if (score > 0.05) {  // Change the value
```

Recommendations:

- `0.01` - very low, more results including noise;
- `0.05` - standard, balanced;
- `0.10` - high, only most relevant results.

### Change context size

In `core/rag_core.h`, method `build_context()`:

```cpp
size_t max_length = 8000) {  // Change this
```

Recommendations:

- `4000` - small models, around 7B parameters;
- `8000` - standard, balanced;
- `16000` - larger models, 14B+ parameters.

### Add new file types

In `core/rag_core.h`:

```cpp
".java", ".py", ".js", ".ts",  // Add
".go", ".rs", ".swift", ".kt"   // Add
```

### Configure stop words

In `TfidfVectorizer`:

```cpp
std::unordered_set<std::string> stop_words = {
    "the", "and", "is", "of", "to", "in"
    // Add custom words
};
```

## Performance

### Benchmark for a project of about 50 files, 8,000 lines

| Operation | Time | Memory |
|----------|------|--------|
| Indexing, BM25 | 0.5-1.0 sec | 15 MB |
| Indexing, Hybrid | 1.5-2.5 sec | 25 MB |
| Search, BM25 | 50-150 ms | 5 MB |
| Search, Hybrid | 100-250 ms | 8 MB |
| Context building | 10-30 ms | 2 MB |

### Benchmark for a project of about 200 files, 30,000 lines

| Operation | Time | Memory |
|----------|------|--------|
| Indexing, BM25 | 2-3 sec | 45 MB |
| Indexing, Hybrid | 5-8 sec | 80 MB |
| Search, BM25 | 150-300 ms | 12 MB |
| Search, Hybrid | 300-500 ms | 20 MB |
| Context building | 30-50 ms | 5 MB |

**Note:** hybrid search requires more resources, but can provide 30-50% more accurate results for mixed semantic and exact-term queries.

## LLM integration

### Example: context generation for Ollama/Qwen

```cpp
#include "rag_core.h"

int main() {
    // Create engine
    RagEngine engine;
    engine.index_project("/path/to/project");

    // Search and build context
    std::string query = "How does Dependency Injection work?";
    auto results = engine.search(query, 5);
    std::string context = engine.build_context(results);

    // Build prompt for the model
    std::string prompt = context + "\n\nAnswer the question: " + query;

    // Send to Ollama through curl or an HTTP client
}
```

## FAQ

### Q: How often should I reindex the project?

**A:** Reindex after every significant codebase change. This can be automated through git hooks.

### Q: Does it work with languages other than C++?

**A:** Yes. Supported languages include C, Java, Python, JavaScript, TypeScript, Go, Rust, Swift and Kotlin.

### Q: What is the maximum project size?

**A:** Up to about 1,000 files is recommended by default. For larger projects, increase `max_context_length` and tune indexing settings.

### Q: Can it be used without the web UI?

**A:** Yes. Use the REST API directly through curl or integrate `RagEngine` into your own code.

### Q: How can search quality be improved?

**A:** Tune the relevance threshold and add domain-specific stop words.

### Q: How much memory is required?

**A:** About 10-20 MB for projects up to 100 files and about 50-100 MB for projects up to 500 files, depending on configuration.

### Q: Which is better, hybrid search or BM25 only?

**A:** Hybrid search is often more accurate for mixed semantic/exact queries, but requires more resources. For small projects with fewer than 100 files, BM25-only mode can be enough.

## Extension

### Adding semantic search

The current project version already supports semantic mode through `config.yaml`:

```yaml
embedding:
  backend: onnx
  model_path: qornix_rag/models/model.onnx
  tokenizer_path: qornix_rag/models/tokenizer.json
  enable_fallback: true
```

If ONNX Runtime is unavailable at build or runtime, the engine automatically falls back to TF-IDF when `enable_fallback = true`.

### Installing ONNX Runtime

#### Option 1: package installation, if available in your OS

Make sure these files are present after installation:

- header: `/usr/local/include/onnxruntime/onnxruntime_cxx_api.h`, or `/usr/include/...`;
- library: `/usr/local/lib/libonnxruntime.so`, or a system library path.

#### Option 2: build from source, verified scenario

```bash
git clone https://github.com/microsoft/onnxruntime.git
cd onnxruntime
./build.sh --config Release --build_shared_lib --parallel
sudo cp build/Linux/Release/libonnxruntime.so /usr/local/lib/
sudo cp -r include/onnxruntime/core/session /usr/local/include/onnxruntime
sudo ldconfig
```

After installation:

```bash
ldconfig -p | grep onnxruntime
```

If the linker cannot find the library:

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### Where to get models and which models fit

Recommended sources:

- Hugging Face Hub, sentence-transformers, bge and e5 models in ONNX format;
- ONNX Community models on Hugging Face.

Minimum model requirements for the current implementation:

- format: `*.onnx`;
- inputs: `input_ids`, `attention_mask`, and optionally `token_type_ids`;
- output: embedding tensor, `[1, seq, hidden]` or `[1, hidden]`;
- a compatible `tokenizer.json` must be stored next to the model.

Important: quality depends strongly on matching `tokenizer.json` to the exact model. Do not mix a tokenizer from one model with an ONNX file from another model.

Example model classes:

- `intfloat/multilingual-e5-small`, for RU/EN queries;
- `BAAI/bge-small-en-v1.5`, for English code/text queries;
- `sentence-transformers/all-MiniLM-L6-v2`, fast baseline option.

### Connecting a model in the project

1. Put files into `qornix_rag/models/`:

   ```text
   qornix_rag/models/model.onnx
   qornix_rag/models/tokenizer.json
   ```

2. Set paths in the `embedding` section of `config.yaml`.
3. Restart the server and check `GET /api/stats`:

   - `embedding_backend` should show the selected backend;
   - `onnx_status` should not contain parsing/loading errors.

### Important indexing details

- The `qornix_rag/models` directory is excluded from indexing.
- Large files are skipped according to `indexing.max_file_size_kb`, default `512`.
- `GET /api/stats` returns the latest indexing duration as `stats.index_duration_ms`.
- `POST /api/search` exposes debug fields: `vector_score`, `text_score`, `fused_score`.

Example indexing block in `config.yaml`:

```yaml
indexing:
  max_file_size_kb: 512
  exclude_dirs:
    - .git
    - build
    - qornix_rag/models
```

Integrate neural embeddings through ONNX Runtime:

```cpp
// Load embedding model
OnnxEmbeddingModel model(config.embedding.model_path, config.embedding.tokenizer_path);

// Compute embedding
auto embedding = model.encode("How does dependency injection work?");

// Semantic search
engine.search_by_embedding(embedding, 10);
```

### Adding a vector store

Use FAISS or another vector database for fast search if the local vector index is not enough:

```cpp
faiss::IndexFlatL2 index(dimension);

// Add vectors
index.add(n, vectors.data());

// Search
index.search(query_count, query_vectors.data(), top_k, distances.data(), labels.data());
```

## Contributing

Pull requests are welcome.

### How to contribute

1. Fork the repository.
2. Create a branch: `git checkout -b feature/amazing-feature`.
3. Commit your changes: `git commit -m 'Add amazing feature'`.
4. Push the branch: `git push origin feature/amazing-feature`.
5. Open a Pull Request.

## Contacts

Questions and suggestions: https://github.com/hairetdin

## Conclusion

**Qornix RAG** is a tool for working with C++ codebases. It helps you:

- quickly find the information you need;
- understand project architecture;
- integrate with LLM models;
- scale to different project sizes.

Start using it now.

## Hybrid approach diagram

```text
+----------------------------------------------------------------+
| STAGE 1: FINE-TUNING                                           |
|                                                                |
| +-------------------+         +-------------------+            |
| | Project code      |   +     | Base model        |            |
| | - All files       |         | - General knowledge|           |
| | - Architecture    |         | - C++ patterns     |           |
| | - Patterns        |         | - Syntax           |           |
| +-------------------+         +-------------------+            |
|                                                                |
|                 LoRA adapters                                  |
|                 additional model weights                       |
|                                                                |
| Result: the fine-tuned model remembers the architecture.        |
+----------------------------------------------------------------+

+----------------------------------------------------------------+
| STAGE 2: RAG + INFERENCE                                       |
|                                                                |
| User question                                                   |
|       |                                                        |
|       v                                                        |
| +----------------------+                                       |
| | RAG search           |                                       |
| | - Finds relevant files|                                      |
| | - Only on-topic data |                                       |
| +----------------------+                                       |
|       |                                                        |
|       v                                                        |
| +----------------------+                                       |
| | Fine-tuned model     |                                       |
| | 1. Sees RAG context  |                                       |
| |    concrete files    |                                       |
| | 2. Recalls architecture through fine-tuning                  |
| | 3. Generates answer  |                                       |
| +----------------------+                                       |
|                                                                |
| Final answer with context and project knowledge.               |
+----------------------------------------------------------------+
```
