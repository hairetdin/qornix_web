#!/usr/bin/env python3
"""Qornix sprint 0 baseline benchmark runner.

The script starts tests/baseline_benchmark_server, runs reproducible HTTP/1.1
keep-alive scenarios, and prints Markdown tables with latency/resource metrics.
It intentionally uses only the Python standard library.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass
class ScenarioResult:
    name: str
    path: str
    concurrency: int
    duration_s: float
    requests: int
    errors: int
    rps: float
    p50_ms: float
    p95_ms: float
    p99_ms: float
    rss_kb: int | None
    threads: int | None
    fds: int | None


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
                 errors: list[int]) -> None:
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    ).encode("ascii")

    try:
        reader, writer = await asyncio.open_connection(host, port)
    except OSError:
        errors.append(1)
        return

    try:
        while time.perf_counter() < deadline:
            started = time.perf_counter()
            writer.write(request)
            await writer.drain()
            status = await read_response(reader)
            elapsed_ms = (time.perf_counter() - started) * 1000.0
            if status != 200:
                errors.append(1)
            else:
                latencies.append(elapsed_ms)
    except (OSError, asyncio.IncompleteReadError, ConnectionError, ValueError):
        errors.append(1)
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except OSError:
            pass


async def run_scenario(host: str,
                       port: int,
                       pid: int,
                       name: str,
                       path: str,
                       concurrency: int,
                       duration_s: float) -> ScenarioResult:
    latencies: list[float] = []
    errors: list[int] = []
    metric_samples: list[tuple[int | None, int | None, int | None]] = []
    stop_sampling = asyncio.Event()
    deadline = time.perf_counter() + duration_s
    started = time.perf_counter()
    sampler = asyncio.create_task(sample_proc_metrics(pid, stop_sampling, metric_samples))
    try:
        await asyncio.gather(*[
            worker(host, port, path, deadline, latencies, errors)
            for _ in range(concurrency)
        ])
    finally:
        elapsed = max(time.perf_counter() - started, 0.001)
        metric_samples.append(read_proc_metrics(pid))
        stop_sampling.set()
        await sampler

    return ScenarioResult(
        name=name,
        path=path,
        concurrency=concurrency,
        duration_s=elapsed,
        requests=len(latencies),
        errors=len(errors),
        rps=len(latencies) / elapsed,
        p50_ms=percentile(latencies, 0.50),
        p95_ms=percentile(latencies, 0.95),
        p99_ms=percentile(latencies, 0.99),
        rss_kb=max_metric(metric_samples, 0),
        threads=max_metric(metric_samples, 1),
        fds=max_metric(metric_samples, 2),
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


def print_markdown(results: Iterable[ScenarioResult], idle: IdleResult | None, args: argparse.Namespace) -> None:
    print("# Qornix Sprint 0 Baseline Benchmark")
    print()
    print(f"- server: `{args.server}`")
    print(f"- address: `{args.host}:{args.port}`")
    print(f"- server threads: `{args.server_threads}`")
    print(f"- scenario duration target: `{args.duration}s`")
    print()
    print("| scenario | endpoint | concurrency | requests | errors | RPS | p50 ms | p95 ms | p99 ms | RSS KB | threads | fd |")
    print("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for result in results:
        print(
            f"| {result.name} | `{result.path}` | {result.concurrency} | {result.requests} | "
            f"{result.errors} | {result.rps:.2f} | {result.p50_ms:.2f} | {result.p95_ms:.2f} | "
            f"{result.p99_ms:.2f} | {fmt(result.rss_kb)} | {fmt(result.threads)} | {fmt(result.fds)} |"
        )
    print()

    if idle is None:
        print("Idle keep-alive scenario: not requested.")
    else:
        print("| idle keep-alive requested | opened | failed | hold s | RSS KB | threads | fd |")
        print("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
        print(
            f"| {idle.requested} | {idle.opened} | {idle.failed} | {idle.hold_s:.2f} | "
            f"{fmt(idle.rss_kb)} | {fmt(idle.threads)} | {fmt(idle.fds)} |"
        )


async def async_main(args: argparse.Namespace) -> int:
    process = start_server(args.server, args.host, args.port, args.server_threads)
    try:
        wait_until_ready(args.host, args.port, timeout_s=10.0)
        results = [
            await run_scenario(args.host, args.port, process.pid, "fast", "/bench/fast", args.concurrency, args.duration),
            await run_scenario(args.host, args.port, process.pid, "delay_10ms", "/bench/delay/10", args.concurrency, args.duration),
            await run_scenario(args.host, args.port, process.pid, "delay_100ms", "/bench/delay/100", args.concurrency, args.duration),
            await run_scenario(args.host, args.port, process.pid, "fast_1k_concurrent", "/bench/fast", 1000, args.duration),
        ]
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
        print_markdown(results, idle, args)
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
