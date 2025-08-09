#!/usr/bin/env python3
import argparse
import asyncio
import urllib.parse
import json
import socket
import os
import subprocess
import shutil
import sys
import time
from typing import Dict, List, Optional, Tuple

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
    # Try to build libwebsockets client if present; ignore failures
    rc, out, err = run_cmd(["cmake", "--build", BUILD, "--target", "bench_lws_client", "-j"])


def ensure_python_package(mod_name: str, pip_name: Optional[str] = None) -> bool:
    try:
        __import__(mod_name)
        return True
    except Exception:
        pass
    pip = [sys.executable, "-m", "pip", "install"]
    try:
        subprocess.check_call(pip + [pip_name or mod_name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        __import__(mod_name)
        return True
    except Exception:
        return False


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


async def aiohttp_throughput(uri: str, payload_len: int, count: int, timeout_s: float = 5.0) -> float:
    try:
        import aiohttp  # type: ignore
    except Exception:
        return float("nan")
    payload = b"A" * payload_len
    timeout = aiohttp.ClientTimeout(total=None, sock_read=timeout_s, sock_connect=timeout_s)
    start = time.perf_counter()
    async with aiohttp.ClientSession(timeout=timeout) as session:
        async with session.ws_connect(uri, timeout=timeout_s, autoping=True, protocols=()) as ws:
            for _ in range(count):
                await ws.send_bytes(payload)
                msg = await ws.receive(timeout=timeout_s)
                if msg.type != aiohttp.WSMsgType.BINARY and msg.type != aiohttp.WSMsgType.TEXT:
                    break
    elapsed = time.perf_counter() - start
    return count / elapsed if elapsed > 0 else float("inf")


async def aiohttp_latency(uri: str, iters: int, timeout_s: float = 5.0) -> Tuple[float, float, float]:
    try:
        import aiohttp  # type: ignore
    except Exception:
        return (float("nan"), float("nan"), float("nan"))
    timeout = aiohttp.ClientTimeout(total=None, sock_read=timeout_s, sock_connect=timeout_s)
    samples: List[float] = []
    async with aiohttp.ClientSession(timeout=timeout) as session:
        async with session.ws_connect(uri, timeout=timeout_s, autoping=True, protocols=()) as ws:
            for _ in range(iters):
                t0 = time.perf_counter()
                await ws.send_bytes(b"x")
                msg = await ws.receive(timeout=timeout_s)
                if msg.type == aiohttp.WSMsgType.CLOSED:
                    break
                t1 = time.perf_counter()
                samples.append((t1 - t0) * 1000.0)
    samples.sort()
    n = len(samples)
    if not n:
        return (float("nan"), float("nan"), float("nan"))
    def pct(p: float) -> float:
        i = min(max(int(n * p), 0), n - 1)
        return samples[i]
    return (pct(0.50), pct(0.90), pct(0.99))


def ws_websocket_client_throughput(uri: str, payload_len: int, count: int, timeout_s: float = 5.0) -> float:
    try:
        import websocket  # type: ignore
    except Exception:
        return float("nan")
    payload = b"A" * payload_len
    ws = websocket.create_connection(uri, timeout=timeout_s)
    ws.settimeout(timeout_s)
    start = time.perf_counter()
    try:
        for _ in range(count):
            ws.send(payload, opcode=2)
            _ = ws.recv()
    except Exception:
        pass
    finally:
        try:
            ws.close()
        except Exception:
            pass
    elapsed = time.perf_counter() - start
    return count / elapsed if elapsed > 0 else float("inf")


def websocat_throughput(uri: str, payload_len: int, count: int) -> float:
    """Use websocat to send newline-terminated payloads and await echo; approximate throughput."""
    websocat = shutil.which("websocat")
    if not websocat:
        return float("nan")
    payload = ("A" * max(1, payload_len)).encode()
    try:
        p = subprocess.Popen([websocat, "-t", uri], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=False, bufsize=0)
    except Exception:
        return float("nan")
    start = time.perf_counter()
    ok = 0
    try:
        for _ in range(count):
            line = payload + b"\n"
            p.stdin.write(line)
            p.stdin.flush()
            out = b""
            while not out.endswith(b"\n"):
                ch = p.stdout.read(1)
                if not ch:
                    raise RuntimeError("websocat closed")
                out += ch
            ok += 1
    except Exception:
        pass
    finally:
        try:
            p.terminate()
        except Exception:
            pass
    elapsed = time.perf_counter() - start
    return ok / elapsed if elapsed > 0 else float("inf")


def ws_websocket_client_latency(uri: str, iters: int, timeout_s: float = 5.0) -> Tuple[float, float, float]:
    try:
        import websocket  # type: ignore
    except Exception:
        return (float("nan"), float("nan"), float("nan"))
    ws = websocket.create_connection(uri, timeout=timeout_s)
    ws.settimeout(timeout_s)
    samples: List[float] = []
    try:
        for _ in range(iters):
            t0 = time.perf_counter()
            ws.send(b"x", opcode=2)
            _ = ws.recv()
            t1 = time.perf_counter()
            samples.append((t1 - t0) * 1000.0)
    except Exception:
        pass
    finally:
        try:
            ws.close()
        except Exception:
            pass
    samples.sort()
    n = len(samples)
    if not n:
        return (float("nan"), float("nan"), float("nan"))
    def pct(p: float) -> float:
        i = min(max(int(n * p), 0), n - 1)
        return samples[i]
    return (pct(0.50), pct(0.90), pct(0.99))


def websocat_latency(uri: str, iters: int) -> Tuple[float, float, float]:
    websocat = shutil.which("websocat")
    if not websocat:
        return (float("nan"), float("nan"), float("nan"))
    try:
        p = subprocess.Popen([websocat, "-t", uri], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=False, bufsize=0)
    except Exception:
        return (float("nan"), float("nan"), float("nan"))
    samples: List[float] = []
    try:
        for _ in range(iters):
            t0 = time.perf_counter()
            p.stdin.write(b"x\n"); p.stdin.flush()
            out = b""
            while not out.endswith(b"\n"):
                ch = p.stdout.read(1)
                if not ch:
                    raise RuntimeError("websocat closed")
                out += ch
            t1 = time.perf_counter()
            samples.append((t1 - t0) * 1000.0)
    except Exception:
        pass
    finally:
        try:
            p.terminate()
        except Exception:
            pass
    samples.sort()
    n = len(samples)
    if not n:
        return (float("nan"), float("nan"), float("nan"))
    def pct(pv: float) -> float:
        i = min(max(int(n * pv), 0), n - 1)
        return samples[i]
    return (pct(0.50), pct(0.90), pct(0.99))


def lws_client_throughput(uri: str, payload_len: int, count: int) -> float:
    exe = os.path.join(BUILD, "bench_lws_client")
    if not os.path.exists(exe):
        return float("nan")
    rc, out, err = run_cmd([exe, uri, str(payload_len), str(count)])
    if rc != 0:
        return float("nan")
    for tok in out.split():
        if tok.startswith("lws") and "msgs/s=" in tok:
            try:
                return float(tok.split("=", 1)[1])
            except Exception:
                return float("nan")
    return float("nan")


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
    ap.add_argument("--install-missing", action="store_true", help="attempt to pip install missing python competitors in this interpreter")
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
                if args.install_missing:
                    ensure_python_package("websockets", "websockets")
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
    results: Dict[str, Dict] = {"uri": args.uri, "throughput": {}, "latency": {}}
    print("== Throughput (round-trip msgs/s) ==")
    for sz in args.sizes:
        ours = bench_ours_throughput(args.uri, sz, args.count)
        print(f" ours  sz={sz:6d}: {ours:.2f} msgs/s" if ours else f" ours  sz={sz:6d}: n/a")
        # Python websockets
        if args.install_missing:
            ensure_python_package("websockets", "websockets")
        try:
            t = asyncio.run(ws_websockets_throughput(args.uri, sz, min(args.count, 20000)))
            print(f" websockets sz={sz:6d}: {t:.2f} msgs/s")
        except Exception as e:
            print(f" websockets sz={sz:6d}: n/a ({e})")
            t = None
        # Python websocket-client (sync)
        if args.install_missing:
            ensure_python_package("websocket", "websocket-client")
        tc = ws_websocket_client_throughput(args.uri, sz, min(args.count, 20000))
        if tc == tc:  # not NaN
            print(f" websocket-client sz={sz:6d}: {tc:.2f} msgs/s")
        else:
            print(f" websocket-client sz={sz:6d}: n/a")
        # aiohttp (async)
        if args.install_missing:
            ensure_python_package("aiohttp", "aiohttp")
        try:
            ta = asyncio.run(aiohttp_throughput(args.uri, sz, min(args.count, 20000)))
            if ta == ta:
                print(f" aiohttp sz={sz:6d}: {ta:.2f} msgs/s")
            else:
                print(f" aiohttp sz={sz:6d}: n/a")
        except Exception:
            ta = float("nan")
            print(f" aiohttp sz={sz:6d}: n/a")
        # websocat (cli)
        tw = websocat_throughput(args.uri, sz, min(args.count, 20000))
        if tw == tw:
            print(f" websocat sz={sz:6d}: {tw:.2f} msgs/s")
        else:
            print(f" websocat sz={sz:6d}: n/a")
        # libwebsockets C client (optional)
        tlws = lws_client_throughput(args.uri, sz, min(args.count, 20000))
        if tlws == tlws:
            print(f" libwebsockets sz={sz:6d}: {tlws:.2f} msgs/s")
        else:
            print(f" libwebsockets sz={sz:6d}: n/a")
        results["throughput"][str(sz)] = {
            "ours_msgs_per_sec": float(ours) if ours else None,
            "websockets_msgs_per_sec": float(t) if t else None,
            "websocket_client_msgs_per_sec": float(tc) if tc == tc else None,
            "aiohttp_msgs_per_sec": float(ta) if ta == ta else None,
            "websocat_msgs_per_sec": float(tw) if tw == tw else None,
            "libwebsockets_msgs_per_sec": float(tlws) if tlws == tlws else None,
        }

    print("\n== Latency (ms) ==")
    ol = bench_ours_latency(args.uri, args.iters)
    if ol:
        print(f" ours: p50={ol[0]:.3f} p90={ol[1]:.3f} p99={ol[2]:.3f}")
    if args.install_missing:
        ensure_python_package("websockets", "websockets")
        ensure_python_package("websocket", "websocket-client")
        ensure_python_package("aiohttp", "aiohttp")
    try:
        p50, p90, p99 = asyncio.run(ws_websockets_latency(args.uri, args.iters))
        print(f" websockets: p50={p50:.3f} p90={p90:.3f} p99={p99:.3f}")
    except Exception as e:
        print(f" websockets: n/a ({e})")
        p50 = p90 = p99 = None
    try:
        cp50, cp90, cp99 = ws_websocket_client_latency(args.uri, args.iters)
        if cp50 == cp50:
            print(f" websocket-client: p50={cp50:.3f} p90={cp90:.3f} p99={cp99:.3f}")
        else:
            print(" websocket-client: n/a")
    except Exception:
        cp50 = cp90 = cp99 = None
    try:
        ap50, ap90, ap99 = asyncio.run(aiohttp_latency(args.uri, args.iters))
        if ap50 == ap50:
            print(f" aiohttp: p50={ap50:.3f} p90={ap90:.3f} p99={ap99:.3f}")
        else:
            print(" aiohttp: n/a")
    except Exception:
        ap50 = ap90 = ap99 = None

    # websocat latency (optional)
    try:
        swp50, swp90, swp99 = websocat_latency(args.uri, args.iters)
        if swp50 == swp50:
            print(f" websocat: p50={swp50:.3f} p90={swp90:.3f} p99={swp99:.3f}")
        else:
            print(" websocat: n/a")
    except Exception:
        swp50 = swp90 = swp99 = None

    if ol:
        results["latency"]["ours_ms"] = {"p50": float(ol[0]), "p90": float(ol[1]), "p99": float(ol[2])}
    else:
        results["latency"]["ours_ms"] = {"p50": None, "p90": None, "p99": None}
    results["latency"]["websockets_ms"] = {"p50": (float(p50) if p50 is not None else None),
                                              "p90": (float(p90) if p90 is not None else None),
                                              "p99": (float(p99) if p99 is not None else None)}
    results["latency"]["websocket_client_ms"] = {"p50": (float(cp50) if 'cp50' in locals() and cp50 == cp50 else None),
                                                   "p90": (float(cp90) if 'cp90' in locals() and cp90 == cp90 else None),
                                                   "p99": (float(cp99) if 'cp99' in locals() and cp99 == cp99 else None)}
    results["latency"]["aiohttp_ms"] = {"p50": (float(ap50) if 'ap50' in locals() and ap50 == ap50 else None),
                                          "p90": (float(ap90) if 'ap90' in locals() and ap90 == ap90 else None),
                                          "p99": (float(ap99) if 'ap99' in locals() and ap99 == ap99 else None)}
    if 'swp50' in locals():
        results["latency"]["websocat_ms"] = {"p50": (float(swp50) if swp50 == swp50 else None),
                                               "p90": (float(swp90) if swp90 == swp90 else None),
                                               "p99": (float(swp99) if swp99 == swp99 else None)}

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
    md_lines.append("| Size | Ours | websockets | websocket-client | aiohttp | websocat | libwebsockets |\n|---:|---:|---:|---:|---:|---:|---:|")
    for sz in args.sizes:
        row = results["throughput"].get(str(sz), {})
        ours_v = row.get("ours_msgs_per_sec")
        ws_v = row.get("websockets_msgs_per_sec")
        wsc_v = row.get("websocket_client_msgs_per_sec")
        aio_v = row.get("aiohttp_msgs_per_sec")
        wc_v = row.get("websocat_msgs_per_sec")
        lws_v = row.get("libwebsockets_msgs_per_sec")
        md_lines.append(f"| {sz} | {ours_v if ours_v is not None else 'n/a'} | {ws_v if ws_v is not None else 'n/a'} | {wsc_v if wsc_v is not None else 'n/a'} | {aio_v if aio_v is not None else 'n/a'} | {wc_v if wc_v is not None else 'n/a'} | {lws_v if lws_v is not None else 'n/a'} |")
    md_lines.append("\n## Latency (ms)\n")
    lm = results["latency"]
    md_lines.append("| Impl | p50 | p90 | p99 |\n|:--|--:|--:|--:|")
    om = lm.get("ours_ms", {})
    wm = lm.get("websockets_ms", {})
    md_lines.append(f"| Ours | {om.get('p50','n/a')} | {om.get('p90','n/a')} | {om.get('p99','n/a')} |")
    md_lines.append(f"| websockets | {wm.get('p50','n/a')} | {wm.get('p90','n/a')} | {wm.get('p99','n/a')} |")
    wcm = lm.get("websocket_client_ms", {})
    md_lines.append(f"| websocket-client | {wcm.get('p50','n/a')} | {wcm.get('p90','n/a')} | {wcm.get('p99','n/a')} |")
    am = lm.get("aiohttp_ms", {})
    md_lines.append(f"| aiohttp | {am.get('p50','n/a')} | {am.get('p90','n/a')} | {am.get('p99','n/a')} |")
    # include websocat if available (latency)
    j = os.path.join(out_dir, "latest.json")
    wsm = results["latency"].get("websocat_ms", {}) if "websocat_ms" in results["latency"] else {}
    if wsm:
        md_lines.append(f"| websocat | {wsm.get('p50','n/a')} | {wsm.get('p90','n/a')} | {wsm.get('p99','n/a')} |")
    md_lines.append("\n")
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


