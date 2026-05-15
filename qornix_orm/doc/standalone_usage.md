# qornix_orm Standalone Usage

`qornix_orm` can be used as a library without starting the full Qornix Web demo application. This is important for services that need only schema management, QueryBuilder, sync DB access or the async DB facade.

## Recommended layout

```text
workspace/
├── qornix_web/
└── my_service/
    ├── CMakeLists.txt
    └── main.cpp
```

## Minimal CMake integration

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QORNIX_WEB_ROOT /path/to/qornix_web CACHE PATH "Qornix Web checkout")
set(QORNIX_BUILD_APP OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QORNIX_ENABLE_ORM ON CACHE BOOL "" FORCE)
set(QORNIX_BUILD_DYNAMIC_API OFF CACHE BOOL "" FORCE)
set(QORNIX_BUILD_ORM_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${QORNIX_WEB_ROOT}" "${CMAKE_BINARY_DIR}/_deps/qornix_web")

add_executable(my_service main.cpp)
target_link_libraries(my_service PRIVATE qornix::orm)
```

## Sync-only standalone build

```bash
cmake -S . -B build   -DQORNIX_ENABLE_SQLITE=ON   -DQORNIX_ENABLE_POSTGRES=OFF   -DQORNIX_ENABLE_MYSQL=OFF   -DQORNIX_ENABLE_ASYNC_DB=OFF
cmake --build build --parallel
```

## Async DB standalone build

```bash
cmake -S . -B build   -DQORNIX_ENABLE_ASYNC_DB=ON   -DQORNIX_ENABLE_SQLITE=ON   -DQORNIX_ENABLE_ASYNC_POSTGRES=OFF   -DQORNIX_ENABLE_ASYNC_MYSQL=OFF
cmake --build build --parallel
```

For real PostgreSQL/MySQL async drivers, enable one of:

```bash
-DQORNIX_ENABLE_ASYNC_POSTGRES=ON
-DQORNIX_ENABLE_ASYNC_MYSQL=ON
```

See `async_db_api.md`, `configuration.md` and `testing.md` for API, config and validation details.
