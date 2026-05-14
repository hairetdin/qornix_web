#!/usr/bin/env python3
"""Run async DB HTTP benchmark scenarios and write doc/benchmark_async_db.md.

The runner starts tests/async_db_benchmark_server and drives the DB endpoints
with keep-alive HTTP/1.1 connections. It intentionally uses only the Python
standard library.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


DB_METRIC_KEYS = [
    "active_connections",
    "idle_connections",
    "queued_waiters",
    "rejected_acquires",
    "acquire_timeouts",
    "created_connections",
    "closed_connections",
    "discarded_connections",
    "failed_connects",
    "query_total",
    "query_success",
    "query_error",
    "query_timeout",
    "query_cancelled",
    "query_option_timeouts",
    "query_pool_default_timeouts",
    "request_deadline_timeouts",
    "prepared_cache_hits",
    "prepared_cache_misses",
    "prepared_cache_evictions",
    "query_latency_p50_us",
    "query_latency_p95_us",
    "query_latency_p99_us",
]


@dataclass
class Scenario:
    name: str
    path: str
    concurrency: int
    duration_s: float
    expected_statuses: tuple[int, ...]
    expected: str


@dataclass
class ScenarioResult:
    name: str
    path: str
    concurrency: int
    duration_s: float
    responses: int
    errors: int
    unexpected_errors: int
    status_2xx: int
    status_4xx: int
    status_5xx: int
    status_503: int
    status_504: int
    client_errors: int
    rps: float
    p50_ms: float
    p95_ms: float
    p99_ms: float
    rss_kb: int | None
    threads: int | None
    fds: int | None
    cpu_percent: float | None
    db_deltas: dict[str, int]
    db_snapshot: dict[str, int]
    expected_statuses: tuple[int, ...]
    expected: str


def percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    index = int((len(sorted_values) - 1) * percent)
    return sorted_values[index]


def read_proc_metrics(pid: int) -> tuple[int | None, int | None, int | None]:
    status_path = Path(f"/proc/{pid}/status")
    fd_path = Path(f"/proc/{pid}/fd")
    rss_kb: int | None = None
    threads: int | None = None

    try:
        for line in status_path.read_text(encoding="utf-8").splitlines():
            if line.startswith("VmRSS:"):
                rss_kb = int(line.split()[1])
            elif line.startswith("Threads:"):
                threads = int(line.split()[1])
    except (FileNotFoundError, PermissionError, ValueError):
        pass

    try:
        fds = len(list(fd_path.iterdir()))
    except (FileNotFoundError, PermissionError):
        fds = None

    return rss_kb, threads, fds


def read_proc_cpu_ticks(pid: int) -> int | None:
    try:
        parts = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8").split()
        return int(parts[13]) + int(parts[14])
    except (FileNotFoundError, PermissionError, ValueError, IndexError):
        return None


def calculate_cpu_percent(start_ticks: int | None, end_ticks: int | None, elapsed_s: float) -> float | None:
    if start_ticks is None or end_ticks is None or elapsed_s <= 0:
        return None
    try:
        hz = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    except (KeyError, ValueError, OSError):
        hz = 100
    return ((end_ticks - start_ticks) / hz) / elapsed_s * 100.0


def max_metric(samples: list[tuple[int | None, int | None, int | None]], index: int) -> int | None:
    values = [sample[index] for sample in samples if sample[index] is not None]
    return max(values) if values else None


def find_free_port(host: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return int(sock.getsockname()[1])


async def sample_proc_metrics(pid: int,
                              stop: asyncio.Event,
                              samples: list[tuple[int | None, int | None, int | None]]) -> None:
    while not stop.is_set():
        samples.append(read_proc_metrics(pid))
        try:
            await asyncio.wait_for(stop.wait(), timeout=0.1)
        except asyncio.TimeoutError:
            pass


async def read_response(reader: asyncio.StreamReader) -> int:
    status_line = await reader.readline()
    if not status_line:
        raise ConnectionError("empty status line")

    parts = status_line.decode("iso-8859-1").split()
    if len(parts) < 2:
        raise ConnectionError(f"bad status line: {status_line!r}")
    status = int(parts[1])

    content_length = 0
    while True:
        line = await reader.readline()
        if line in (b"\r\n", b"\n", b""):
            break
        name, _, value = line.decode("iso-8859-1").partition(":")
        if name.lower() == "content-length":
            content_length = int(value.strip())

    if content_length:
        await reader.readexactly(content_length)
    return status


async def worker(host: str,
                 port: int,
                 path: str,
                 deadline: float,
                 latencies: list[float],
                 statuses: list[int],
                 client_errors: list[int]) -> None:
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    ).encode("ascii")

    try:
        reader, writer = await asyncio.open_connection(host, port)
    except OSError:
        client_errors.append(1)
        return

    try:
        while time.perf_counter() < deadline:
            started = time.perf_counter()
            writer.write(request)
            await writer.drain()
            status = await read_response(reader)
            latencies.append((time.perf_counter() - started) * 1000.0)
            statuses.append(status)
    except (OSError, asyncio.IncompleteReadError, ConnectionError, ValueError):
        client_errors.append(1)
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except OSError:
            pass


def fetch_json(host: str, port: int, path: str) -> dict:
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")
    try:
        with socket.create_connection((host, port), timeout=1.0) as sock:
            sock.sendall(request)
            data = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                data += chunk
        _, _, body = data.partition(b"\r\n\r\n")
        parsed = json.loads(body.decode("utf-8"))
        return parsed if isinstance(parsed, dict) else {}
    except (OSError, ValueError, json.JSONDecodeError):
        return {}


def fetch_db_metrics(host: str, port: int) -> dict[str, int]:
    payload = fetch_json(host, port, "/db/metrics")
    metrics = payload.get("metrics", {})
    if not isinstance(metrics, dict):
        return {}
    result: dict[str, int] = {}
    for key in DB_METRIC_KEYS:
        try:
            result[key] = int(metrics.get(key, 0))
        except (TypeError, ValueError):
            result[key] = 0
    return result


def metric_delta(before: dict[str, int], after: dict[str, int], key: str) -> int:
    return max(0, int(after.get(key, 0)) - int(before.get(key, 0)))


async def run_scenario(host: str, port: int, pid: int, scenario: Scenario) -> ScenarioResult:
    latencies: list[float] = []
    statuses: list[int] = []
    client_errors: list[int] = []
    metric_samples: list[tuple[int | None, int | None, int | None]] = []
    stop_sampling = asyncio.Event()
    deadline = time.perf_counter() + scenario.duration_s
    before_db = fetch_db_metrics(host, port)
    started = time.perf_counter()
    start_cpu_ticks = read_proc_cpu_ticks(pid)
    sampler = asyncio.create_task(sample_proc_metrics(pid, stop_sampling, metric_samples))

    try:
        await asyncio.gather(*[
            worker(host, port, scenario.path, deadline, latencies, statuses, client_errors)
            for _ in range(scenario.concurrency)
        ])
    finally:
        elapsed = max(time.perf_counter() - started, 0.001)
        end_cpu_ticks = read_proc_cpu_ticks(pid)
        metric_samples.append(read_proc_metrics(pid))
        stop_sampling.set()
        await sampler

    after_db = fetch_db_metrics(host, port)
    responses = len(statuses)
    status_2xx = sum(1 for status in statuses if 200 <= status < 300)
    status_4xx = sum(1 for status in statuses if 400 <= status < 500)
    status_5xx = sum(1 for status in statuses if 500 <= status < 600)
    status_503 = sum(1 for status in statuses if status == 503)
    status_504 = sum(1 for status in statuses if status == 504)
    errors = (responses - status_2xx) + len(client_errors)
    expected_statuses = set(scenario.expected_statuses)
    unexpected_errors = sum(1 for status in statuses if status not in expected_statuses) + len(client_errors)
    db_deltas = {key: metric_delta(before_db, after_db, key) for key in DB_METRIC_KEYS}

    return ScenarioResult(
        name=scenario.name,
        path=scenario.path,
        concurrency=scenario.concurrency,
        duration_s=elapsed,
        responses=responses,
        errors=errors,
        unexpected_errors=unexpected_errors,
        status_2xx=status_2xx,
        status_4xx=status_4xx,
        status_5xx=status_5xx,
        status_503=status_503,
        status_504=status_504,
        client_errors=len(client_errors),
        rps=responses / elapsed,
        p50_ms=percentile(latencies, 0.50),
        p95_ms=percentile(latencies, 0.95),
        p99_ms=percentile(latencies, 0.99),
        rss_kb=max_metric(metric_samples, 0),
        threads=max_metric(metric_samples, 1),
        fds=max_metric(metric_samples, 2),
        cpu_percent=calculate_cpu_percent(start_cpu_ticks, end_cpu_ticks, elapsed),
        db_deltas=db_deltas,
        db_snapshot=after_db,
        expected_statuses=scenario.expected_statuses,
        expected=scenario.expected,
    )


def wait_until_ready(host: str, port: int, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            payload = fetch_json(host, port, "/health")
            if payload.get("status") == "ok":
                return
        except OSError:
            pass
        time.sleep(0.05)
    raise RuntimeError("async DB benchmark server did not become ready")


def start_server(args: argparse.Namespace) -> subprocess.Popen[bytes]:
    env = os.environ.copy()
    env["QORNIX_ASYNC_DB_DRIVER"] = args.driver
    env["QORNIX_ASYNC_DB_POOL_SIZE"] = str(args.pool_size)
    env["QORNIX_ASYNC_DB_MAX_WAITERS"] = str(args.max_waiters)
    env["QORNIX_ASYNC_DB_QUERY_TIMEOUT_MS"] = str(args.query_timeout_ms)
    env["QORNIX_ASYNC_DB_ACQUIRE_TIMEOUT_MS"] = str(args.acquire_timeout_ms)
    env["QORNIX_ASYNC_DB_PREPARED_CACHE_SIZE"] = str(args.prepared_cache_size)
    env["QORNIX_ASYNC_DB_SLOW_TIMEOUT_MS"] = str(args.slow_timeout_ms)
    env["QORNIX_ASYNC_DB_SLOW_SECONDS"] = str(args.slow_seconds)
    env["QORNIX_ASYNC_DB_MOCK_LATENCY_MS"] = str(args.mock_latency_ms)

    return subprocess.Popen(
        [str(args.server), str(args.port), str(args.server_threads), args.driver],
        stdout=subprocess.PIPE if args.quiet_server else None,
        stderr=subprocess.STDOUT if args.quiet_server else None,
        start_new_session=True,
        env=env,
    )


def stop_server(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=5)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        process.kill()
        process.wait(timeout=5)


def fmt(value: int | float | None) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return f"{value:.2f}"
    return str(value)


def render_markdown(results: Iterable[ScenarioResult], args: argparse.Namespace) -> str:
    results = list(results)
    generated_at = datetime.now().astimezone().isoformat(timespec="seconds")
    lines: list[str] = [
        "# Async DB Benchmark",
        "",
        f"- generated at: `{generated_at}`",
        f"- server: `{args.server}`",
        f"- address: `{args.host}:{args.port}`",
        f"- driver: `{args.driver}`",
        f"- server threads: `{args.server_threads}`",
        f"- pool size: `{args.pool_size}`",
        f"- max waiters: `{args.max_waiters}`",
        f"- query timeout: `{args.query_timeout_ms}ms`",
        f"- acquire timeout: `{args.acquire_timeout_ms}ms`",
        "",
        "Counters marked `delta` are per-scenario increments from `/db/metrics`.",
        "`errors` counts every non-2xx response; `unexpected` counts responses outside the scenario's expected status set.",
        "",
        "| scenario | endpoint | concurrency | responses | errors | unexpected | 2xx | 4xx | 5xx | 503 | 504 | client errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | CPU % | threads | fd | DB ok delta | DB err delta | acquire timeout delta | query timeout delta | cancel delta | reject delta | discard delta | failed connect delta | prepared hit delta | prepared miss delta | expected statuses | expected |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]

    last_snapshot: dict[str, int] = {}
    for result in results:
        last_snapshot = result.db_snapshot
        delta = result.db_deltas
        lines.append(
            f"| {result.name} | `{result.path}` | {result.concurrency} | {result.responses} | "
            f"{result.errors} | {result.unexpected_errors} | {result.status_2xx} | {result.status_4xx} | "
            f"{result.status_5xx} | {result.status_503} | {result.status_504} | "
            f"{result.client_errors} | {result.rps:.2f} | {result.p50_ms:.2f} | {result.p95_ms:.2f} | "
            f"{result.p99_ms:.2f} | {fmt(result.rss_kb)} | {fmt(result.cpu_percent)} | "
            f"{fmt(result.threads)} | {fmt(result.fds)} | {delta['query_success']} | "
            f"{delta['query_error']} | {delta['acquire_timeouts']} | {delta['query_timeout']} | "
            f"{delta['query_cancelled']} | {delta['rejected_acquires']} | "
            f"{delta['discarded_connections']} | {delta['failed_connects']} | "
            f"{delta['prepared_cache_hits']} | {delta['prepared_cache_misses']} | "
            f"{'/'.join(str(status) for status in result.expected_statuses)} | {result.expected} |"
        )

    unexpected = [result for result in results if result.unexpected_errors > 0]
    lines.extend([
        "",
        "## Validation",
        "",
    ])
    if unexpected:
        lines.append("| scenario | unexpected | expected statuses |")
        lines.append("| --- | ---: | --- |")
        for result in unexpected:
            expected_statuses = "/".join(str(status) for status in result.expected_statuses)
            lines.append(f"| {result.name} | {result.unexpected_errors} | {expected_statuses} |")
    else:
        lines.append("All scenarios matched their expected HTTP status sets.")

    lines.extend([
        "",
        "## Final DB Metrics",
        "",
        "| metric | value |",
        "| --- | ---: |",
    ])
    for key in DB_METRIC_KEYS:
        lines.append(f"| `{key}` | {last_snapshot.get(key, 0)} |")

    lines.extend([
        "",
        "## Command",
        "",
        "```bash",
        " ".join(sys.argv),
        "```",
        "",
    ])
    return "\n".join(lines)


def build_scenarios(args: argparse.Namespace) -> list[Scenario]:
    prefix = args.driver
    scenarios = [
        Scenario(f"{prefix}_normal_select_{args.normal_concurrency}", "/db/select", args.normal_concurrency, args.duration,
                 (200,),
                 "0 errors, bounded threads"),
        Scenario(f"{prefix}_prepared_select_{args.normal_concurrency}", "/db/prepared", args.normal_concurrency, args.duration,
                 (200,),
                 "0 errors, prepared cache warms then hits"),
        Scenario(f"{prefix}_transaction_rollback_{args.normal_concurrency}", "/db/transaction", args.normal_concurrency, args.duration,
                 (200,),
                 "0 errors, rollback path succeeds"),
        Scenario(f"{prefix}_slow_query_timeout", "/db/slow", args.timeout_concurrency, args.duration,
                 (504,),
                 "504 responses counted as query timeouts"),
    ]
    if args.extended:
        scenarios.insert(1, Scenario(f"{prefix}_normal_select_{args.high_concurrency}", "/db/select",
                                     args.high_concurrency, args.duration, (200,), "0 errors, bounded threads"))
        scenarios.append(Scenario(f"{prefix}_pool_overload_{args.overload_concurrency}", "/db/select",
                                  args.overload_concurrency, args.duration,
                                  (200, 503, 504),
                                  "controlled 503/504 responses, no unbounded waiters"))
    return scenarios


async def async_main(args: argparse.Namespace) -> int:
    process = start_server(args)
    try:
        wait_until_ready(args.host, args.port, timeout_s=10.0)
        results = []
        for scenario in build_scenarios(args):
            results.append(await run_scenario(args.host, args.port, process.pid, scenario))
        markdown = render_markdown(results, args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(markdown, encoding="utf-8")
        print(markdown)
        print(f"Async DB benchmark report written to {args.output}", file=sys.stderr)
        return 0
    finally:
        stop_server(process)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, default=Path("cmake-build-debug/async_db_benchmark_server"))
    parser.add_argument("--driver", choices=["mock", "postgres", "mysql"], default=os.getenv("QORNIX_ASYNC_DB_DRIVER", "mock"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--server-threads", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--normal-concurrency", type=int, default=128)
    parser.add_argument("--high-concurrency", type=int, default=1000)
    parser.add_argument("--overload-concurrency", type=int, default=10000)
    parser.add_argument("--timeout-concurrency", type=int, default=128)
    parser.add_argument("--pool-size", type=int, default=32)
    parser.add_argument("--max-waiters", type=int, default=1024)
    parser.add_argument("--query-timeout-ms", type=int, default=2000)
    parser.add_argument("--acquire-timeout-ms", type=int, default=1000)
    parser.add_argument("--prepared-cache-size", type=int, default=64)
    parser.add_argument("--slow-timeout-ms", type=int, default=50)
    parser.add_argument("--slow-seconds", type=float, default=0.25)
    parser.add_argument("--mock-latency-ms", type=int, default=2)
    parser.add_argument("--extended", action="store_true", help="include 1000-concurrency and overload scenarios")
    parser.add_argument("--quiet-server", action="store_true", help="capture benchmark server output")
    parser.add_argument("--output", type=Path, default=Path("doc/benchmark_async_db.md"))
    args = parser.parse_args()

    if args.port == 0:
        args.port = find_free_port(args.host)
    if not args.server.exists():
        parser.error(f"server executable not found: {args.server}")
    return args


def main() -> int:
    return asyncio.run(async_main(parse_args()))


if __name__ == "__main__":
    sys.exit(main())
