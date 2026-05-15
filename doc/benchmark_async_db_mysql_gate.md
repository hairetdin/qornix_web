# Async DB Benchmark

- generated at: `2026-05-14T14:05:04+05:00`
- server: `build/async-gate-mysql/async_db_benchmark_server`
- address: `127.0.0.1:46777`
- driver: `mysql`
- server threads: `32`
- pool size: `32`
- max waiters: `1024`
- query timeout: `2000ms`
- acquire timeout: `1000ms`
- unexpected sample limit: `12`
- body preview bytes: `500`
- requested nofile limit: `10576`
- active nofile soft/hard: `10576` / `1048576`

Counters marked `delta` are per-scenario increments from `/db/metrics`.
`errors` counts every non-2xx response; `unexpected` counts responses outside the scenario's expected status set.

| scenario | endpoint | concurrency | responses | errors | unexpected | 2xx | 4xx | 5xx | 503 | 504 | client errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | CPU % | threads | fd | DB ok delta | DB err delta | acquire timeout delta | query timeout delta | cancel delta | reject delta | discard delta | failed connect delta | prepared hit delta | prepared miss delta | expected statuses | expected |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| mysql_normal_select_128 | `/db/select` | 128 | 7820 | 0 | 0 | 7820 | 0 | 0 | 0 | 0 | 0 | 769.83 | 166.08 | 168.03 | 168.22 | 16524 | 79.84 | 34 | 167 | 7820 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, bounded threads |
| mysql_normal_select_1000 | `/db/select` | 1000 | 10550 | 2037 | 0 | 8513 | 0 | 2037 | 2037 | 0 | 0 | 955.77 | 1030.93 | 1037.82 | 1039.96 | 27512 | 1994.70 | 34 | 1039 | 8513 | 2037 | 2037 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200/503 | 200 plus controlled 503 pool timeouts allowed under documented high-concurrency saturation |
| mysql_prepared_select_128 | `/db/prepared` | 128 | 21799 | 0 | 0 | 21799 | 0 | 0 | 0 | 0 | 0 | 2168.40 | 59.22 | 65.25 | 67.43 | 27356 | 202.13 | 34 | 167 | 21799 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 21767 | 32 | 200 | 0 errors, prepared cache warms then hits |
| mysql_transaction_rollback_128 | `/db/transaction` | 128 | 7812 | 0 | 0 | 7812 | 0 | 0 | 0 | 0 | 0 | 768.34 | 165.98 | 168.97 | 171.01 | 26724 | 98.45 | 34 | 167 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, rollback path succeeds |
| mysql_slow_query_timeout | `/db/slow` | 128 | 1376 | 1376 | 0 | 0 | 0 | 1376 | 0 | 1376 | 0 | 127.03 | 1003.70 | 1005.75 | 1029.11 | 26616 | 52.16 | 34 | 167 | 0 | 0 | 0 | 1376 | 0 | 0 | 1376 | 0 | 0 | 0 | 504 | 504 responses counted as query timeouts |
| mysql_pool_overload_10000 | `/db/select` | 10000 | 250417 | 246477 | 0 | 3940 | 0 | 246477 | 246477 | 0 | 0 | 22953.24 | 320.29 | 471.86 | 1390.14 | 63952 | 2141.09 | 34 | 10039 | 3940 | 246477 | 6055 | 0 | 0 | 240422 | 0 | 0 | 0 | 0 | 200/503/504 | controlled 503/504 responses, no unbounded waiters |

## Validation

All scenarios matched their expected HTTP status sets.

## Status breakdown

| scenario | observed statuses |
| --- | --- |
| mysql_normal_select_128 | 200=7820 |
| mysql_normal_select_1000 | 200=8513, 503=2037 |
| mysql_prepared_select_128 | 200=21799 |
| mysql_transaction_rollback_128 | 200=7812 |
| mysql_slow_query_timeout | 504=1376 |
| mysql_pool_overload_10000 | 200=3940, 503=246477 |

## Final DB Metrics

| metric | value |
| --- | ---: |
| `active_connections` | 0 |
| `idle_connections` | 32 |
| `queued_waiters` | 0 |
| `rejected_acquires` | 240422 |
| `acquire_timeouts` | 8092 |
| `created_connections` | 1408 |
| `closed_connections` | 0 |
| `discarded_connections` | 1376 |
| `failed_connects` | 0 |
| `query_total` | 291962 |
| `query_success` | 42072 |
| `query_error` | 248514 |
| `query_timeout` | 1376 |
| `query_cancelled` | 0 |
| `query_option_timeouts` | 1376 |
| `query_pool_default_timeouts` | 0 |
| `request_deadline_timeouts` | 0 |
| `prepared_cache_hits` | 21767 |
| `prepared_cache_misses` | 32 |
| `prepared_cache_evictions` | 0 |
| `query_latency_p50_us` | 960414 |
| `query_latency_p95_us` | 1001111 |
| `query_latency_p99_us` | 1018213 |

## Command

```bash
scripts/db_benchmark.py --server build/async-gate-mysql/async_db_benchmark_server --driver mysql --duration 10 --normal-concurrency 128 --high-concurrency 1000 --overload-concurrency 10000 --pool-size 32 --max-waiters 1024 --extended --output doc/benchmark_async_db_mysql_gate.md
```
