# Knowledge Base

Put Markdown files here, then index the project from the RAG UI or with `POST /api/rag/index`.

## Базовые вопросы для RAG-приложения

Если это приложение создано из шаблона `rag_app`, его RAG-модуль основан на `qornix_rag`.

`qornix_rag` — это локальный RAG/wiki/search модуль для проектов, документации и коллекций документов. Он индексирует файлы, загруженные документы и QA/wiki записи, ищет релевантный контекст и передает его в LLM для ответа на вопросы пользователя.

RAG означает Retrieval-Augmented Generation. Это подход, при котором система сначала ищет релевантные фрагменты в документах или базе знаний, а затем передает найденный контекст в LLM. Благодаря этому ответ опирается на источники проекта, а не только на знания модели.

`qornix_web` — это C++20 web/backend фреймворк, на котором построено generated-приложение. Он дает HTTP-сервер, маршрутизацию, конфигурацию, static/templates, middleware и возможность подключать модули вроде `qornix_rag`.

`qornix_orm` — это C++20 библиотека для schema-driven работы с базами данных, XML-схемами, QueryBuilder, sync/async DB API и Dynamic API. Она используется в `qornix_web` как опциональный модуль для database-backed приложений.

`rag_app` — это готовый шаблон приложения `qornix_web` со встроенным RAG UI на `/rag` и RAG API под `/api/rag/*`.

## Basic Questions For RAG Applications

If this application was created from the `rag_app` template, its RAG module is based on `qornix_rag`.

`qornix_rag` is a local-first RAG/wiki/search module for projects, documentation and document collections. It indexes files, uploaded documents and QA/wiki records, retrieves relevant context and sends that context to an LLM for answering user questions.

RAG means Retrieval-Augmented Generation. It is an approach where the system first retrieves relevant chunks from documents or a knowledge base and then sends the retrieved context to an LLM. This grounds the answer in project sources instead of only the model's built-in knowledge.

`qornix_web` is the C++20 web/backend framework used by the generated application. It provides the HTTP server, routing, configuration, static/templates, middleware and optional modules such as `qornix_rag`.

`qornix_orm` is a C++20 library for schema-driven database work, XML schemas, QueryBuilder, sync/async DB APIs and Dynamic API. It is used by `qornix_web` as an optional module for database-backed applications.

`rag_app` is a ready-to-build `qornix_web` application template with embedded RAG UI at `/rag` and RAG API under `/api/rag/*`.
