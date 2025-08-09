#!/usr/bin/env python3
"""
Showcase the WibeSocket Python API (sync + asyncio).

Usage:
  python examples/py_showcase.py ws://echo.websocket.events
  or
  WIBESOCKET_URI=ws://127.0.0.1:8765 python examples/py_showcase.py
"""
import os
import sys
import time
import asyncio
import contextlib

import wibesocket
from wibesocket_wrappers import WebSocket, AsyncWebSocket


def sync_demo(uri: str) -> None:
    print("[sync] connecting:", uri)
    ws = WebSocket.connect(uri, handshake_timeout_ms=5000, max_frame_size=1 << 20)

    # Zero-copy context manager for memoryview + auto-release
    @contextlib.contextmanager
    def recv_mem(timeout_ms: int = 1000):
        fr = ws.recv(timeout_ms=timeout_ms)
        if fr is None:
            yield None
            return
        try:
            yield fr
        finally:
            fr.release()

    ok = True
    ws.send_text(f"hello-sync-{int(time.time())}")
    print("[sync] send_text:", ok)

    with recv_mem(2000) as item:
        if item is None:
            print("[sync] recv timeout")
        else:
            fr = item
            print("[sync] recv:", fr.type, fr.text(errors="ignore"), "final=", fr.is_final)

    ws.close(1000, "bye")


async def asyncio_demo(uri: str) -> None:
    print("[asyncio] connecting:", uri)
    aws = await AsyncWebSocket.connect(uri, handshake_timeout_ms=5000, max_frame_size=1 << 20)
    try:
        aws.send_text(f"hello-async-{int(time.time())}")
        fr = await aws.recv(timeout=3.0)
        with fr:
            print("[asyncio] recv:", fr.type, fr.text(errors="ignore"), "final=", fr.is_final)
    finally:
        aws.close(1000, "bye")


def main() -> None:
    uri = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("WIBESOCKET_URI", "ws://echo.websocket.events")
    sync_demo(uri)
    try:
        asyncio.run(asyncio_demo(uri))
    except RuntimeError:
        # If already in an event loop (e.g., in notebooks), just run sync demo
        pass


if __name__ == "__main__":
    main()


