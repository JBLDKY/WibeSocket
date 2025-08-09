#!/usr/bin/env python3
import argparse
import asyncio
import os
import subprocess
import sys
import time
from typing import List, Optional, Tuple

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")


def run_cmd(cmd: List[str]) -> Tuple[int, str, str]:
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out, err = p.communicate()
    return p.returncode, (out or "").strip(), (err or "").strip()


def ensure_built() -> None:
    if not os.path.isdir(BUILD):
        rc, out, err = run_cmd(["cmake", "-S", ROOT, "-B", BUILD])
        if rc != 0:
            print(err or out, file=sys.stderr)
            sys.exit(2)
    for tgt in ("bench_throughput", "bench_latency"):
        exe = os.path.join(BUILD, tgt)
        if not os.path.exists(exe):
            rc, out, err = run_cmd(["cmake", "--build", BUILD, "--target", tgt, "-j"])
            if rc != 0:
                print(err or out, file=sys.stderr)
                sys.exit(2)


async def ws_websockets_throughput(uri: str, payload_len: int, count: int) -> float:
    try:
        import websockets  # type: ignore
    except Exception:
        return float("nan")
    payload = b"A" * payload_len
    start = time.perf_counter()
    async with websockets.connect(uri, max_size=None) as ws:
        for _ in range(count):
            await ws.send(payload)
            await ws.recv()
    elapsed = time.perf_counter() - start
    return count / elapsed if elapsed > 0 else float("inf")


async def ws_websockets_latency(uri: str, iters: int) -> Tuple[float, float, float]:
    try:
        import websockets  # type: ignore
    except Exception:
        return (float("nan"), float("nan"), float("nan"))
    samples = []
    async with websockets.connect(uri, max_size=None) as ws:
        for _ in range(iters):
            t0 = time.perf_counter()
            await ws.send(b"x")
            await ws.recv()
            t1 = time.perf_counter()
            samples.append((t1 - t0) * 1000.0)
    samples.sort()
    n = len(samples)
    def pct(p: float) -> float:
        i = min(max(int(n * p), 0), n - 1) if n else 0
        return samples[i] if n else float("nan")
    return (pct(0.50), pct(0.90), pct(0.99))


def bench_ours_throughput(uri: str, payload_len: int, count: int) -> Optional[float]:
    ensure_built()
    exe = os.path.join(BUILD, "bench_throughput")
    rc, out, err = run_cmd([exe, uri, str(payload_len), str(count)])
    if rc != 0:
        print(err or out, file=sys.stderr)
        return None
    try:
        last = out.strip().splitlines()[-1]
        parts = last.split()
        for token in parts:
            if token.startswith("msgs/s="):
                return float(token.split("=", 1)[1])
    except Exception:
        pass
    return None


def bench_ours_latency(uri: str, iters: int) -> Optional[Tuple[float, float, float]]:
    ensure_built()
    exe = os.path.join(BUILD, "bench_latency")
    rc, out, err = run_cmd([exe, uri, str(iters)])
    if rc != 0:
        print(err or out, file=sys.stderr)
        return None
    try:
        last = out.strip().splitlines()[-1]
        tokens = last.replace(",", " ").replace("=", " ").split()
        p50 = float(tokens[tokens.index("p50") + 1].rstrip("ms"))
        p90 = float(tokens[tokens.index("p90") + 1].rstrip("ms"))
        p99 = float(tokens[tokens.index("p99") + 1].rstrip("ms"))
        return (p50, p90, p99)
    except Exception:
        return None


def main() -> None:
    ap = argparse.ArgumentParser(description="Run WibeSocket benchmarks and compare with Python websockets")
    ap.add_argument("uri", nargs="?", default=os.environ.get("WIBESOCKET_BENCH_URI"), help="ws://host:port/path echo endpoint")
    ap.add_argument("--sizes", nargs="*", type=int, default=[125, 16 * 1024, 64 * 1024], help="payload sizes for throughput")
    ap.add_argument("--count", type=int, default=50000, help="messages per size for throughput")
    ap.add_argument("--iters", type=int, default=2000, help="iterations for latency")
    args = ap.parse_args()
    if not args.uri:
        print("Provide URI via arg or WIBESOCKET_BENCH_URI", file=sys.stderr)
        sys.exit(2)

    print(f"URI: {args.uri}")
    print("== Throughput (round-trip msgs/s) ==")
    for sz in args.sizes:
        ours = bench_ours_throughput(args.uri, sz, args.count)
        print(f" ours  sz={sz:6d}: {ours:.2f} msgs/s" if ours else f" ours  sz={sz:6d}: n/a")
        try:
            t = asyncio.run(ws_websockets_throughput(args.uri, sz, min(args.count, 20000)))
            print(f" websockets sz={sz:6d}: {t:.2f} msgs/s")
        except Exception as e:
            print(f" websockets sz={sz:6d}: n/a ({e})")

    print("\n== Latency (ms) ==")
    ol = bench_ours_latency(args.uri, args.iters)
    if ol:
        print(f" ours: p50={ol[0]:.3f} p90={ol[1]:.3f} p99={ol[2]:.3f}")
    try:
        p50, p90, p99 = asyncio.run(ws_websockets_latency(args.uri, args.iters))
        print(f" websockets: p50={p50:.3f} p90={p90:.3f} p99={p99:.3f}")
    except Exception as e:
        print(f" websockets: n/a ({e})")

    print("\nTip: install 'websockets' for Python comparison: pip install websockets")

if __name__ == "__main__":
    main()


