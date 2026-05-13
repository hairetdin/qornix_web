# Changelog

## 2026-05-13 - Async HTTP runtime improvements

- Added coroutine-friendly HTTP controls: route timeout, read/write timeout, cancellation tracking and late-response suppression.
- Added overload protection: global active request limit, per-route concurrency limit and request body size limits.
- Added async middleware support through `add_async_middleware(...)` while keeping the existing sync middleware API compatible.
- Added observability helpers: request metrics, latency percentiles, structured access logs and `/qornix/metrics` endpoint.
- Added graceful shutdown support for stopping accept, waiting for active requests and stopping the `io_context` after the deadline.
- Added `qornix::async::BlockingTaskPool` for offloading blocking database or external API calls away from `io_context` threads.
- Added `qornix::async::AsyncDbPool` facade for awaitable DB-style examples and migration toward real async database drivers.
- Extended the benchmark server with async sleep, DB-pool-limited, blocking-offload and metrics endpoints.
- Updated `scripts/baseline_benchmark.py` to write the latest performance report to `doc/benchmark.md` by default.

### Benchmark report updates

- Changed `/bench/delay/10` and `/bench/delay/100` in the benchmark server to use async timers instead of blocking worker threads.
- Split the DB benchmark into normal-load and overload scenarios.
- Updated the benchmark report with `2xx/4xx/5xx/client errors` columns and per-scenario rejected/timeout metric deltas.

### Reference performance result

The latest checked-in benchmark report is stored in `doc/benchmark.md`. It was generated with the full high-concurrency benchmark command from the README using 32 server threads, 10 second scenarios, `--db-normal-concurrency 128`, `--db-concurrency 10000` and `--idle-connections 10000`.

Highlights from the reference run:

- Fast route: `8799.87` RPS at 256 concurrency with `0` errors.
- Async 100 ms delay route: `2465.51` RPS, p50 `102.81 ms`, p95 `104.57 ms`, `0` errors.
- 10k active `/health` load: `6963.31` RPS with `0` errors.
- 10k active async sleep load: `3970.39` RPS with `0` errors.
- Normal DB-pool scenario at 128 concurrency: `6430.50` RPS, p50 `17.13 ms`, p95 `33.23 ms`, `0` errors.
- 10k idle keep-alive: `10000/10000` connections opened, `0` failures, `65` threads, about `75 MB` RSS.
- Overload and timeout scenarios are represented as controlled failures with rejected/timeout deltas in the report.
