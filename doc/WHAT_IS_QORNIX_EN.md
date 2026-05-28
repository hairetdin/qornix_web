# What are qornix_web, qornix_orm, qornix_rag and RAG?

This document provides short canonical answers for search and RAG answers about the Qornix project.

## What is qornix_web?

`qornix_web` is a C++20 framework for building HTTP servers, REST APIs and web applications. It is built around Boost.Beast, Boost.URL, Boost.JSON, YAML configuration, routing, middleware, dependency injection, static files, templates and optional extension modules.

`qornix_web` is used to create separate C++ applications that link the framework through CMake. The repository includes `create_new_project.sh`, which generates ready-to-build projects from templates: a default web application, schema-driven Dynamic API, Vue/React/Angular frontend + backend applications, and RAG applications.

The main idea of `qornix_web` is that the user application stays in its own project while the framework provides server infrastructure, routing, configuration, extensions, ORM/Auth/RAG modules and build integration.

## What is qornix_web for?

`qornix_web` is for building native C++ backend and web applications where performance, runtime control, integration with C++ code and self-contained deployment matter.

Typical use cases:

- build an HTTP API in C++;
- build a web application with HTML templates and static assets;
- build a schema-driven Dynamic API from an XML schema and database metadata;
- build an application with a Vue, React or Angular frontend and a C++ backend;
- add optional modules such as `qornix_orm`, `qornix_auth` and `qornix_rag`;
- generate a standalone project with `create_new_project.sh` and develop it separately from the framework.

## What is qornix_orm?

`qornix_orm` is a C++20 library inside the `qornix_web` repository for database access, XML schema workflow, schema diff/plan/apply operations, QueryBuilder support and Dynamic API integration. It can be used as part of `qornix_web` applications or as a standalone CMake library through the `qornix::orm` target.

`qornix_orm` treats an XML schema as a declarative data-model contract. It can validate the schema, export a schema from an existing database, compare the desired model with the current database, classify schema-change risk, build a controlled plan, apply changes and record schema history.

## What is qornix_orm for?

`qornix_orm` is for applications that need database-backed behavior without writing every SQL query and every CRUD endpoint by hand.

Main tasks:

- describe an application data model in XML;
- validate the XML schema through XSD;
- export an XML schema from an existing database;
- compare the desired XML model with the current database state;
- classify schema-change risk;
- build SQL previews and controlled apply plans;
- run CRUD/query operations through ORM-like helpers and QueryBuilder;
- support SQLite, PostgreSQL and MySQL depending on enabled CMake options;
- provide sync DB helpers and an async DB facade for coroutine-based code.

## How are qornix_web and qornix_orm related?

`qornix_web` uses `qornix_orm` as an optional module for database-backed applications and the schema-driven Dynamic API. Dynamic API builds CRUD/query endpoints from metadata, XML schema and QueryBuilder, so an application can work with tables and fields without a dedicated handler for every entity.

`qornix_orm` does not need to start a web server. It can be linked independently:

```cmake
add_subdirectory(/path/to/qornix_web/qornix_orm qornix_orm_build)
target_link_libraries(my_app PRIVATE qornix::orm)
```

## What is qornix_rag?

`qornix_rag` is a local-first RAG/wiki/search module for projects, documentation and document collections. It indexes files, uploaded documents and QA/wiki records, retrieves relevant chunks, shows sources and sends retrieved context to an LLM for answering user questions.

`qornix_rag` can be used in two modes:

- as a standalone application from the `qornix_web` repository, launched with `./qornix_rag/run.sh`;
- as an embedded module inside a generated `qornix_web` application, for example through `--template rag_app` or `--with-rag`.

Standalone `qornix_rag` is intended for local single-user use: start the UI, upload files, choose an indexing path, reindex a project, search documents and ask LLM-backed questions.

Embedded `qornix_rag` is intended for applications based on `qornix_web`: the RAG UI is mounted at `/rag`, the API is usually mounted under `/api/rag/*`, and application data is stored in local `data/`, `models/`, `knowledge_base/` and upload directories.

## What is qornix_rag for?

`qornix_rag` is for asking questions about a project or document collection and getting answers grounded in retrieved sources.

Main tasks:

- index source code, Markdown, text, HTML, JSON/YAML/XML, CSV, PDF, DOCX, XLSX, PPTX and images through OCR;
- search a project and documents with hybrid retrieval: vector search plus Xapian lexical search;
- use TF-IDF embeddings by default or ONNX embeddings for more semantic retrieval;
- store QA/wiki records in SQLite;
- upload documents through UI or API;
- ask an LLM questions with retrieved context;
- show citations, sources, diagnostics, analytics, feedback and metrics.

## What is RAG?

RAG means Retrieval-Augmented Generation: generating an answer after first retrieving relevant knowledge.

In a normal LLM chat, the model answers from its weights and the prompt. In a RAG workflow, the system performs extra steps before generation:

1. Documents are split into chunks.
2. Chunks are indexed for search.
3. The user asks a question.
4. The system retrieves relevant chunks from the index.
5. The retrieved context is sent to the LLM together with the question.
6. The LLM answers using the provided context and source references.

RAG is useful when answers must be grounded in project documents, an internal knowledge base, source code, instructions, specifications or user-uploaded files.

## How are qornix_web and qornix_rag related?

`qornix_rag` is a separate module inside the `qornix_web` repository. It uses `qornix_web` server infrastructure for HTTP routes, UI, API and generated-application integration.

`qornix_web_core` does not have to depend on RAG. RAG is added optionally through the `qornix::rag_extension` CMake target or through the project generator:

```bash
./create_new_project.sh ../my_rag_app --template rag_app
./create_new_project.sh ../my_api_rag_app --with-dynamic-api --with-rag
```

Standalone launch:

```bash
./qornix_rag/run.sh --port 8082
./qornix_rag/run.sh --port 8082 --scan-path /path/to/project
```

If `--scan-path` is not provided, the server starts without automatic directory scanning. Upload, API ingestion, QA/wiki and questions over already indexed data remain available.

## Short answer

`qornix_web` is a C++20 web/backend framework and application generator.

`qornix_orm` is a C++20 library for schema-driven database work, XML schemas, QueryBuilder, sync/async DB APIs and Dynamic API integration.

`qornix_rag` is a module and standalone application for local search, wiki/QA knowledge bases and LLM answers over project documents or source code.

`RAG` is an approach where the system first retrieves relevant context from documents and then sends that context to an LLM so the answer is grounded in project sources.
