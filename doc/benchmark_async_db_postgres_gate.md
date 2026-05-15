# Async DB Benchmark

- generated at: `2026-05-14T12:43:21+05:00`
- server: `build/async-gate-postgres/async_db_benchmark_server`
- address: `127.0.0.1:46797`
- driver: `postgres`
- server threads: `32`
- pool size: `32`
- max waiters: `1024`
- query timeout: `2000ms`
- acquire timeout: `1000ms`
- requested nofile limit: `10576`
- active nofile soft/hard: `10576` / `1048576`

Counters marked `delta` are per-scenario increments from `/db/metrics`.
`errors` counts every non-2xx response; `unexpected` counts responses outside the scenario's expected status set.

| scenario | endpoint | concurrency | responses | errors | unexpected | 2xx | 4xx | 5xx | 503 | 504 | client errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | CPU % | threads | fd | DB ok delta | DB err delta | acquire timeout delta | query timeout delta | cancel delta | reject delta | discard delta | failed connect delta | prepared hit delta | prepared miss delta | expected statuses | expected |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| postgres_normal_select_128 | `/db/select` | 128 | 22770 | 0 | 0 | 22770 | 0 | 0 | 0 | 0 | 0 | 2263.71 | 55.25 | 64.16 | 67.62 | 27176 | 201.02 | 33 | 167 | 22770 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, bounded threads |
| postgres_normal_select_1000 | `/db/select` | 1000 | 19523 | 0 | 0 | 19523 | 0 | 0 | 0 | 0 | 0 | 1857.42 | 537.54 | 553.73 | 559.41 | 35956 | 2086.62 | 33 | 1039 | 19523 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, bounded threads |
| postgres_prepared_select_128 | `/db/prepared` | 128 | 22589 | 0 | 0 | 22589 | 0 | 0 | 0 | 0 | 0 | 2246.20 | 57.05 | 63.25 | 66.00 | 35616 | 199.67 | 33 | 167 | 22589 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 22557 | 32 | 200 | 0 errors, prepared cache warms then hits |
| postgres_transaction_rollback_128 | `/db/transaction` | 128 | 22398 | 0 | 0 | 22398 | 0 | 0 | 0 | 0 | 0 | 2226.03 | 57.38 | 64.31 | 66.07 | 34576 | 219.54 | 33 | 167 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, rollback path succeeds |
| postgres_slow_query_timeout | `/db/slow` | 128 | 2122 | 2122 | 0 | 0 | 0 | 2122 | 0 | 2122 | 0 | 199.71 | 644.26 | 653.41 | 661.97 | 35572 | 126.02 | 33 | 148 | 0 | 0 | 0 | 2122 | 0 | 0 | 2122 | 0 | 0 | 0 | 504 | 504 responses counted as query timeouts |
| postgres_pool_overload_10000 | `/db/select` | 10000 | 257527 | 252240 | 0 | 5287 | 0 | 252240 | 252240 | 0 | 0 | 24580.85 | 374.30 | 456.67 | 1392.04 | 74056 | 2212.52 | 33 | 10039 | 5287 | 252240 | 5079 | 0 | 0 | 247161 | 0 | 0 | 0 | 0 | 200/503/504 | controlled 503/504 responses, no unbounded waiters |

## Validation

All scenarios matched their expected HTTP status sets.

## Final DB Metrics

| metric | value |
| --- | ---: |
| `active_connections` | 0 |
| `idle_connections` | 32 |
| `queued_waiters` | 0 |
| `rejected_acquires` | 247161 |
| `acquire_timeouts` | 5079 |
| `created_connections` | 2154 |
| `closed_connections` | 0 |
| `discarded_connections` | 2122 |
| `failed_connects` | 0 |
| `query_total` | 324531 |
| `query_success` | 70169 |
| `query_error` | 252240 |
| `query_timeout` | 2122 |
| `query_cancelled` | 0 |
| `query_option_timeouts` | 2122 |
| `query_pool_default_timeouts` | 0 |
| `request_deadline_timeouts` | 0 |
| `prepared_cache_hits` | 22557 |
| `prepared_cache_misses` | 32 |
| `prepared_cache_evictions` | 0 |
| `query_latency_p50_us` | 689333 |
| `query_latency_p95_us` | 880154 |
| `query_latency_p99_us` | 892874 |

## Command

```bash
scripts/db_benchmark.py --server build/async-gate-postgres/async_db_benchmark_server --driver postgres --duration 10 --normal-concurrency 128 --high-concurrency 1000 --overload-concurrency 10000 --pool-size 32 --max-waiters 1024 --extended --output doc/benchmark_async_db_postgres_gate.md
```
