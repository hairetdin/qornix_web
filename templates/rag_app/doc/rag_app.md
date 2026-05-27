# RAG Application Template

This template is for a full Qornix Web application that hosts the reusable `qornix_rag` module.

Runtime ownership:

- Qornix Web owns server startup, bind address, logging, and application config.
- `qornix_rag` owns RAG indexing, search, Ask, QA storage, and RAG UI/API routes.
- Dedicated generated RAG apps expose the UI at `/` and `/rag`, with APIs under `/api/rag/*`.

The generated application should be configured through its own `config.yaml`, not through the standalone `qornix_rag/config.yaml`.

Operational guidance for generated apps is in `doc/operations.md`.
