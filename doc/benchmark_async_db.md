# Async DB Benchmark

This file is reserved for real PostgreSQL/MySQL async DB benchmark results.

Current status: the common async DB contracts, hardened pool basics, cancellation, mock driver and ORM facade are implemented. PostgreSQL has a libpq non-blocking driver path and a smoke test gated by `QORNIX_ASYNC_POSTGRES_URL`; the live PostgreSQL smoke test has passed locally. MySQL now has a Boost.MySQL/Asio driver path and a smoke test gated by `QORNIX_ASYNC_MYSQL_URL` or `QORNIX_ASYNC_MYSQL_*` fields. Real PostgreSQL/MySQL benchmark numbers still must be added after running against live database servers.

## Required benchmark matrix

| scenario | expected result |
| --- | --- |
| postgres_smoke_query_prepare_tx | optional smoke test via `QORNIX_ASYNC_POSTGRES_URL` |
| postgres_normal_select_128 | 0 errors, bounded threads |
| postgres_normal_select_1000 | 0 errors, bounded threads |
| postgres_pool_overload_10000 | controlled rejects/timeouts |
| postgres_slow_query_timeout | timeout delta equals expected timeout responses |
| mysql_smoke_query_prepare_tx | optional smoke test via `QORNIX_ASYNC_MYSQL_URL` or explicit `QORNIX_ASYNC_MYSQL_*` fields |
| mysql_normal_select_128 | 0 errors, bounded threads |
| mysql_normal_select_1000 | 0 errors, bounded threads |
| mysql_pool_overload_10000 | controlled rejects/timeouts |
| mysql_slow_query_timeout | timeout delta equals expected timeout responses |
| sqlite_offloaded_baseline_128 | documented as sync/offloaded baseline |

## Required report fields

- driver;
- pool size;
- max waiters;
- query timeout;
- responses / errors / 2xx / 4xx / 5xx;
- DB success/error/timeout/cancel/reject deltas;
- timeout-source deltas: `query_option_timeouts`, `query_pool_default_timeouts`, `request_deadline_timeouts`, `acquire_timeouts`;
- connection created/closed/discarded/failed-connect deltas;
- p50/p95/p99 query latency;
- HTTP latency;
- RSS/CPU/fd/thread count.


## Phase 4/5 pool and timeout metrics now available

`AsyncDbMetricsSnapshot` now exposes pool lifecycle counters that benchmark scripts should capture before and after each run:

- `created_connections`;
- `closed_connections`;
- `discarded_connections`;
- `failed_connects`;
- `rejected_acquires`;
- `acquire_timeouts`;
- `query_option_timeouts`;
- `query_pool_default_timeouts`;
- `request_deadline_timeouts`;
- `query_latency_p50_us`;
- `query_latency_p95_us`;
- `query_latency_p99_us`.

These fields are sufficient to distinguish controlled overload behavior from leaked connections, unbounded queue growth, slow DB operations and HTTP/request-deadline cancellations in the future DB benchmark runner.
