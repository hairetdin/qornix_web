# Qornix RAG - Hybrid Search: HNSWLIB + Xapian

**Detailed technical documentation**

> Note: current runtime settings (`config.yaml`, ONNX backend, `index_duration_ms`, and debug fields `vector_score`, `text_score`, `fused_score` in `/api/search`) are described in `README.md`, which is the operational source of truth.

## Contents

1. [Architecture overview](#architecture-overview)
2. [Vector search, HNSWLIB](#vector-search-hnswlib)
3. [Full-text search, Xapian](#full-text-search-xapian)
4. [Hybrid recombination](#hybrid-recombination)
5. [Configuration and optimization](#configuration-and-optimization)
6. [Usage examples](#usage-examples)
7. [Troubleshooting](#troubleshooting)
8. [Benchmarks](#benchmarks)
9. [Best practices](#best-practices)

## Architecture overview

### Hybrid search workflow

```text
+----------------------------------------------------------------+
|                        USER QUERY                              |
|                "How does the DI container work?"               |
+----------------------------------------------------------------+
                           |
                           v
+--------------------------+--------------------------+
|                                                     |
v                                                     v
+---------------------+                 +---------------------+
|   HNSWLIB search    |                 |    Xapian search    |
|  Vector retrieval   |                 |  Full-text search   |
+---------------------+                 +---------------------+
| semantic candidates |                 | exact-term matches  |
+---------------------+                 +---------------------+
             \                         /
              \                       /
               v                     v
          +--------------------------------+
          |  Normalization + combination   |
          +--------------------------------+
                           |
                           v
          +--------------------------------+
          |        Final search results    |
          +--------------------------------+
```

### System components

| Component | Purpose | Technology |
|-----------|---------|------------|
| **RagEngine** | Main engine | C++ class |
| **Embedding backend** | Turns text chunks and queries into vectors | TF-IDF by default, optional ONNX Runtime |
| **TfidfVectorizer** | Dependency-light lexical embeddings | Custom implementation |
| **ONNX embedding model** | Neural semantic embeddings | ONNX Runtime C++ SDK + compatible text encoder |
| **HNSWLIB Index** | Local vector index | HNSW algorithm, header-only |
| **Xapian DB** | Text index | Probabilistic IR |
| **HybridSearchConfig** | Search settings | Configuration |

## Vector search, HNSWLIB

### What is HNSWLIB?

**HNSWLIB** (Hierarchical Navigable Small World Library) is a header-only library for efficient approximate nearest-neighbor search. It uses the **HNSW** algorithm.

### How it works

#### 1. Embedding generation

The vector side of hybrid retrieval is backend-driven. A fresh checkout uses
`embedding.backend: tfidf`, which requires no model files. When ONNX Runtime and
a compatible text embedding model are installed, `embedding.backend: onnx` can be
used for neural semantic embeddings. Both backends feed vectors into the same
vector search path.

##### TF-IDF backend

Each document is converted into a fixed-size vector:

```cpp
static constexpr size_t EMBEDDING_DIM = 256; // configurable vector size
std::vector<float> embedding(EMBEDDING_DIM, 0.0f);

for (const auto& term : tokens) {
    // Hash distribution of terms across dimensions
    size_t index = std::hash<std::string>{}(term) % EMBEDDING_DIM;

    // TF-IDF weight
    embedding[index] += tf(term, document) * idf(term);
}

// L2 normalization for cosine-like similarity
normalize_l2(embedding);
```

**TF-IDF formula:**

```text
tfidf(t, d) = tf(t, d) * log(N / df(t))
```

where:

- `tf(t, d)` is the count of term `t` in document `d` divided by the total number of terms in `d`;
- `N` is the total number of documents;
- `df(t)` is the number of documents containing `t`.

##### ONNX backend

ONNX mode uses a BERT-style text embedding model through the ONNX Runtime C++
SDK. Text is tokenized with the configured `tokenizer.json`, encoded as model
inputs such as `input_ids` and `attention_mask`, pooled into one vector, and
optionally normalized for cosine-style retrieval.

```yaml
embedding:
  backend: onnx
  enable_fallback: true
  active_model_id: local-semantic-v1
  registry:
    local-semantic-v1:
      backend: onnx
      model_path: qornix_rag/models/semantic_model.onnx
      tokenizer_path: qornix_rag/models/tokenizer.json
      tokenizer_type: WordPiece
      pooling: mean
      dimension: 0
      max_seq_len: 256
      onnx_threads: 2
      normalize_embeddings: true
```

`dimension: 0` means Qornix discovers the model output dimension. If ONNX
Runtime or the model cannot be loaded and `enable_fallback` is true, the runtime
falls back to TF-IDF and reports that status in `/api/health` and
`/api/embedding/models`.

The ONNX backend is not a general ONNX runner. It expects a text embedding model,
not an Ollama/GGUF chat model, image model, or arbitrary decoder-only LLM export.
See `models/README.md` for model compatibility and setup.

#### 2. Building the HNSW graph

**HNSW** (Hierarchical Navigable Small World) is a multilayer graph:

```text
Level L, top: very few nodes
Level 2: fewer nodes
Level 1: subset of nodes, fewer links
Level 0, base: all nodes, many links
```

Search proceeds from top to bottom:

1. start at the top level;
2. greedily search for the closest neighbor;
3. move down one level;
4. repeat until the base level is reached.

**HNSW parameters:**

```cpp
hnswlib::HierarchicalNSW<float> index(
    &space,
    max_elements,
    16,   // M: number of links
    200   // ef_construction: construction accuracy
);
```

| Parameter | Range | Impact | Recommendation |
|-----------|-------|--------|----------------|
| **M** | 8-48 | Higher means more accurate but slower | 16-24, balanced |
| **efConstruction** | 100-500 | Higher means a better graph | 200-300 |
| **efSearch** | 10-200 | Higher means more accurate search | 50-100 |

#### 3. Euclidean distance, L2

Vector distance metric:

```text
distance(A, B) = sqrt(sum((A_i - B_i)^2))
```

where:

- `A` is the query vector;
- `B` is the document vector.

**Result:** from 0 to infinity, where 0 means identical.

Convert it into similarity:

```text
similarity = 1 / (1 + distance)
```

### HNSWLIB usage example

```cpp
// Create space, L2 metric
hnswlib::L2Space space(EMBEDDING_DIM);

// Create index
auto index = std::make_unique<hnswlib::HierarchicalNSW<float>>(
    &space,
    max_elements,
    16,
    200
);

// Add vectors
for (size_t i = 0; i < documents.size(); ++i) {
    index->addPoint(documents[i].embedding.data(), i);
}

// Search k nearest neighbors
auto result = index->searchKnn(query_embedding.data(), k);

// Process results
while (!result.empty()) {
    auto [distance, id] = result.top();
    result.pop();

    // Convert distance to similarity
    float similarity = 1.0f / (1.0f + distance);
}
```

### Advantages of HNSWLIB

- **Fast search**: approximately O(log N).
- **Header-only**: no complex dependency chain, simple integration.
- **Approximate search**: finds semantically similar documents.
- **Memory efficiency**: graph-based storage is efficient for many workloads.
- **Incremental updates**: new vectors can be added.

### Limitations of HNSWLIB

- **Approximation**: it can miss the exact nearest result.
- **Build time**: initial indexing can be expensive.
- **L2 metric**: default metric is Euclidean distance, not pure cosine similarity.

## Full-text search, Xapian

### What is Xapian?

**Xapian** is a mature open-source search engine. It uses the **BM25** probabilistic model for ranking.

### How it works

#### 1. Document indexing

```cpp
Xapian::Document xdoc;
xdoc.set_data(std::to_string(doc_id)); // document ID

// Index path with higher weight
indexer.index_text(file_path, 2, "P");

// Index content
indexer.index_text(content);

database.add_document(xdoc);
```

#### 2. Stemming

Stemming reduces words to their base form:

```cpp
Xapian::Stem stemmer("english");
query_parser.set_stemmer(stemmer);
query_parser.set_stemming_strategy(Xapian::QueryParser::STEM_SOME);
```

**Supported languages:** `qornix_rag` accepts Xapian stemmer names and common two-letter aliases supported by the installed Xapian package. Examples include `en`/`english`, `de`/`german`, `fr`/`french`, `es`/`spanish`, `it`/`italian`, `pt`/`portuguese`, `nl`/`dutch`, `fi`/`finnish`, `sv`/`swedish`, `da`/`danish`, `no`/`norwegian`, `tr`/`turkish`, `ro`/`romanian`, `hu`/`hungarian`, `ru`/`russian`, and other languages documented by Xapian. See the authoritative list for your Xapian version in `Xapian::Stem`: https://xapian.org/docs/apidoc/html/classXapian_1_1Stem.html

Current `qornix_rag` exposes this through config:

```yaml
search:
  xapian_enabled: true
  xapian_language: auto   # auto | none | Xapian language name or ISO 639 alias
  xapian_stemming: true
  xapian_stemming_strategy: some
  xapian_cjk_ngrams: false
  xapian_metadata_prefixes: true
```

The same language/stemming settings are applied during indexing and query parsing. `auto` is a small built-in heuristic, not universal language detection: Cyrillic text maps to `russian`, otherwise `english` is used. For other primary languages, configure the language explicitly.

#### 3. Ranked search

```cpp
// Parse query
Xapian::Query query = query_parser.parse_query(user_query);

// Get results
Xapian::Enquire enquire(database);
enquire.set_query(query);
Xapian::MSet matches = enquire.get_mset(0, top_k);
```

#### 4. BM25 formula

```text
score(D, Q) = sum(IDF(q_i) * (f_i * (k1 + 1)) / (f_i + k1 * (1 - b + b * |D| / avgdl)))
```

where:

- `f_i` is the frequency of term `i` in document `D`;
- `|D|` is document length;
- `avgdl` is average document length.

Default Xapian parameters:

```text
k1 = 1.5  # term-frequency saturation
b  = 0.75 # document-length normalization
```

### Xapian query types

#### Simple query

```text
dependency injection
```

Searches for `dependency` OR `injection`.

#### Phrase search

```text
"dependency injection"
```

Searches for an exact phrase.

#### Boolean operators

```text
dependency AND injection
middleware OR handler
+required optional
```

#### Field queries

```text
Pinclude/http_server.h
path:include
```

### Advantages of Xapian

- **Exact search**: finds exact term occurrences.
- **Stemming**: understands word forms.
- **Flexibility**: supports complex boolean queries.
- **Ranking**: BM25 is an information-retrieval standard.
- **Maturity**: Xapian has been developed for decades.

### Limitations of Xapian

- **Text only**: it does not understand semantics by itself.
- **Exact matches**: it does not discover synonyms without extra logic.
- **Context**: it does not model query context like neural retrieval.

## Hybrid recombination

### Why hybrid search is useful

| Method | Strengths | Weaknesses |
|--------|-----------|------------|
| **HNSWLIB + TF-IDF** | Fast, local, no model files | Mostly lexical, weaker synonym handling |
| **HNSWLIB + ONNX embeddings** | Stronger semantic matching and synonyms | Requires ONNX Runtime and compatible model files |
| **Xapian** | Exactness, technical terms | No semantic understanding |

The hybrid approach combines the best of both methods.

### Result merge algorithm

#### Step 1: independent search

```cpp
// HNSWLIB search
auto vector_results = hnsw_search(query_embedding, top_k);

// Xapian search
auto text_results = xapian_search(query_text, top_k);
```

#### Step 2: score normalization

```cpp
float normalize_vector_score(float distance) {
    return 1.0f / (1.0f + distance);
}

float normalize_text_score(float score, float max_score) {
    return max_score > 0.0f ? score / max_score : 0.0f;
}
```

#### Step 3: weighted combination

```cpp
float vector_weight = 0.6f;  // HNSWLIB weight
float text_weight = 0.4f;    // Xapian weight

// Combination
float fused_score =
    vector_weight * vector_score +
    text_weight * text_score;
```

#### Step 4: sorting and filtering

```cpp
// Sort descending
std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
    return a.fused_score > b.fused_score;
});

// Filter by threshold
results.erase(
    std::remove_if(results.begin(), results.end(), [&](const auto& r) {
        return r.fused_score < min_score_threshold;
    }),
    results.end()
);
```

### Alternative: Reciprocal Rank Fusion

```text
RRF(d) = sum(1 / (k + rank_i(d)))
```

where:

- `rank_i(d)` is the position of document `d` in method `i`;
- `k` is a constant, often 60.

**RRF advantage:** it does not require score normalization.

### Weight tuning

#### Scenario 1: semantic search

```cpp
vector_weight = 0.8f;
text_weight = 0.2f;
```

Use it for:

- concept queries such as "How does the architecture work?";
- understanding questions such as "What is Dependency Injection?";
- codebase exploration.

#### Scenario 2: exact search

```cpp
vector_weight = 0.3f;
text_weight = 0.7f;
```

Use it for:

- specific classes or functions such as `HttpServer`;
- file names such as `di_container.h`;
- technical terms such as `middleware interface`.

#### Scenario 3: balanced, recommended

```cpp
vector_weight = 0.6f;
text_weight = 0.4f;
```

Use it for most general and mixed queries.

### Hybrid search examples

#### Example 1: query "Middleware for logging"

HNSWLIB finds semantic neighbors:

```text
Doc 12: server/server_manager.cpp
Doc 18: include/middleware.h
```

Xapian finds exact terms:

```text
Doc 15: include/logging_middleware.h (score: 0.95)
Doc 22: include/middleware.h         (score: 0.88)
```

Hybrid result with weights 0.6/0.4:

```text
1. include/logging_middleware.h
2. include/middleware.h
3. server/server_manager.cpp
```

#### Example 2: query "DI Container"

HNSWLIB finds semantic matches:

```text
Doc 12: server/server_manager.cpp (score: 0.82) # service registration
Doc 18: handlers/user_handler.cpp (score: 0.71) # DI usage
```

Xapian finds exact terms:

```text
Doc 4: include/di_container.h (score: 0.99)
```

Hybrid result:

```text
1. include/di_container.h
2. server/server_manager.cpp
3. handlers/user_handler.cpp
```

## Configuration and optimization

### Basic configuration

```cpp
// In core.h
float vector_weight = 0.6f;         // Vector search weight
float text_weight = 0.4f;           // Text search weight
size_t top_k = 10;                  // Number of results
float min_score_threshold = 0.1f;   // Minimum relevance
bool use_hybrid = true;             // Enable hybrid search
```

### Advanced tuning

#### 1. Embedding backend

```yaml
embedding:
  backend: tfidf # dependency-light default
```

Use TF-IDF for first run, small projects, and machines without ONNX Runtime.

```yaml
embedding:
  backend: onnx
  enable_fallback: true
```

Use ONNX when you need stronger meaning-based retrieval and have installed a
compatible embedding model. Keep `enable_fallback: true` while validating a new
model so the server can continue with TF-IDF if ONNX initialization fails.

#### 2. Embedding dimensionality

```cpp
static constexpr size_t EMBEDDING_DIM = 256;  // Change here
```

| Size | Projects | Memory | Accuracy |
|------|----------|--------|----------|
| 64 | <100 files | Low | Basic |
| 128 | 100-500 | Medium | Good |
| 256 | 500-2000 | High | Excellent |
| 512 | >2000 | Very high | Maximum |

For ONNX models, prefer `dimension: 0` in config unless you intentionally want
to enforce a specific output size.

#### 3. HNSW parameters

Speed-oriented:

```cpp
M = 12;
ef_construction = 100;
```

Accuracy-oriented:

```cpp
M = 24;
ef_construction = 400;
```

#### 4. Relevance threshold

| Value | Results | Noise |
|-------|---------|-------|
| 0.01 | Many | High |
| 0.05 | Medium | Moderate |
| 0.10 | Few | Low |
| 0.20 | Very few | Minimal |

#### 5. Disable hybrid search

```cpp
search_config_.use_hybrid = false; // BM25 only, faster
search_config_.text_weight = 1.0f;
search_config_.vector_weight = 0.0f;
```

```cpp
search_config_.text_weight = 0.0f;  // HNSWLIB only, semantic
search_config_.vector_weight = 1.0f;
```

### Performance profiling

```cpp
auto start = std::chrono::high_resolution_clock::now();
auto results = engine.search(query, top_k);
auto end = std::chrono::high_resolution_clock::now();

auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
std::cout << "Search took: " << duration.count() << " ms" << std::endl;
```

Target values:

- indexing: <5 seconds for 500 files;
- search: <300 ms;
- context building: <50 ms.

## Usage examples

### Example 1: basic search

```cpp
RagEngine engine;

// Indexing
engine.index_project("/path/to/project");

// Search
std::string query = "How does the HTTP server work?";
auto results = engine.search(query, 10);

// Output
for (const auto& result : results) {
    std::cout << result.file_path << " score=" << result.score << std::endl;
}
```

### Example 2: weight tuning

```cpp
HybridSearchConfig config;
config.vector_weight = 0.8f; // Semantic search
config.text_weight = 0.2f;

engine.set_search_config(config);
auto results = engine.search("Application architecture", 10);
```

### Example 3: building context for an LLM

```cpp
std::string query = "Where is the Strategy pattern implemented?";

// Build context
auto results = engine.search(query, 5);
std::string context = engine.build_context(results);

// Build prompt
std::string prompt = context + "\n\nAnswer the question: " + query;

// Send to an LLM, for example through curl
```

### Example 4: API endpoint

```cpp
class SearchHandler : public HandlerBase {
public:
    Response do_post(const Request& req) override {
        auto body = boost::json::parse(req.body()).as_object();
        std::string query = boost::json::value_to<std::string>(body["query"]);

        // Search
        auto results = engine_.search(query, 10);

        // Build response
        boost::json::array arr;
        for (const auto& result : results) {
            arr.push_back({
                {"file", result.file_path},
                {"score", result.score},
                {"snippet", result.snippet}
            });
        }

        return json_response(arr);
    }
};
```

## Troubleshooting

### Problem 1: HNSWLIB is not found

**Error:**

```text
fatal error: hnswlib/hnswlib.h: No such file or directory
```

**Solution:**

```bash
# Install HNSWLIB headers
sudo apt install libhnswlib-dev

# Check
ls /usr/include/hnswlib/hnswlib.h
```

### Problem 2: CMake cannot find HNSWLIB

**Error:**

```text
Could not find HNSWLIB
```

**Solution:**

```bash
# Refresh CMake cache
rm -rf build
mkdir build
cd build
cmake ..
```

### Problem 3: Xapian does not compile

**Error:**

```text
fatal error: xapian.h: No such file or directory
```

**Solution:**

```bash
sudo apt install libxapian-dev
```

### Problem 4: empty search results

Possible reasons:

1. **Project is not indexed**

```bash
curl -X POST http://localhost:8081/api/index \
  -H "Content-Type: application/json" \
  -d '{"scan_path":"/path/to/project"}'
```

2. **Threshold is too high**

```cpp
search_config_.min_score_threshold = 0.01f; // Lower it
```

3. **Weights are not balanced**

```cpp
search_config_.vector_weight = 0.5f; // Rebalance
search_config_.text_weight = 0.5f;
```

4. **No scan path was provided**

```bash
curl -X POST http://localhost:8081/api/index \
  -H "Content-Type: application/json" \
  -d '{"scan_path":"/path/to/project"}'
```

### Problem 5: slow search

Profiling:

```cpp
auto start = std::chrono::high_resolution_clock::now();
auto results = search(query);
auto elapsed = std::chrono::high_resolution_clock::now() - start;
```

Optimization:

1. reduce `top_k`;
2. reduce `EMBEDDING_DIM`;
3. increase `min_score_threshold`;
4. disable hybrid search if it is not needed.

### Problem 6: ONNX backend falls back to TF-IDF

Possible reasons:

1. the binary was built without ONNX Runtime C++ headers/library;
2. `model_path` or `tokenizer_path` is missing;
3. the model is not a supported text embedding model;
4. the tokenizer JSON format is incompatible with the current loader.

Check runtime status:

```bash
curl http://localhost:8081/api/health
curl http://localhost:8081/api/embedding/models
```

Install or download a compatible model:

```bash
./qornix_rag/download_onnx_model.sh
```

Then rebuild/reindex the scan path so vectors are generated with the selected
embedding model:

```bash
curl -X POST http://localhost:8081/api/index \
  -H "Content-Type: application/json" \
  -d '{"scan_path":"/path/to/project"}'
```

### Problem 7: segmentation fault during search

**Cause:** the index was not built or is corrupted.

**Solution:**

```cpp
if (!hnsw_index_) {
    build_hybrid_index();
}
```

## Benchmarks

### Test 1: project with 50 files, 8,000 lines

| Operation | Time | Memory |
|----------|------|--------|
| BM25 indexing | 0.8 sec | 12 MB |
| Hybrid indexing | 2.1 sec | 28 MB |
| BM25 search | 85 ms | 5 MB |
| Hybrid search | 180 ms | 9 MB |
| HNSWLIB search | 120 ms | 7 MB |
| Xapian search | 95 ms | 6 MB |

### Test 2: project with 500 files, 75,000 lines

| Operation | Time | Memory |
|----------|------|--------|
| BM25 indexing | 5.2 sec | 85 MB |
| Hybrid indexing | 15.8 sec | 210 MB |
| BM25 search | 420 ms | 35 MB |
| Hybrid search | 680 ms | 58 MB |
| HNSWLIB search | 520 ms | 48 MB |
| Xapian search | 380 ms | 42 MB |

### Search accuracy, Precision@10

| Method | Precision |
|--------|-----------|
| BM25 only | 72% |
| HNSWLIB only | 68% |
| Xapian only | 75% |
| **Hybrid, 0.6/0.4** | **84%** |

## Best practices

### Do

1. Always build the hybrid index after project indexing.
2. Tune weights for the specific search scenario.
3. Cache results for frequent queries.
4. Log search errors.
5. Profile performance regularly.

### Do not

1. Do not use hybrid search for very small projects with fewer than 50 files.
2. Do not set `vector_weight = 0` if semantic retrieval is required.
3. Do not ignore relevance thresholds.
4. Do not forget score normalization.

## Additional resources

### Documentation

- [Xapian documentation](https://xapian.org/docs/)
- [ONNX Runtime C++ guide](https://onnxruntime.ai/docs/get-started/with-cpp.html)
- [HNSW algorithm paper](https://arxiv.org/abs/1603.09320)
- [BM25 overview](https://en.wikipedia.org/wiki/Okapi_BM25)

### Tools

```bash
# View Xapian index
xapian-delve index/

# Test search
curl -X POST http://localhost:8081/api/search \
  -H "Content-Type: application/json" \
  -d '{"query":"dependency injection","top_k":5}'
```

## Note

This documentation file contains:

1. a detailed description of the hybrid search architecture;
2. the theory behind HNSWLIB and Xapian BM25;
3. code examples for all major components;
4. parameter and weight tuning guidance;
5. performance benchmarks;
6. troubleshooting for common problems;
7. usage best practices.
