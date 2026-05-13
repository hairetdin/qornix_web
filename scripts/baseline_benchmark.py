#!/usr/bin/env python3
"""Qornix async benchmark runner for reproducible HTTP load scenarios.

The script starts tests/baseline_benchmark_server, runs HTTP/1.1 keep-alive
scenarios, writes a Markdown report to doc/benchmark.md by default, and also
prints the report to stdout. It intentionally uses only the Python standard
library.
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
from datetime import datetime
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class ScenarioResult:
    name: str
    path: str
    concurrency: int
    duration_s: float
    responses: int
    errors: int
    status_2xx: int
    status_4xx: int
    status_5xx: int
    client_errors: int
    rps: float
    p50_ms: float
    p95_ms: float
    p99_ms: float
    rss_kb: int | None
    threads: int | None
    fds: int | None
    cpu_percent: float | None
    active_requests_end: int | None
    rejected_requests_delta: int | None
    timed_out_requests_delta: int | None
    rejected_db_waiters_delta: int | None


@dataclass
class IdleResult:
    requested: int
    opened: int
    failed: int
    hold_s: float
    rss_kb: int | None
    threads: int | None
    fds: int | None


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


def fetch_server_metrics(host: str, port: int) -> dict[str, int | float]:
    request = (
        "GET /qornix/metrics HTTP/1.1\r\n"
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


async def sample_proc_metrics(pid: int,
                              stop: asyncio.Event,
                              samples: list[tuple[int | None, int | None, int | None]]) -> None:
    while not stop.is_set():
        samples.append(read_proc_metrics(pid))
        try:
            await asyncio.wait_for(stop.wait(), timeout=0.1)
        except asyncio.TimeoutError:
            pass


def max_metric(samples: list[tuple[int | None, int | None, int | None]], index: int) -> int | None:
    values = [sample[index] for sample in samples if sample[index] is not None]
    return max(values) if values else None


def find_free_port(host: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return int(sock.getsockname()[1])


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
            elapsed_ms = (time.perf_counter() - started) * 1000.0
            statuses.append(status)
            latencies.append(elapsed_ms)
    except (OSError, asyncio.IncompleteReadError, ConnectionError, ValueError):
        client_errors.append(1)
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except OSError:
            pass


def metric_int(metrics: dict[str, int | float], key: str) -> int | None:
    if key not in metrics:
        return None
    try:
        return int(metrics[key])
    except (TypeError, ValueError):
        return None


def metric_delta(before: dict[str, int | float],
                 after: dict[str, int | float],
                 key: str) -> int | None:
    start = metric_int(before, key)
    end = metric_int(after, key)
    if start is None or end is None:
        return None
    return max(0, end - start)


async def run_scenario(host: str,
                       port: int,
                       pid: int,
                       name: str,
                       path: str,
                       concurrency: int,
                       duration_s: float) -> ScenarioResult:
    latencies: list[float] = []
    statuses: list[int] = []
    client_errors: list[int] = []
    metric_samples: list[tuple[int | None, int | None, int | None]] = []
    stop_sampling = asyncio.Event()
    deadline = time.perf_counter() + duration_s
    before_server_metrics = fetch_server_metrics(host, port)
    started = time.perf_counter()
    start_cpu_ticks = read_proc_cpu_ticks(pid)
    sampler = asyncio.create_task(sample_proc_metrics(pid, stop_sampling, metric_samples))
    try:
        await asyncio.gather(*[
            worker(host, port, path, deadline, latencies, statuses, client_errors)
            for _ in range(concurrency)
        ])
    finally:
        elapsed = max(time.perf_counter() - started, 0.001)
        end_cpu_ticks = read_proc_cpu_ticks(pid)
        metric_samples.append(read_proc_metrics(pid))
        stop_sampling.set()
        await sampler

    after_server_metrics = fetch_server_metrics(host, port)
    active_requests_end = metric_int(after_server_metrics, "active_requests")
    if active_requests_end is not None:
        # The metrics request itself is active while the snapshot is produced.
        active_requests_end = max(0, active_requests_end - 1)
    responses = len(statuses)
    status_2xx = sum(1 for status in statuses if 200 <= status < 300)
    status_4xx = sum(1 for status in statuses if 400 <= status < 500)
    status_5xx = sum(1 for status in statuses if 500 <= status < 600)
    errors = (responses - status_2xx) + len(client_errors)

    return ScenarioResult(
        name=name,
        path=path,
        concurrency=concurrency,
        duration_s=elapsed,
        responses=responses,
        errors=errors,
        status_2xx=status_2xx,
        status_4xx=status_4xx,
        status_5xx=status_5xx,
        client_errors=len(client_errors),
        rps=responses / elapsed,
        p50_ms=percentile(latencies, 0.50),
        p95_ms=percentile(latencies, 0.95),
        p99_ms=percentile(latencies, 0.99),
        rss_kb=max_metric(metric_samples, 0),
        threads=max_metric(metric_samples, 1),
        fds=max_metric(metric_samples, 2),
        cpu_percent=calculate_cpu_percent(start_cpu_ticks, end_cpu_ticks, elapsed),
        active_requests_end=active_requests_end,
        rejected_requests_delta=metric_delta(before_server_metrics, after_server_metrics, "rejected_requests"),
        timed_out_requests_delta=metric_delta(before_server_metrics, after_server_metrics, "timed_out_requests"),
        rejected_db_waiters_delta=metric_delta(before_server_metrics, after_server_metrics, "rejected_db_waiters"),
    )


async def open_idle_connection(host: str, port: int) -> tuple[asyncio.StreamReader, asyncio.StreamWriter] | None:
    request = (
        "GET /bench/fast HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    ).encode("ascii")

    try:
        reader, writer = await asyncio.open_connection(host, port)
        writer.write(request)
        await writer.drain()
        status = await read_response(reader)
        if status != 200:
            writer.close()
            await writer.wait_closed()
            return None
        return reader, writer
    except (OSError, asyncio.IncompleteReadError, ConnectionError, ValueError):
        return None


async def run_idle_keepalive(host: str,
                             port: int,
                             pid: int,
                             connections: int,
                             hold_s: float,
                             batch_size: int) -> IdleResult:
    opened: list[tuple[asyncio.StreamReader, asyncio.StreamWriter]] = []
    failed = 0

    for start in range(0, connections, batch_size):
        batch_count = min(batch_size, connections - start)
        batch = await asyncio.gather(*[
            open_idle_connection(host, port)
            for _ in range(batch_count)
        ])
        for connection in batch:
            if connection is None:
                failed += 1
            else:
                opened.append(connection)

    await asyncio.sleep(hold_s)
    rss_kb, threads, fds = read_proc_metrics(pid)

    for _, writer in opened:
        writer.close()
    await asyncio.gather(*[
        writer.wait_closed()
        for _, writer in opened
    ], return_exceptions=True)

    return IdleResult(
        requested=connections,
        opened=len(opened),
        failed=failed,
        hold_s=hold_s,
        rss_kb=rss_kb,
        threads=threads,
        fds=fds,
    )


def wait_until_ready(host: str, port: int, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2) as sock:
                request = (
                    "GET /health HTTP/1.1\r\n"
                    f"Host: {host}:{port}\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                ).encode("ascii")
                sock.sendall(request)
                if b"200" in sock.recv(128):
                    return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("benchmark server did not become ready")


def start_server(server: Path,
                 host: str,
                 port: int,
                 threads: int) -> subprocess.Popen[bytes]:
    del host
    return subprocess.Popen(
        [str(server), str(port), str(threads)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
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


def render_markdown(results: Iterable[ScenarioResult], idle: IdleResult | None, args: argparse.Namespace) -> str:
    lines: list[str] = []
    generated_at = datetime.now().astimezone().isoformat(timespec="seconds")

    lines.append("# Qornix Async Benchmark")
    lines.append("")
    lines.append(f"- generated at: `{generated_at}`")
    lines.append(f"- server: `{args.server}`")
    lines.append(f"- address: `{args.host}:{args.port}`")
    lines.append(f"- server threads: `{args.server_threads}`")
    lines.append(f"- scenario duration target: `{args.duration}s`")
    lines.append(f"- extended load scenarios: `{'enabled' if args.extended_load else 'disabled'}`")
    lines.append("")
    lines.append("Counters marked as `delta` are per-scenario increments measured from `/qornix/metrics`, not cumulative process totals.")
    lines.append("")
    lines.append("| scenario | endpoint | concurrency | responses | errors | 2xx | 4xx | 5xx | client errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | CPU % | threads | fd | active end | rejected delta | timeout delta | DB reject delta |")
    lines.append("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for result in results:
        lines.append(
            f"| {result.name} | `{result.path}` | {result.concurrency} | {result.responses} | "
            f"{result.errors} | {result.status_2xx} | {result.status_4xx} | {result.status_5xx} | "
            f"{result.client_errors} | {result.rps:.2f} | {result.p50_ms:.2f} | {result.p95_ms:.2f} | "
            f"{result.p99_ms:.2f} | {fmt(result.rss_kb)} | {fmt(result.cpu_percent)} | "
            f"{fmt(result.threads)} | {fmt(result.fds)} | {fmt(result.active_requests_end)} | "
            f"{fmt(result.rejected_requests_delta)} | {fmt(result.timed_out_requests_delta)} | "
            f"{fmt(result.rejected_db_waiters_delta)} |"
        )
    lines.append("")

    if idle is None:
        lines.append("Idle keep-alive scenario: not requested.")
    else:
        lines.append("| idle keep-alive requested | opened | failed | hold s | RSS KB | threads | fd |")
        lines.append("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
        lines.append(
            f"| {idle.requested} | {idle.opened} | {idle.failed} | {idle.hold_s:.2f} | "
            f"{fmt(idle.rss_kb)} | {fmt(idle.threads)} | {fmt(idle.fds)} |"
        )

    return "\n".join(lines) + "\n"


def write_report(markdown: str, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(markdown, encoding="utf-8")


async def async_main(args: argparse.Namespace) -> int:
    process = start_server(args.server, args.host, args.port, args.server_threads)
    try:
        wait_until_ready(args.host, args.port, timeout_s=10.0)
        results = [
            await run_scenario(args.host, args.port, process.pid, "fast", "/bench/fast", args.concurrency, args.duration),
            await run_scenario(args.host, args.port, process.pid, "async_delay_10ms", "/bench/delay/10", args.concurrency, args.duration),
            await run_scenario(args.host, args.port, process.pid, "async_delay_100ms", "/bench/delay/100", args.concurrency, args.duration),
            await run_scenario(args.host, args.port, process.pid, "fast_1k_concurrent", "/bench/fast", 1000, args.duration),
        ]
        if args.extended_load:
            for concurrency in args.load_concurrency:
                results.append(await run_scenario(
                    args.host, args.port, process.pid, f"load_fast_{concurrency}", "/health", concurrency, args.duration
                ))
                results.append(await run_scenario(
                    args.host, args.port, process.pid, f"load_async_sleep_{concurrency}", "/sleep?ms=100", concurrency, args.duration
                ))
            results.append(await run_scenario(
                args.host, args.port, process.pid, "db_pool_normal", "/users/42", args.db_normal_concurrency, args.duration
            ))
            results.append(await run_scenario(
                args.host, args.port, process.pid, "db_pool_overload", "/users/42", args.db_concurrency, args.duration
            ))
            results.append(await run_scenario(
                args.host, args.port, process.pid, "timeout_guard", "/sleep?ms=3000", args.concurrency, args.duration
            ))
        idle = None
        if args.idle_connections > 0:
            idle = await run_idle_keepalive(
                args.host,
                args.port,
                process.pid,
                args.idle_connections,
                args.idle_hold,
                args.idle_batch_size,
            )
        markdown = render_markdown(results, idle, args)
        write_report(markdown, args.output)
        print(markdown, end="")
        print(f"Benchmark report written to {args.output}", file=sys.stderr)
        return 0
    finally:
        stop_server(process)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    default_server = Path("build/async_baseline/baseline_benchmark_server")
    parser.add_argument("--server", type=Path, default=default_server)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--server-threads", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--concurrency", type=int, default=64)
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--idle-connections", type=int, default=0)
    parser.add_argument("--idle-hold", type=float, default=5.0)
    parser.add_argument("--idle-batch-size", type=int, default=500)
    parser.add_argument("--extended-load", action="store_true", help="run additional high-concurrency load scenarios")
    parser.add_argument("--load-concurrency", type=int, nargs="*", default=[1000, 5000, 10000])
    parser.add_argument("--db-normal-concurrency", type=int, default=128, help="concurrency for the normal DB pool scenario")
    parser.add_argument("--db-concurrency", type=int, default=10000, help="concurrency for the DB pool overload scenario")
    parser.add_argument("--output", type=Path, default=Path("doc/benchmark.md"), help="Markdown report path")
    args = parser.parse_args()

    if args.port == 0:
        args.port = find_free_port(args.host)
    if not args.server.exists():
        parser.error(f"server executable not found: {args.server}")
    return args


def main() -> int:
    args = parse_args()
    return asyncio.run(async_main(args))


if __name__ == "__main__":
    sys.exit(main())
