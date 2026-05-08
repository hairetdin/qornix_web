# Qornix Schema-driven Backend Showcase

This showcase demonstrates the central Qornix Web feature:

```text
XML schema -> validation -> diff -> plan -> SQL preview -> apply -> dynamic CRUD/query API
```

## Run

```bash
mkdir -p build
cd build
cmake -DQORNIX_BUILD_EXAMPLES=ON ..
cmake --build . --target schema_driven_backend
./example/schema_driven_backend/schema_driven_backend
```

Open:

```text
/schema-manager
/api/dynamic/openapi.json
/api/dynamic/meta/tables
```
