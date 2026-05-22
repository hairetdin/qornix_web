# Qornix RAG project documentation index

This directory contains internal project planning and execution documents.

User-facing documentation should stay in files such as `qornix_rag/README.md`, quick-start guides, install guides, API documentation, and configuration guides. Planning notes, milestone execution plans, implementation checklists, and architecture decision drafts should be stored here. Backlog items related to the stabilization roadmap should be collected in `ROADMAP_STABILIZATION_AND_PRODUCT_PLAN.md` instead of separate `FUTURE_WORK_*` files.

## Documents

- `ROADMAP_STABILIZATION_AND_PRODUCT_PLAN.md` - stabilization and product roadmap for standalone RAG, reusable core, `qornix_web/templates` integration, and future production RAG expansion.
- `MILESTONE_0_PREP.md` - completed preparation notes and execution checklist for starting Milestone A.
- `MILESTONE_A1_STANDALONE_CONFIG.md` - implementation note for normalized standalone startup/config defaults, local-first bind behavior, and startup diagnostics.
- `ROADMAP_STABILIZATION_AND_PRODUCT_PLAN_changelog.md` - execution changelog for roadmap milestones and acceptance status.

## Documentation boundary

Use this split for future documents:

- user documentation: `README.md`, quick start, install/run instructions, API docs, config docs;
- project documentation: milestone plans, design notes, baseline audits, implementation checklists, and internal decision logs under `qornix_rag/doc/project_doc/`;
- roadmap backlog: keep deferred items for the current stabilization/product plan in `ROADMAP_STABILIZATION_AND_PRODUCT_PLAN.md`, not in separate `FUTURE_WORK_*` files.
