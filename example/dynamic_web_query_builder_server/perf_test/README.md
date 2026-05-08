# Dynamic API performance tests

Automated performance test suite for the dynamic query builder server.

## What is tested

| Phase | Description |
|------|-------------|
| **simple_reads** | Simple GET requests: read tables by name and by ID |
| **complex_queries** | Complex queries with JOIN, filters, sorting and pagination |
| **metadata_endpoints** | Metadata: table/field autocomplete and XML schema export |
| **write_operations** | POST create, PUT full update, PATCH partial update and DELETE |
| **concurrent_load** | Tests with different concurrency levels: 1, 5, 10, 20, 50 and 100 connections |
| **stress_test** | Sustained high load: 200 requests against 6 endpoints in parallel |

## Quick start

### 1. Create a virtual environment, recommended

```bash
python3 -m venv .venv
source .venv/bin/activate
```

### 2. Install Python dependencies

```bash
pip install -r requirements.txt
```

### 3. Run the test with one command

```bash
./run_perf_test.sh
```

The script automatically:

- builds the server if the binary is missing;
- starts the server on `127.0.0.1:8008`;
- waits until it is ready;
- runs the tests;
- stops the server.

### 4. Or run manually

```bash
# Build and start the server
cd ../../../../build
cmake --build . --target dynamic_web_query_builder_server
./example/dynamic_web_query_builder_server/dynamic_web_query_builder_server
```

```bash
# In another terminal
cd example/dynamic_web_query_builder_server/perf_test
python3 perf_test.py --host 127.0.0.1 --port 8008 --output report.json
```

To exit the virtual environment:

```bash
deactivate
```

## Parameters

### `run_perf_test.sh`

| Flag | Default | Description |
|------|---------|-------------|
| `--build` | — | Force rebuilding the server |
| `--concurrency N` | 20 | Maximum number of concurrent connections |
| `--requests N` | 200 | Requests per endpoint pattern |
| `--output FILE` | — | Save JSON report to a file |
| `--help` | — | Show help |

### `perf_test.py`

| Flag | Default | Description |
|------|---------|-------------|
| `--host` | 127.0.0.1 | Server host |
| `--port` | 8008 | Server port |
| `--concurrency` | 20 | Concurrent connections |
| `--requests` | 200 | Requests per pattern |
| `--output` | — | Path for JSON report |

## Examples

```bash
# Light test, quick check
./run_perf_test.sh --requests 20 --concurrency 5

# Medium test
./run_perf_test.sh --requests 100 --concurrency 20

# Heavy test
./run_perf_test.sh --requests 500 --concurrency 100 --output report.json

# Manual run with a custom host
python3 perf_test.py --host 192.168.1.10 --port 8008 --requests 100
```

## Report structure

### Text report, stdout

```text
=== Dynamic API Performance Report ===

Phase: simple_reads
  Requests: 1400
  Success: 1400
  Errors: 0
  Avg latency: 12.3 ms
  P95 latency: 30.1 ms
  P99 latency: 44.8 ms
  RPS: 820.4

Phase: complex_queries
  ...
```

### JSON report, `--output report.json`

```json
{
  "summary": {
    "total_requests": 3200,
    "successful_requests": 3200,
    "failed_requests": 0,
    "duration_sec": 4.25
  },
  "phases": {
    "simple_reads": {
      "requests": 1400,
      "success": 1400,
      "errors": 0,
      "avg_latency_ms": 12.3,
      "p95_latency_ms": 30.1,
      "p99_latency_ms": 44.8,
      "rps": 820.4
    }
  }
}
```

## Dependencies

- **aiohttp** — asynchronous HTTP client.
- **curl** — server readiness check in the shell script.

## Server requirements

The server must be running and available at `127.0.0.1:8008` before tests are started manually.

## Notes

- The demo SQLite database is created automatically on the first server start.
- Write tests (POST/PUT/PATCH/DELETE) create and remove test records so the database is not polluted.
- For reproducible results, stop other server load during the test.
- The `concurrent_load` and `stress_test` phases can generate heavy load. Use them carefully on shared machines.
