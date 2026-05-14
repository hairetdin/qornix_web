# qornix_orm standalone consumer smoke sample

This sample validates that `qornix_orm` can be consumed as a standalone CMake
library outside the top-level `qornix_web` build.

It uses:

- `add_subdirectory(<qornix_web>/qornix_orm ...)`;
- the exported `qornix::orm` target;
- the async DB facade;
- the mock async driver, so no PostgreSQL/MySQL service is required.

## Build and run

From the repository root:

```bash
cmake -S qornix_orm/examples/standalone_consumer \
  -B build/qornix_orm_standalone

cmake --build build/qornix_orm_standalone --parallel

./build/qornix_orm_standalone/qornix_orm_standalone_consumer
```

Expected output:

```text
qornix_orm standalone async smoke passed queries=<N> created_connections=<N>
```

The sample is intended as a quick standalone-consumer check for applications that
want to use `qornix_orm` without linking the top-level web demo executable.
