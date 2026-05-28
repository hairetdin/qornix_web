# Knowledge Base — Qornix RAG

## 1. Overview

`qornix_rag` can be used as a **knowledge base** through `QASource`, a data source that stores and searches QA pairs.

This allows an application to:

- create a project knowledge base without depending on source code indexing;
- add, update and delete knowledge dynamically through the API;
- find answers by semantic similarity between questions;
- combine the knowledge base with code search in hybrid mode.

## 2. Quick Start

### 2.1. Configuration

Add the `rag.qa_kb` section to `config.yaml`:

```yaml
rag:
  enabled: true

  qa_kb:
    enabled: true
    name: "Project Knowledge Base"
    source_id: "prod_kb"
    pairs:
      - question: "How do I start the project?"
        answer: "Run: cmake -B build && cmake --build build && ./build/qornix_web"
        category: "setup"
        aliases: ["start", "run", "build"]
      - question: "What are the system requirements?"
        answer: "A C++20 compiler, Boost 1.83+, Xapian, libcurl and yaml-cpp"
        category: "requirements"
      - question: "How does the DI Container work?"
        answer: "The DI Container manages object lifetimes and dependencies"
        category: "architecture"
```

### 2.2. Run

```bash
./qornix_rag
```

### 2.3. Check

```bash
# Search the knowledge base
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "How do I start?", "top_k": 3}'

# Ask the LLM, if configured
curl -X POST http://localhost:8008/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "How do I start the project?", "top_k": 3}'
```

## 3. QA Pair Structure

### 3.1. Fields

```cpp
struct QAPair {
    std::string id;                      // Unique identifier
    std::string question;                // Question text
    std::string answer;                  // Answer text
    std::string category = "general";    // Category/tag
    std::vector<std::string> aliases;    // Alternative phrasings
    std::map<std::string, std::string> metadata; // Extra metadata
};
```

### 3.2. Examples

#### Basic QA Pair

```json
{
  "id": "rag_intro",
  "question": "What is RAG?",
  "answer": "RAG (Retrieval-Augmented Generation) is a pattern that combines knowledge-base retrieval with LLM answer generation.",
  "category": "concepts"
}
```

#### QA Pair With Aliases

```json
{
  "id": "deploy",
  "question": "How do I deploy the project?",
  "answer": "Use Docker Compose: docker-compose up -d",
  "category": "deployment",
  "aliases": ["deploy", "docker", "production"]
}
```

#### QA Pair With Metadata

```json
{
  "id": "cpp_version",
  "question": "Which C++ version is required?",
  "answer": "C++20 or newer (GCC 11+, Clang 12+, MSVC 19.2+)",
  "category": "requirements",
  "metadata": {
    "owner": "platform",
    "source": "README.md"
  }
}
```

## 4. Knowledge Base Management API

### 4.1. Add A QA Pair

```bash
curl -X POST http://localhost:8008/api/qa \
  -H "Content-Type: application/json" \
  -d '{
    "id": "logging",
    "question": "How do I configure logging?",
    "answer": "Edit the logging section in config.yaml",
    "category": "configuration",
    "aliases": ["log", "logging"]
  }'
```

**Response:**

```json
{
  "success": true,
  "id": "logging"
}
```

### 4.2. Update A QA Pair

```bash
curl -X PUT http://localhost:8008/api/qa/logging \
  -H "Content-Type: application/json" \
  -d '{
    "answer": "Updated answer with detailed instructions"
  }'
```

### 4.3. Delete A QA Pair

```bash
curl -X DELETE http://localhost:8008/api/qa/logging
```

### 4.4. List QA Pairs

```bash
# First page, 20 pairs
curl http://localhost:8008/api/qa

# Specific page
curl "http://localhost:8008/api/qa?page=2&limit=20"

# Specific source
curl "http://localhost:8008/api/qa?source_id=prod_kb"
```

**Response:**

```json
{
  "success": true,
  "total": 2,
  "items": [
    {
      "id": "start_project",
      "question": "How do I start the project?",
      "category": "setup",
      "aliases": ["start", "run"]
    },
    {
      "id": "requirements",
      "question": "What are the system requirements?",
      "category": "requirements",
      "aliases": ["requirements"]
    }
  ]
}
```

### 4.5. Add A Data Source

```bash
curl -X POST http://localhost:8008/api/sources \
  -H "Content-Type: application/json" \
  -d '{
    "type": "qa",
    "name": "Support FAQ",
    "source_id": "support_faq",
    "pairs": [
      {
        "id": "reset_password",
        "question": "How do I reset a password?",
        "answer": "Use /api/auth/reset-password",
        "category": "support"
      }
    ]
  }'
```

### 4.6. Remove A Source

```bash
curl -X DELETE http://localhost:8008/api/sources/support_faq
```

### 4.7. List Sources

```bash
curl http://localhost:8008/api/sources
```

**Response:**

```json
{
  "success": true,
  "sources": [
    {
      "id": "prod_kb",
      "name": "Project Knowledge Base",
      "type": "qa",
      "count": 12
    }
  ]
}
```

## 5. Use Cases

### 5.1. Technical Documentation

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "How do I add a new endpoint?"
        answer: "Create a handler in handlers/ and register the route in routes.h"
        category: "development"
        aliases: ["endpoint", "route"]

      - question: "How does middleware work?"
        answer: "Middleware runs before and after the handler. Use addMiddleware()"
        category: "architecture"
```

### 5.2. Support FAQ

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "How do I fix a compilation error?"
        answer: "Make sure all dependencies are installed: ./depend_install.sh"
        category: "support"
        aliases: ["compile", "build error"]

      - question: "Why does the server not start?"
        answer: "Check that port 8008 is free and config.yaml exists"
        category: "support"
        aliases: ["port", "startup"]
```

### 5.3. Developer Onboarding

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "How do I start working on the project?"
        answer: "1. Clone the repository\n2. Install dependencies\n3. Build the project\n4. Run tests"
        category: "onboarding"
        aliases: ["first steps", "setup"]

      - question: "What is the code review process?"
        answer: "Create a PR, get approval from 2+ maintainers, and pass CI"
        category: "process"
```

### 5.4. Hybrid Mode: Code And Knowledge Base

```yaml
rag:
  qa_kb:
    enabled: true
    pairs:
      - question: "Project architecture"
        answer: "qornix_web consists of ServerManager, HttpServer and DI Container"
        category: "architecture"

  indexing:
    enabled: true
    scan_path: "."
```

In this mode, RAG can search both curated QA entries and indexed project files.

## 6. Programmatic Usage

### 6.1. QASource

```cpp
#include "core.h"

using namespace qornix::rag;

// Create a knowledge base
auto qa_source = std::make_shared<QASource>("Project KB", "prod_kb");

// Add a QA pair
QAPair pair;
pair.id = "di";
pair.question = "What is DI?";
pair.answer = "Dependency Injection is a pattern for providing dependencies to objects";
pair.category = "architecture";
qa_source->addQAPair(pair);

// Search
auto docs = qa_source->getDocuments();

// Search by category
auto setup_pairs = qa_source->getByCategory("setup");

// Get all pairs
auto all_pairs = qa_source->getAllPairs();
```

### 6.2. RagEngine Integration

```cpp
RagEngine engine(config);

// Add a QA source
auto qa_source = std::make_shared<QASource>("Project KB", "prod_kb");
engine.addDataSource(qa_source);

// Add a filesystem source
auto fs_source = std::make_shared<FileSystemSource>(".", "code");
engine.addDataSource(fs_source);

// Index
engine.indexAllSources();

// Search all sources
auto results = engine.search("How does DI work?", 10);
```

## 7. Best Practices

### 7.1. Category Structure

Use a category hierarchy to organize knowledge:

```yaml
categories:
  - "setup"          # Installation and setup
  - "development"    # Development
  - "deployment"     # Deployment
  - "support"        # Support
  - "onboarding"     # Onboarding
  - "architecture"   # Architecture
  - "security"       # Security
  - "performance"    # Performance
```

### 7.2. Aliases

Add aliases for each question:

```json
{
  "question": "How do I deploy?",
  "aliases": ["deploy", "docker", "production"]
}
```

### 7.3. Metadata

Use metadata for tracking:

```json
{
  "metadata": {
    "owner": "platform",
    "source": "docs/deploy.md",
    "reviewed": "true"
  }
}
```

### 7.4. Regular Maintenance

- Review QA pairs regularly.
- Remove outdated pairs.
- Update versions, commands and links when behavior changes.

## 8. Limitations

1. **`findQAPair()` is exact-match lookup** - it searches an exact question or alias. Use `RagEngine::search()` for semantic search.
2. **Duplicate detection is not automatic in this API** - add and maintain pairs intentionally.
3. **No answer version history** - previous answer versions are not stored by this data source.
4. **Text only** - attachments should be indexed as documents through the document ingestion pipeline.
5. **One source should use one primary language** - this keeps lexical and semantic retrieval behavior easier to tune.

## 9. How Search Works

### 9.1. Full-Text Search With Xapian

During indexing, QA pairs are converted to documents:

```cpp
doc.content = "Question: " + pair.question + "\n\nAnswer: " + pair.answer;
```

The full content, including both question and answer, is indexed by Xapian. Keyword search over answer text works:

```bash
# If the answer contains "docker-compose", this query can find the QA pair
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "docker-compose", "top_k": 3}'
```

### 9.2. Semantic Search With HNSW/Embeddings

The same `doc.content` is used to generate the embedding vector. Meaning-based search also works even when the exact words differ:

```bash
# Finds a QA pair by meaning even without exact word overlap
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "how to restart the service", "top_k": 3}'
```

### 9.3. Hybrid Search: Xapian + HNSW

By default, RAG uses a weighted combination:

```yaml
rag:
  search:
    vector_weight: 0.7
    lexical_weight: 0.3
```

### 9.4. Exact Lookup By ID

`findQAPair(id)` provides fast access to a specific QA pair without search.

## 10. Examples

### 10.1. Complete `config.yaml` Example

```yaml
rag:
  enabled: true
  qa_kb:
    enabled: true
    name: "Operations FAQ"
    source_id: "ops_faq"
    pairs:
      - id: "restart_service"
        question: "How do I restart the service?"
        answer: "Run: systemctl restart qornix-web"
        category: "operations"
        aliases: ["restart", "reload"]

      - id: "find_logs"
        question: "Where are the logs?"
        answer: "Use logs/server.log or docker-compose logs -f"
        category: "operations"
        aliases: ["log", "logging"]
```

### 10.2. API Workflow

```bash
# 1. Add a QA pair
curl -X POST http://localhost:8008/api/qa \
  -H "Content-Type: application/json" \
  -d '{
    "id": "scale",
    "question": "How do I scale the service?",
    "answer": "Increase workers in config.yaml",
    "category": "operations"
  }'

# 2. Check the list
curl http://localhost:8008/api/qa

# 3. Update the answer
curl -X PUT http://localhost:8008/api/qa/scale \
  -H "Content-Type: application/json" \
  -d '{
    "answer": "Use Kubernetes: kubectl scale deployment..."
  }'

# 4. Search
curl -X POST http://localhost:8008/api/search \
  -H "Content-Type: application/json" \
  -d '{"query": "scaling", "top_k": 3}'

# 5. Ask with LLM
curl -X POST http://localhost:8008/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "How do I scale the service?", "top_k": 3}'
```
