# Schema history

Schema history records what happened when a schema plan is applied or dry-run.

Code:

```text
qornix_orm/core/schema_history.h
qornix_orm/core/schema_history.cpp
```

The first implementation is an append-only JSON Lines store. This keeps the audit trail in `qornix_orm`, independent of any UI or HTTP application layer.

## Stored information

A schema history record can include:

- record id;
- timestamp;
- plan id;
- XML schema format version;
- desired schema hash;
- current schema hash before apply;
- current schema hash after apply;
- user/actor;
- success flag;
- dry-run flag;
- SQL preview;
- plan JSON;
- apply result JSON;
- result message.

## Minimal example

```cpp
#include "core/schema_history.h"

SchemaHistoryStore history("schema_history.jsonl");

SchemaHistoryAppendResult saved = history.append(
    plan,
    applyResult,
    "admin",
    "desired_hash",
    "before_hash",
    "after_hash"
);

if (!saved.success) {
    std::cerr << saved.message << std::endl;
}
```

Read all records:

```cpp
std::vector<SchemaHistoryRecord> records = history.readAll();
```

## Design rule

Schema history belongs to ORM core. UI and dynamic API layers should read history from this core service instead of inventing a separate audit mechanism.

