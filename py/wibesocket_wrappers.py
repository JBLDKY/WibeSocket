from __future__ import annotations

import asyncio
import contextlib
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

import wibesocket as _c


class FrameType(IntEnum):
    CONTINUATION = 0x0
    TEXT = 0x1
    BINARY = 0x2
    CLOSE = 0x8
    PING = 0x9
    PONG = 0xA


@dataclass
class Frame:
    """Zero-copy frame wrapper. Use as a context manager or call release()."""

    conn: object  # capsule
    type: FrameType
    data: memoryview
    is_final: bool
    _released: bool = False

    def release(self) -> None:
        if not self._released:
            _c.release_payload(self.conn)
            self._released = True

    def __enter__(self) -> "Frame":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.release()

    def __del__(self) -> None:
        # Avoid C calls in GC context; rely on explicit release or context manager
        self._released = True

    def text(self, errors: str = "strict") -> str:
        return self.data.tobytes().decode("utf-8", errors)


class WebSocket:
    """Pythonic, zero-copy WebSocket client wrapper around the C extension."""

    def __init__(self, capsule: object):
        self._c = capsule

    @classmethod
    def connect(
        cls,
        uri: str,
        *,
        handshake_timeout_ms: int = 5000,
        max_frame_size: int = 1 << 20,
        user_agent: Optional[str] = None,
        origin: Optional[str] = None,
        protocol: Optional[str] = None,
    ) -> "WebSocket":
        c = _c.connect(
            uri,
            handshake_timeout_ms=handshake_timeout_ms,
            max_frame_size=max_frame_size,
            user_agent=user_agent,
            origin=origin,
            protocol=protocol,
        )
        if c is None:
            raise ConnectionError("wibesocket connect failed")
        return cls(c)

    def __enter__(self) -> "WebSocket":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    # Sending
    def send_text(self, data: str | bytes) -> None:
        ok = _c.send_text(self._c, data if isinstance(data, bytes) else data)
        if not ok:
            raise RuntimeError("send_text failed")

    def send_binary(self, data: bytes | memoryview) -> None:
        ok = _c.send_binary(self._c, memoryview(data))
        if not ok:
            raise RuntimeError("send_binary failed")

    # Receiving
    def recv(self, timeout_ms: int = 0) -> Optional[Frame]:
        res = _c.recv(self._c, timeout_ms=timeout_ms)
        if res is None:
            return None
        ftype, data, is_final = res
        return Frame(self._c, FrameType(ftype), data, bool(is_final))

    # Control
    def ping(self, data: bytes = b"") -> None:
        # PING is handled at C level; exposing here for API completeness
        ok = _c.send_binary(self._c, data) if False else True  # no-op helper
        if not ok:
            raise RuntimeError("ping failed")

    def close(self, code: int = 1000, reason: Optional[str] = None) -> None:
        # Explicit close order to avoid destructor-based closes
        _c.send_close(self._c, code, reason or "")
        _c.close(self._c)

    # Integration
    def fileno(self) -> int:
        return int(_c.fileno(self._c))


class AsyncWebSocket:
    """Async wrapper using asyncio add_reader for zero-copy reads."""

    def __init__(self, ws: WebSocket, loop: Optional[asyncio.AbstractEventLoop] = None):
        self._ws = ws
        self._loop = loop or asyncio.get_running_loop()
        self._fd = ws.fileno()
        self._fut: Optional[asyncio.Future] = None

    @classmethod
    async def connect(cls, uri: str, **kwargs) -> "AsyncWebSocket":
        return cls(WebSocket.connect(uri, **kwargs))

    def _on_readable(self) -> None:
        if not self._fut or self._fut.done():
            return
        fr = self._ws.recv(timeout_ms=0)
        if fr is not None:
            self._fut.set_result(fr)

    async def recv(self, timeout: float | None = None) -> Frame:
        self._fut = self._loop.create_future()
        self._loop.add_reader(self._fd, self._on_readable)
        try:
            return await asyncio.wait_for(self._fut, timeout=timeout)
        finally:
            self._loop.remove_reader(self._fd)

    # Proxy helpers
    def send_text(self, data: str | bytes) -> None:
        self._ws.send_text(data)

    def send_binary(self, data: bytes | memoryview) -> None:
        self._ws.send_binary(data)

    def close(self, code: int = 1000, reason: Optional[str] = None) -> None:
        self._ws.close(code, reason)


