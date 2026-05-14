# Async DB Benchmark

- generated at: `2026-05-14T07:27:38+05:00`
- server: `build/async-db-perf/async_db_benchmark_server`
- address: `127.0.0.1:60611`
- driver: `postgres`
- server threads: `32`
- pool size: `32`
- max waiters: `1024`
- query timeout: `2000ms`
- acquire timeout: `1000ms`

Counters marked `delta` are per-scenario increments from `/db/metrics`.
`errors` counts every non-2xx response; `unexpected` counts responses outside the scenario's expected status set.

| scenario | endpoint | concurrency | responses | errors | unexpected | 2xx | 4xx | 5xx | 503 | 504 | client errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | CPU % | threads | fd | DB ok delta | DB err delta | acquire timeout delta | query timeout delta | cancel delta | reject delta | discard delta | failed connect delta | prepared hit delta | prepared miss delta | expected statuses | expected |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| postgres_normal_select_128 | `/db/select` | 128 | 10375 | 0 | 0 | 10375 | 0 | 0 | 0 | 0 | 0 | 2049.38 | 61.06 | 70.49 | 130.68 | 26296 | 372.94 | 33 | 167 | 10375 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, bounded threads |
| postgres_normal_select_1000 | `/db/select` | 1000 | 7369 | 0 | 0 | 7369 | 0 | 0 | 0 | 0 | 0 | 1326.08 | 774.81 | 815.25 | 829.59 | 35832 | 1705.96 | 33 | 1039 | 7369 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, bounded threads |
| postgres_prepared_select_128 | `/db/prepared` | 128 | 10057 | 0 | 0 | 10057 | 0 | 0 | 0 | 0 | 0 | 1991.58 | 64.28 | 72.43 | 77.71 | 35648 | 632.90 | 33 | 167 | 10057 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 10025 | 32 | 200 | 0 errors, prepared cache warms then hits |
| postgres_transaction_rollback_128 | `/db/transaction` | 128 | 9831 | 0 | 0 | 9831 | 0 | 0 | 0 | 0 | 0 | 1943.32 | 66.10 | 73.80 | 76.87 | 35020 | 642.43 | 33 | 167 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 200 | 0 errors, rollback path succeeds |
| postgres_slow_query_timeout | `/db/slow` | 128 | 1087 | 1087 | 0 | 0 | 0 | 1087 | 0 | 1087 | 0 | 192.78 | 670.47 | 684.23 | 687.64 | 35900 | 200.23 | 33 | 147 | 0 | 0 | 0 | 1087 | 0 | 0 | 1087 | 0 | 0 | 0 | 504 | 504 responses counted as query timeouts |
| postgres_pool_overload_10000 | `/db/select` | 10000 | 45574 | 44550 | 0 | 1024 | 0 | 44550 | 43494 | 1056 | 0 | 6768.02 | 611.66 | 2664.80 | 2913.81 | 77440 | 2462.53 | 33 | 10039 | 1024 | 43494 | 2048 | 1056 | 0 | 41446 | 0 | 32 | 0 | 0 | 200/503/504 | controlled 503/504 responses, no unbounded waiters |

## Validation

All scenarios matched their expected HTTP status sets.

## Final DB Metrics

| metric | value |
| --- | ---: |
| `active_connections` | 0 |
| `idle_connections` | 8 |
| `queued_waiters` | 0 |
| `rejected_acquires` | 41446 |
| `acquire_timeouts` | 2048 |
| `created_connections` | 1095 |
| `closed_connections` | 0 |
| `discarded_connections` | 1087 |
| `failed_connects` | 32 |
| `query_total` | 74462 |
| `query_success` | 28825 |
| `query_error` | 43494 |
| `query_timeout` | 2143 |
| `query_cancelled` | 0 |
| `query_option_timeouts` | 1087 |
| `query_pool_default_timeouts` | 0 |
| `request_deadline_timeouts` | 1056 |
| `prepared_cache_hits` | 10025 |
| `prepared_cache_misses` | 32 |
| `prepared_cache_evictions` | 0 |
| `query_latency_p50_us` | 660301 |
| `query_latency_p95_us` | 883706 |
| `query_latency_p99_us` | 903447 |

## Command

```bash
scripts/db_benchmark.py --server build/async-db-perf/async_db_benchmark_server --driver postgres --extended
```
