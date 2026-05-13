# Async DB Benchmark

This file is reserved for real PostgreSQL/MySQL async DB benchmark results.

Current status: the common async DB contracts, pool, cancellation, mock driver and ORM facade are implemented. PostgreSQL has a libpq non-blocking driver path and a smoke test gated by `QORNIX_ASYNC_POSTGRES_URL`; the live PostgreSQL smoke test has passed locally. MySQL now has a Boost.MySQL/Asio driver path and a smoke test gated by `QORNIX_ASYNC_MYSQL_URL` or `QORNIX_ASYNC_MYSQL_*` fields. Real PostgreSQL/MySQL benchmark numbers still must be added after running against live database servers.

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
- p50/p95/p99 query latency;
- HTTP latency;
- RSS/CPU/fd/thread count.
