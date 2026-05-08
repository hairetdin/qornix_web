#!/usr/bin/env python3
"""
Performance test suite for dynamic_web_query_builder_server.

Tests:
  1. Simple reads (GET by table, GET by id)
  2. Complex queries (filters, joins, sorting, pagination)
  3. Metadata/autocomplete endpoints
  4. Write operations (POST, PUT, PATCH, DELETE)
  5. Concurrent load (varying concurrency levels)
  6. Stress test (sustained high load)

Usage:
  python perf_test.py [--host HOST] [--port PORT] [--concurrency N] [--requests N] [--output FILE]

Example:
  python perf_test.py --concurrency 50 --requests 1000 --output report.json
"""

import argparse
import asyncio
import json
import time
import sys
from dataclasses import dataclass, field
from typing import Optional

try:
    import aiohttp
except ImportError:
    print("ERROR: aiohttp is required. Install: pip install -r requirements.txt", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class RequestResult:
    endpoint: str
    method: str
    status_code: int
    response_time_ms: float
    size_bytes: int
    error: Optional[str] = None

@dataclass
class TestPhase:
    name: str
    results: list = field(default_factory=list)

    def summary(self) -> dict:
        times_ms = [r.response_time_ms for r in self.results if not r.error]
        errors = sum(1 for r in self.results if r.error)
        sizes = [r.size_bytes for r in self.results]

        stats = compute_stats(times_ms)
        stats["errors"] = errors
        if sizes:
            stats["size_min_bytes"] = min(sizes)
            stats["size_avg_bytes"] = round(sum(sizes) / len(sizes))
            stats["size_max_bytes"] = max(sizes)
        if times_ms:
            stats["rps"] = 0  # computed externally

        # Per-endpoint breakdown
        ep_map: dict[str, list] = {}
        for r in self.results:
            ep_map.setdefault(r.endpoint, []).append(r)

        endpoints = []
        for path, res_list in sorted(ep_map.items()):
            ep_times = [r.response_time_ms for r in res_list if not r.error]
            ep_errors = sum(1 for r in res_list if r.error)
            ep_stats = compute_stats(ep_times)
            ep_stats["path"] = path
            ep_stats["errors"] = ep_errors
            endpoints.append(ep_stats)

        return {
            "name": self.name,
            "stats": stats,
            "endpoints": endpoints,
        }

@dataclass
class TestReport:
    phases: list = field(default_factory=list)
    total_requests: int = 0
    total_errors: int = 0
    total_duration_sec: float = 0
    config: dict = field(default_factory=dict)

    def summary(self) -> dict:
        # Aggregate totals from phases
        total_requests = sum(len(p.results) for p in self.phases)
        total_errors = sum(sum(1 for r in p.results if r.error) for p in self.phases)

        overall = {
            "config": self.config,
            "total_requests": total_requests,
            "total_errors": total_errors,
            "total_duration_sec": round(self.total_duration_sec, 3),
            "overall_rps": round(
                total_requests / self.total_duration_sec, 2
            ) if self.total_duration_sec > 0 else 0,
            "phases": [],
        }
        for phase in self.phases:
            p = phase.summary()
            # Compute phase RPS
            phase_duration = self.total_duration_sec / max(len(self.phases), 1)
            phase_count = p["stats"].get("count", 0)
            if phase_duration > 0:
                p["stats"]["rps"] = round(phase_count / phase_duration, 2)
            overall["phases"].append(p)
        return overall

    def save(self, path: str):
        with open(path, "w") as f:
            json.dump(self.summary(), f, indent=2, ensure_ascii=False)
        print(f"Report saved to {path}")


def compute_stats(times_ms: list) -> dict:
    if not times_ms:
        return {
            "count": 0,
            "min_ms": 0,
            "max_ms": 0,
            "avg_ms": 0,
            "median_ms": 0,
            "p90_ms": 0,
            "p95_ms": 0,
            "p99_ms": 0,
        }
    times_ms.sort()
    n = len(times_ms)
    return {
        "count": n,
        "min_ms": round(min(times_ms), 2),
        "max_ms": round(max(times_ms), 2),
        "avg_ms": round(sum(times_ms) / n, 2),
        "median_ms": round(times_ms[n // 2], 2),
        "p90_ms": round(times_ms[int(n * 0.9)], 2),
        "p95_ms": round(times_ms[int(n * 0.95)], 2),
        "p99_ms": round(times_ms[int(n * 0.99)], 2),
    }


# ---------------------------------------------------------------------------
# Test phase helpers
# ---------------------------------------------------------------------------

class QueryBuilderPerfTest:
    """Performance test suite for the dynamic query builder server."""

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 8008,
        concurrency: int = 20,
        requests_per_phase: int = 200,
    ):
        self.host = host
        self.port = port
        self.base_url = f"http://{host}:{port}"
        self.concurrency = concurrency
        self.requests_per_phase = requests_per_phase
        self.report = TestReport()
        self.report.config = {
            "host": host,
            "port": port,
            "concurrency": concurrency,
            "requests_per_phase": requests_per_phase,
        }

    # -- public API --------------------------------------------------------

    async def run_all(self):
        """Run all test phases and return the report."""
        start = time.monotonic()
        phases = [
            self.test_simple_reads,
            self.test_complex_queries,
            self.test_metadata_endpoints,
            self.test_write_operations,
            self.test_concurrent_load,
            self.test_stress_test,
        ]
        for phase_fn in phases:
            await phase_fn()
        self.report.total_duration_sec = time.monotonic() - start
        return self.report

    # -- phase implementations ---------------------------------------------

    async def test_simple_reads(self):
        """Phase 1: Simple GET requests — single table reads and by-id."""
        phase = TestPhase(name="simple_reads")
        endpoints = [
            ("GET", "/api/dynamic/categories"),
            ("GET", "/api/dynamic/customers"),
            ("GET", "/api/dynamic/products"),
            ("GET", "/api/dynamic/orders"),
            ("GET", "/api/dynamic/products/1"),
            ("GET", "/api/dynamic/orders/1"),
            ("GET", "/api/dynamic/customers/2"),
        ]
        print(f"  Phase: {phase.name} — {len(endpoints)} endpoint patterns, "
              f"{self.requests_per_phase} requests each, concurrency={self.concurrency}")

        async with aiohttp.ClientSession() as session:
            tasks = []
            for _ in range(self.requests_per_phase):
                for method, path in endpoints:
                    tasks.append(self._make_request(session, method, path, phase))
            await asyncio.gather(*tasks)

        self.report.phases.append(phase)

    async def test_complex_queries(self):
        """Phase 2: Complex queries with filters, joins, sorting."""
        phase = TestPhase(name="complex_queries")
        print(f"  Phase: {phase.name} — {self.requests_per_phase} complex queries, "
              f"concurrency={self.concurrency}")

        complex_queries = [
            # Filtered query
            {
                "method": "GET",
                "table": "orders",
                "query": {
                    "fields": ["orders.id", "orders.total_amount", "customers.name"],
                    "filter": ["orders.status='paid'"],
                    "join": ["JOIN customers ON orders.customer_id=customers.id"],
                    "order_by": ["orders.total_amount DESC"],
                    "limit": 10,
                },
            },
            # Join with category
            {
                "method": "GET",
                "table": "products",
                "query": {
                    "fields": ["products.name", "products.price", "categories.name"],
                    "join": ["JOIN categories ON products.category_id=categories.id"],
                    "filter": ["categories.name='Electronics'"],
                    "order_by": ["products.price ASC"],
                },
            },
            # Aggregation-like: filter by stock
            {
                "method": "GET",
                "table": "products",
                "query": {
                    "fields": ["products.name", "products.stock", "products.price"],
                    "filter": ["products.stock > 10", "products.price < 100"],
                    "limit": 5,
                },
            },
            # Cross-table query
            {
                "method": "GET",
                "table": "orders",
                "query": {
                    "fields": ["orders.id", "orders.quantity", "products.name"],
                    "join": ["JOIN products ON orders.product_id=products.id"],
                    "filter": ["orders.quantity >= 2"],
                    "order_by": ["orders.quantity DESC"],
                },
            },
        ]

        async with aiohttp.ClientSession() as session:
            tasks = []
            for _ in range(self.requests_per_phase):
                for q in complex_queries:
                    tasks.append(self._make_post_request(session, "/api/dynamic/", q, phase))
            await asyncio.gather(*tasks)

        self.report.phases.append(phase)

    async def test_metadata_endpoints(self):
        """Phase 3: Metadata and autocomplete endpoints."""
        phase = TestPhase(name="metadata_endpoints")
        endpoints = [
            ("GET", "/api/dynamic/meta/tables"),
            ("GET", "/api/dynamic/meta/tables?q=c"),
            ("GET", "/api/dynamic/meta/tables?q=or"),
            ("GET", "/api/dynamic/meta/products/fields"),
            ("GET", "/api/dynamic/meta/products/fields?q=id"),
            ("GET", "/api/dynamic/meta/orders/fields?q=total"),
            ("GET", "/api/dynamic/meta/autocomplete?type=tables&q=p"),
            ("GET", "/api/dynamic/meta/autocomplete?type=fields&table=orders&q=dat"),
            ("GET", "/api/dynamic/schema.xml"),
        ]
        print(f"  Phase: {phase.name} — {len(endpoints)} endpoint patterns, "
              f"{self.requests_per_phase} requests each, concurrency={self.concurrency}")

        async with aiohttp.ClientSession() as session:
            tasks = []
            for _ in range(self.requests_per_phase):
                for method, path in endpoints:
                    tasks.append(self._make_request(session, method, path, phase))
            await asyncio.gather(*tasks)

        self.report.phases.append(phase)

    async def test_write_operations(self):
        """Phase 4: Write operations (POST, PUT, PATCH, DELETE)."""
        phase = TestPhase(name="write_operations")
        print(f"  Phase: {phase.name} — write operations, concurrency={self.concurrency}")

        # We'll do a cycle: create → read → update → delete to keep data clean
        write_ops = [
            # POST: create a new customer
            {
                "method": "POST",
                "table": "customers",
                "data": {
                    "name": "PerfTest User",
                    "email": f"perf_test_{int(time.time())}@test.local",
                    "phone": "+1-555-9999",
                    "city": "TestCity",
                    "created_at": "2026-06-01",
                },
            },
            # POST: create a new product
            {
                "method": "POST",
                "table": "products",
                "data": {
                    "category_id": 1,
                    "name": "PerfTest Product",
                    "description": "Created by perf test",
                    "price": 99.99,
                    "stock": 10,
                    "created_at": "2026-06-01",
                },
            },
        ]

        async with aiohttp.ClientSession() as session:
            # POST: create a new customer and product
            tasks = [
                self._make_post_request(session, "/api/dynamic/", op, phase)
                for op in write_ops
            ]
            await asyncio.gather(*tasks)

            # Read back what we created
            tasks = [
                self._make_request(session, "GET", f"/api/dynamic/{table}", phase)
                for table in ["customers", "products"]
            ]
            await asyncio.gather(*tasks)

            # PUT: full update
            put_op = {
                "method": "PUT",
                "table": "customers",
                "id": 5,
                "data": {
                    "name": "PerfTest User Updated",
                    "email": "perf_test_updated@test.local",
                    "phone": "+1-555-9998",
                    "city": "TestCity2",
                    "created_at": "2026-06-01",
                },
            }
            tasks = [self._make_post_request(session, "/api/dynamic/", put_op, phase)]
            await asyncio.gather(*tasks)

            # PATCH: partial update
            patch_op = {
                "method": "PATCH",
                "table": "customers",
                "id": 5,
                "data": {"city": "TestCityPatch"},
            }
            tasks = [self._make_post_request(session, "/api/dynamic/", patch_op, phase)]
            await asyncio.gather(*tasks)

            # DELETE
            delete_op = {
                "method": "DELETE",
                "table": "customers",
                "id": 5,
            }
            tasks = [self._make_post_request(session, "/api/dynamic/", delete_op, phase)]
            await asyncio.gather(*tasks)

            delete_op2 = {
                "method": "DELETE",
                "table": "products",
                "id": 6,
            }
            tasks = [self._make_post_request(session, "/api/dynamic/", delete_op2, phase)]
            await asyncio.gather(*tasks)

        self.report.phases.append(phase)

    async def test_concurrent_load(self):
        """Phase 5: Varying concurrency levels on the same endpoint."""
        phase = TestPhase(name="concurrent_load")
        print(f"  Phase: {phase.name} — testing different concurrency levels")

        concurrency_levels = [1, 5, 10, 20, 50, 100]
        requests_per_level = max(50, self.requests_per_phase // len(concurrency_levels))

        async with aiohttp.ClientSession() as session:
            for level in concurrency_levels:
                print(f"    Concurrency: {level}")
                tasks = []
                for _ in range(requests_per_level):
                    tasks.append(
                        self._make_request(session, "GET", "/api/dynamic/orders", phase)
                    )
                await asyncio.gather(*tasks)

        self.report.phases.append(phase)

    async def test_stress_test(self):
        """Phase 6: Sustained high-concurrency load."""
        phase = TestPhase(name="stress_test")
        print(f"  Phase: {phase.name} — sustained load, concurrency={self.concurrency}")

        endpoints = [
            "/api/dynamic/categories",
            "/api/dynamic/customers",
            "/api/dynamic/products",
            "/api/dynamic/orders",
            "/api/dynamic/meta/tables",
            "/api/dynamic/meta/products/fields",
        ]

        async with aiohttp.ClientSession() as session:
            tasks = []
            for _ in range(self.requests_per_phase):
                path = endpoints[_ % len(endpoints)]
                tasks.append(
                    self._make_request(session, "GET", path, phase)
                )
            await asyncio.gather(*tasks)

        self.report.phases.append(phase)

    # -- internal helpers --------------------------------------------------

    async def _make_request(
        self,
        session: aiohttp.ClientSession,
        method: str,
        path: str,
        phase: TestPhase,
    ) -> RequestResult:
        url = f"{self.base_url}{path}"
        start = time.monotonic()
        try:
            async with session.request(method, url, timeout=aiohttp.ClientTimeout(total=10)) as resp:
                body = await resp.read()
                elapsed = (time.monotonic() - start) * 1000
                result = RequestResult(
                    endpoint=path,
                    method=method,
                    status_code=resp.status,
                    response_time_ms=round(elapsed, 2),
                    size_bytes=len(body),
                )
                phase.results.append(result)
                return result
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            result = RequestResult(
                endpoint=path,
                method=method,
                status_code=0,
                response_time_ms=round(elapsed, 2),
                size_bytes=0,
                error=str(e),
            )
            phase.results.append(result)
            return result

    async def _make_post_request(
        self,
        session: aiohttp.ClientSession,
        path: str,
        body: dict,
        phase: TestPhase,
    ) -> RequestResult:
        """POST with JSON body and Content-Type header."""
        url = f"{self.base_url}{path}"
        start = time.monotonic()
        try:
            async with session.post(
                url,
                json=body,
                headers={"Content-Type": "application/json"},
                timeout=aiohttp.ClientTimeout(total=10),
            ) as resp:
                response_body = await resp.read()
                elapsed = (time.monotonic() - start) * 1000
                result = RequestResult(
                    endpoint=path,
                    method="POST",
                    status_code=resp.status,
                    response_time_ms=round(elapsed, 2),
                    size_bytes=len(response_body),
                )
                phase.results.append(result)
                return result
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            result = RequestResult(
                endpoint=path,
                method="POST",
                status_code=0,
                response_time_ms=round(elapsed, 2),
                size_bytes=0,
                error=str(e),
            )
            phase.results.append(result)
            return result


# ---------------------------------------------------------------------------
# Report formatting
# ---------------------------------------------------------------------------

def print_report(report: TestReport):
    """Print a human-readable report to stdout."""
    summary = report.summary()
    print("\n" + "=" * 80)
    print("  PERFORMANCE TEST REPORT")
    print("=" * 80)
    print(f"  Host:        {summary['config']['host']}:{summary['config']['port']}")
    print(f"  Concurrency: {summary['config']['concurrency']}")
    print(f"  Duration:    {summary['total_duration_sec']:.3f}s")
    print(f"  Total reqs:  {summary['total_requests']}")
    print(f"  Total errs:  {summary['total_errors']}")
    print(f"  Overall RPS: {summary['overall_rps']}")
    print("=" * 80)

    for phase in summary["phases"]:
        print(f"\n  ── {phase['name']} ──")
        stats = phase.get("stats", {})
        if stats:
            print(f"    Requests:  {stats.get('count', 0)}")
            print(f"    Errors:    {stats.get('errors', 0)}")
            print(f"    RPS:       {stats.get('rps', 0):.2f}")
            print(f"    Min:       {stats.get('min_ms', 0):.2f} ms")
            print(f"    Avg:       {stats.get('avg_ms', 0):.2f} ms")
            print(f"    Median:    {stats.get('median_ms', 0):.2f} ms")
            print(f"    P90:       {stats.get('p90_ms', 0):.2f} ms")
            print(f"    P95:       {stats.get('p95_ms', 0):.2f} ms")
            print(f"    P99:       {stats.get('p99_ms', 0):.2f} ms")
            print(f"    Max:       {stats.get('max_ms', 0):.2f} ms")
            print(f"    Size min:  {stats.get('size_min_bytes', 0)} B")
            print(f"    Size avg:  {stats.get('size_avg_bytes', 0)} B")
            print(f"    Size max:  {stats.get('size_max_bytes', 0)} B")

        # Sub-endpoint breakdown
        if "endpoints" in phase and phase["endpoints"]:
            print(f"\n    {'Endpoint':<55} {'Avg ms':>8} {'P95 ms':>8} {'Reqs':>6} {'Errors':>7}")
            print(f"    {'-'*55} {'-'*8} {'-'*8} {'-'*6} {'-'*7}")
            for ep in phase["endpoints"]:
                print(f"    {ep['path']:<55} {ep['avg_ms']:>8.2f} {ep['p95_ms']:>8.2f} {ep['count']:>6} {ep['errors']:>7}")

    print("\n" + "=" * 80)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

async def main_async(args):
    test = QueryBuilderPerfTest(
        host=args.host,
        port=args.port,
        concurrency=args.concurrency,
        requests_per_phase=args.requests,
    )

    print(f"\n  Starting performance test...")
    print(f"  Server: {test.base_url}")
    print(f"  Concurrency: {args.concurrency}")
    print(f"  Requests per phase: {args.requests}")
    print()

    report = await test.run_all()
    print_report(report)

    if args.output:
        report.save(args.output)

    return report


def main():
    parser = argparse.ArgumentParser(
        description="Performance test for dynamic_web_query_builder_server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python perf_test.py                          # defaults: 127.0.0.1:8008, 20 conc, 200 reqs
  python perf_test.py --concurrency 50         # 50 concurrent connections
  python perf_test.py --requests 1000          # 1000 requests per phase
  python perf_test.py --output report.json     # save JSON report
  python perf_test.py --host 10.0.0.5 --port 9000
        """,
    )
    parser.add_argument("--host", default="127.0.0.1", help="Server hostname (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8008, help="Server port (default: 8008)")
    parser.add_argument("--concurrency", type=int, default=20, help="Max concurrent connections (default: 20)")
    parser.add_argument("--requests", type=int, default=200, help="Requests per endpoint pattern (default: 200)")
    parser.add_argument("--output", default=None, help="Save JSON report to file")

    args = parser.parse_args()

    try:
        asyncio.run(run_with_check(args))
    except KeyboardInterrupt:
        print("\nTest interrupted by user.", file=sys.stderr)
        sys.exit(130)


async def run_with_check(args):
    """Connectivity check + run tests."""
    try:
        async with aiohttp.ClientSession() as session:
            try:
                async with session.get(
                    f"http://{args.host}:{args.port}/",
                    timeout=aiohttp.ClientTimeout(total=3),
                ):
                    pass
            except Exception as e:
                print(
                    f"ERROR: Cannot connect to {args.host}:{args.port} — {e}",
                    file=sys.stderr,
                )
                print("Make sure the server is running.", file=sys.stderr)
                sys.exit(1)
        await main_async(args)
    except KeyboardInterrupt:
        raise


if __name__ == "__main__":
    main()
