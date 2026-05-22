# Migration From The Old Code Search UI

Older Qornix RAG builds exposed a code-search-first page. The stabilized standalone application is now a local RAG/wiki UI.

## What Changed

Old primary workflow:

```text
open UI -> search code snippets
```

Current primary workflow:

```text
open UI -> Ask -> retrieve local context -> call configured LLM -> show answer and sources
```

Search is still available, but it is now a secondary tool.

## Current UI Sections

- `Ask`: LLM-generated answers with retrieved context and sources.
- `Search`: ranked project and QA search results.
- `QA`: local wiki/QA management.
- `Sources / Health`: source list, index state, LLM diagnostics, and reindex action.

## API Changes To Notice

Use:

```text
POST /api/index
POST /api/search
POST /api/ask
GET  /api/health
GET  /api/sources
GET  /api/qa/list
POST /api/qa/add
POST /api/qa/update
POST /api/qa/delete
```

Do not use old `/api/reindex` examples. Reindexing is handled by `POST /api/index`.

## QA Knowledge Now Affects Retrieval

QA pairs are not just stored records. They participate in:

- Ask context;
- Search results;
- Sources/health visibility.

This lets local wiki entries fill gaps where project files do not contain enough explanation.

## LLM Fallback

If the configured LLM is unavailable, Ask returns a graceful fallback with relevant context and reports a non-`ok` `llm_status`. The UI shows the configured model, available models where possible, and concrete next steps such as pulling the missing Ollama model.

