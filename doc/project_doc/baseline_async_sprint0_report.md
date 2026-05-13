# Qornix Async Sprint 0 Baseline Report

Дата: 2026-05-12
Baseline commit: `b828114`

## Окружение

- OS: Linux 6.17.0-20-generic x86_64 GNU/Linux
- Compiler: c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- CMake: 3.28.3
- Boost: 1.83.0
- CPU threads: 32
- `ulimit -n`: 1048576

## Сборка

Основная baseline-сборка:

```bash
cmake -S . -B build/async_baseline \
  -DCMAKE_BUILD_TYPE=Release \
  -DQORNIX_BUILD_EXAMPLES=ON \
  -DQORNIX_BUILD_TESTS=ON \
  -DQORNIX_BUILD_RAG=OFF
cmake --build build/async_baseline --parallel
```

Результат: успешно.

Проверенные цели:

- core: `qornix_web_core`, `qornix_web`
- examples: `dynamic_entity_server`, `dynamic_web_query_builder_server`, `schema_driven_backend`, `wiki_app`, `dynamic_web_query_builder_schema_roundtrip_test`
- qornix_orm: library + CTest suite
- qornix_dynamic_api: library + `dynamic_openapi_service_test`
- qornix_auth: проверен как зависимость root build и отдельно как standalone project

Standalone qornix_auth:

```bash
cmake -S qornix_auth -B build/async_baseline_auth -DCMAKE_BUILD_TYPE=Release
cmake --build build/async_baseline_auth --parallel
```

Результат: успешно, собраны `console_auth_example` и `web_auth_example`.

`qornix_rag` не включался в baseline-сборку: в root `CMakeLists.txt` он опциональный (`QORNIX_BUILD_RAG=OFF` по умолчанию), поэтому не входит в обязательный Sprint 0 baseline.

Зафиксированные предупреждения, не исправлялись в Sprint 0:

- `qornix_auth/include/password_hasher.h`: OpenSSL 3.0 deprecation warnings для `SHA256_Init`, `SHA256_Update`, `SHA256_Final`.
- Несколько ORM/example целей: повторное определение `QORNIX_ORM_SCHEMA_APP_XSD_PATH`.

## Baseline Tests

Команда:

```bash
ctest --test-dir build/async_baseline --output-on-failure
```

Результат: `19/19` tests passed.

Добавленный HTTP smoke-test покрывает:

- `/health`
- обычный route
- route с path params
- `404`
- keep-alive: два HTTP/1.1 запроса по одному TCP connection

## Baseline Benchmark

Команда:

```bash
scripts/baseline_benchmark.py \
  --server build/async_baseline/baseline_benchmark_server \
  --duration 3 \
  --concurrency 64 \
  --idle-connections 10000 \
  --idle-hold 3 \
  --idle-batch-size 500
```

Benchmark server:

- address: `127.0.0.1:44401`
- server threads: `32`
- scenario duration target: `3.0s`

| scenario | endpoint | concurrency | requests | errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | threads | fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fast | `/bench/fast` | 64 | 266486 | 0 | 88793.72 | 0.70 | 0.83 | 1.05 | 5740 | 33 | 71 |
| delay_10ms | `/bench/delay/10` | 64 | 9568 | 0 | 3171.27 | 20.15 | 20.39 | 20.55 | 5924 | 33 | 71 |
| delay_100ms | `/bench/delay/100` | 64 | 992 | 0 | 319.33 | 200.21 | 200.55 | 200.68 | 5932 | 33 | 71 |
| fast_1k_concurrent | `/bench/fast` | 1000 | 255262 | 0 | 84528.11 | 11.54 | 15.39 | 21.14 | 8208 | 33 | 1007 |

Idle keep-alive:

| requested | opened | failed | hold s | RSS KB | threads | fd |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10000 | 10000 | 0 | 3.00 | 28280 | 33 | 10007 |

Примечание: delay endpoints используют текущую sync-модель и блокируют `io_context` worker thread через `std::this_thread::sleep_for`. Поэтому при 64 concurrent и 32 server threads latency для 10 ms и 100 ms endpoint ожидаемо примерно удваивается.

## Definition of Done

- Проект собирается: да.
- Есть baseline tests: да.
- Есть baseline benchmark report: да.
- Дальнейшие async-изменения можно сравнивать с baseline: да.
