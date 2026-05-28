# qornix_rag Overview

This document provides short direct answers for users who index only the `qornix_rag/` directory.

## What is qornix_rag?

`qornix_rag` is a local-first RAG/wiki/search module for projects, documentation and document collections. It indexes files, uploaded documents and QA/wiki records, retrieves relevant chunks, shows sources and sends retrieved context to an LLM for answering user questions.

`qornix_rag` can run as a standalone application through `./qornix_rag/run.sh` or as an embedded module inside generated `qornix_web` applications through `--template rag_app` or `--with-rag`.

## What is qornix_rag for?

`qornix_rag` is for asking questions about a project, documentation, knowledge base or uploaded files and getting answers grounded in retrieved sources.

It provides local search, knowledge-base storage, document upload, hybrid retrieval, LLM answer generation, source citations, search diagnostics and QA/wiki persistence.

## What is RAG?

RAG means Retrieval-Augmented Generation. It is an approach where the system first retrieves relevant context from documents and then sends that context to an LLM to generate an answer.

In `qornix_rag`, the RAG workflow looks like this:

1. Files or QA/wiki records are added to the system.
2. Documents are split into chunks.
3. Chunks are indexed for lexical and vector search.
4. The user asks a question.
5. The system retrieves relevant sources.
6. The LLM receives the question and retrieved context.
7. The user gets an answer with source references.

## How does search work in qornix_rag?

`qornix_rag` uses hybrid retrieval: it combines lexical search through Xapian and vector search through embeddings. A lightweight TF-IDF backend is available by default. ONNX embeddings can be enabled for more semantic retrieval when a compatible embedding model is configured.

Directory indexing is optional. If `indexing.scan_path` or `--scan-path` is not set, the server starts without automatic directory scanning. Upload, API ingestion, QA/wiki and questions over already indexed data remain available.

## How is qornix_rag related to qornix_web?

`qornix_rag` lives inside the `qornix_web` repository, but it is a separate reusable module. `qornix_web` provides HTTP/UI/API infrastructure, while `qornix_rag` provides RAG functionality.

Standalone launch:

```bash
./qornix_rag/run.sh --port 8082
./qornix_rag/run.sh --port 8082 --scan-path /path/to/project
```

Generated application integration:

```bash
./create_new_project.sh ../my_rag_app --template rag_app
./create_new_project.sh ../my_api_rag_app --with-dynamic-api --with-rag
```

## Short answer

`qornix_rag` is a local module for search, knowledge bases and LLM answers over project documents. It indexes code and documents, retrieves relevant sources and answers user questions using project context.
