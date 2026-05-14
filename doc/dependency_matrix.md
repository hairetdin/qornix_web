# Dependency Matrix

This document summarizes the dependency baseline for building Qornix Web and the standalone `qornix_orm` library with the async HTTP and async DB APIs enabled.

## Baseline

| Dependency | Required for async branch | Notes |
| --- | --- | --- |
| C++ compiler | C++20-capable compiler | GCC 10+, Clang 10+ or newer compatible compiler. |
| CMake | 3.20+ | Root project and templates use C++20. |
| Boost | 1.83+ | Required baseline for Boost.URL, Boost.JSON, Boost.Asio and Boost.MySQL-capable async builds. |
| yaml-cpp | required | Unified project/template configuration. |
| OpenSSL | module-dependent | Auth and Boost.MySQL async TLS/link checks. |
| SQLite | optional ORM driver | Required for default self-contained ORM tests. |
| libpq | optional PostgreSQL driver | Required for sync PostgreSQL and async PostgreSQL live builds. |
| mysqlclient | optional sync MySQL driver | Required for sync MySQL driver. |
| Boost.MySQL headers | optional async MySQL driver | Required when `QORNIX_ENABLE_ASYNC_MYSQL=ON`. |
| pugixml/libxml2 | ORM schema workflow | XML schema parsing and validation support. |

## CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `QORNIX_BUILD_APP` | `ON` | Build root demo app. |
| `QORNIX_BUILD_EXAMPLES` | `OFF` | Build examples. |
| `QORNIX_BUILD_TESTS` | `ON` | Build root tests. |
| `QORNIX_ENABLE_ORM` | `ON` | Build and link `qornix_orm`. |
| `QORNIX_BUILD_DYNAMIC_API` | `ON` | Build Dynamic API module when ORM is enabled. |
| `QORNIX_BUILD_RAG` | `OFF` | Build optional RAG module. |
| `QORNIX_ENABLE_ASYNC_DB` | `ON` in ORM | Build async DB facade and common driver contracts. |
| `QORNIX_ENABLE_ASYNC_POSTGRES` | `OFF` | Build async PostgreSQL driver path. |
| `QORNIX_ENABLE_ASYNC_MYSQL` | `OFF` | Build async MySQL driver path. |

## Build verification commands

```bash
cmake -S . -B build/qornix-release   -DCMAKE_BUILD_TYPE=Release   -DQORNIX_BUILD_EXAMPLES=ON   -DQORNIX_BUILD_TESTS=ON   -DQORNIX_BUILD_RAG=OFF
cmake --build build/qornix-release --parallel
ctest --test-dir build/qornix-release --output-on-failure
```

For PostgreSQL/MySQL integration tests, enable the corresponding async backend flag and provide the DSN/environment variables documented in `qornix_orm/doc/testing.md`.
