#!/usr/bin/env python3
import argparse
import asyncio
import urllib.parse
import json
import socket
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


async def ws_websockets_throughput(uri: str, payload_len: int, count: int, timeout_s: float = 5.0) -> float:
    try:
        import websockets  # type: ignore
    except Exception:
        return float("nan")
    payload = b"A" * payload_len
    start = time.perf_counter()
    try:
        conn = await asyncio.wait_for(websockets.connect(uri, max_size=None), timeout=timeout_s)
    except Exception:
        return float("nan")
    async with conn as ws:
        for _ in range(count):
            try:
                await asyncio.wait_for(ws.send(payload), timeout=timeout_s)
                await asyncio.wait_for(ws.recv(), timeout=timeout_s)
            except Exception:
                break
    elapsed = time.perf_counter() - start
    return count / elapsed if elapsed > 0 else float("inf")


async def ws_websockets_latency(uri: str, iters: int, timeout_s: float = 5.0) -> Tuple[float, float, float]:
    try:
        import websockets  # type: ignore
    except Exception:
        return (float("nan"), float("nan"), float("nan"))
    samples = []
    try:
        conn = await asyncio.wait_for(websockets.connect(uri, max_size=None), timeout=timeout_s)
    except Exception:
        return (float("nan"), float("nan"), float("nan"))
    async with conn as ws:
        for _ in range(iters):
            t0 = time.perf_counter()
            try:
                await asyncio.wait_for(ws.send(b"x"), timeout=timeout_s)
                await asyncio.wait_for(ws.recv(), timeout=timeout_s)
            except Exception:
                break
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

    # Auto-start a local echo server if URI targets localhost and 'websockets' is available
    echo_proc = None
    echo_thread = None
    echo_stop = None
    try:
        parsed = urllib.parse.urlsplit(args.uri)
        host = parsed.hostname or ""
        port = parsed.port
        if host in ("127.0.0.1", "localhost", "::1"):
            try:
                import websockets  # type: ignore
                import threading, queue
                addr_host = host if host != "localhost" else "127.0.0.1"
                port_q: "queue.Queue[int]" = queue.Queue(maxsize=1)
                stop_ev = threading.Event()

                def _run_server():
                    import asyncio as _aio
                    async def echo(ws):
                        async for m in ws:
                            await ws.send(m)
                    async def runner():
                        server = await websockets.serve(echo, addr_host, 0, max_size=None)
                        sel_port = server.sockets[0].getsockname()[1]
                        port_q.put(sel_port)
                        try:
                            while not stop_ev.is_set():
                                await _aio.sleep(0.1)
                        finally:
                            server.close()
                            await server.wait_closed()
                    _aio.run(runner())

                t = threading.Thread(target=_run_server, daemon=True)
                t.start()
                try:
                    sel_port = port_q.get(timeout=5.0)
                    args.uri = f"ws://{addr_host}:{sel_port}/"
                    echo_thread = t
                    echo_stop = stop_ev
                except Exception:
                    print("Warning: local echo server failed to start within timeout", file=sys.stderr)
            except Exception:
                print("Note: couldn't auto-start local echo server (install 'websockets' in this interpreter)", file=sys.stderr)
    except Exception:
        pass

    print(f"URI: {args.uri}")
    results = {"uri": args.uri, "throughput": {}, "latency": {}}
    print("== Throughput (round-trip msgs/s) ==")
    for sz in args.sizes:
        ours = bench_ours_throughput(args.uri, sz, args.count)
        print(f" ours  sz={sz:6d}: {ours:.2f} msgs/s" if ours else f" ours  sz={sz:6d}: n/a")
        try:
            t = asyncio.run(ws_websockets_throughput(args.uri, sz, min(args.count, 20000)))
            print(f" websockets sz={sz:6d}: {t:.2f} msgs/s")
        except Exception as e:
            print(f" websockets sz={sz:6d}: n/a ({e})")
            t = None
        results["throughput"][str(sz)] = {
            "ours_msgs_per_sec": float(ours) if ours else None,
            "websockets_msgs_per_sec": float(t) if t else None,
        }

    print("\n== Latency (ms) ==")
    ol = bench_ours_latency(args.uri, args.iters)
    if ol:
        print(f" ours: p50={ol[0]:.3f} p90={ol[1]:.3f} p99={ol[2]:.3f}")
    try:
        p50, p90, p99 = asyncio.run(ws_websockets_latency(args.uri, args.iters))
        print(f" websockets: p50={p50:.3f} p90={p90:.3f} p99={p99:.3f}")
    except Exception as e:
        print(f" websockets: n/a ({e})")
        p50 = p90 = p99 = None

    if ol:
        results["latency"]["ours_ms"] = {"p50": float(ol[0]), "p90": float(ol[1]), "p99": float(ol[2])}
    else:
        results["latency"]["ours_ms"] = {"p50": None, "p90": None, "p99": None}
    results["latency"]["websockets_ms"] = {"p50": (float(p50) if p50 is not None else None),
                                              "p90": (float(p90) if p90 is not None else None),
                                              "p99": (float(p99) if p99 is not None else None)}

    print("\nTip: install 'websockets' for Python comparison: pip install websockets")

    # Persist JSON and Markdown summary in bench/
    out_dir = os.path.join(ROOT, "bench", "results")
    os.makedirs(out_dir, exist_ok=True)
    json_path = os.path.join(out_dir, "latest.json")
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)

    md_lines = []
    md_lines.append(f"# WibeSocket Benchmarks\n\nURI: `{results['uri']}`\n")
    md_lines.append("## Throughput (msgs/s)\n")
    md_lines.append("| Size | Ours | websockets |\n|---:|---:|---:|")
    for sz in args.sizes:
        row = results["throughput"].get(str(sz), {})
        ours_v = row.get("ours_msgs_per_sec")
        ws_v = row.get("websockets_msgs_per_sec")
        md_lines.append(f"| {sz} | {ours_v if ours_v is not None else 'n/a'} | {ws_v if ws_v is not None else 'n/a'} |")
    md_lines.append("\n## Latency (ms)\n")
    lm = results["latency"]
    md_lines.append("| Impl | p50 | p90 | p99 |\n|:--|--:|--:|--:|")
    om = lm.get("ours_ms", {})
    wm = lm.get("websockets_ms", {})
    md_lines.append(f"| Ours | {om.get('p50','n/a')} | {om.get('p90','n/a')} | {om.get('p99','n/a')} |")
    md_lines.append(f"| websockets | {wm.get('p50','n/a')} | {wm.get('p90','n/a')} | {wm.get('p99','n/a')} |\n")
    md_path = os.path.join(ROOT, "bench", "RESULTS.md")
    with open(md_path, "w") as f:
        f.write("\n".join(md_lines) + "\n")
    print(f"\nSaved: {json_path}\nSaved: {md_path}")

    # Tear down local echo server if we started one
    if echo_proc is not None:
        try:
            echo_proc.terminate()
        except Exception:
            pass
    if echo_stop is not None:
        echo_stop.set()

if __name__ == "__main__":
    main()


