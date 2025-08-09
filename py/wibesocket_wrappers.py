from __future__ import annotations

"""High-level, pythonic wrappers over the zero-copy C extension.

Exposes:
- FrameType: IntEnum of RFC 6455 frame types
- Frame: zero-copy frame wrapper (context-manageable), with helper to decode text
- WebSocket: async-first client with sync fallbacks
- AsyncWebSocket: thin asyncio adapter using add_reader (no background threads)

Design goals
- Zero-copy receive path: payload is a memoryview into the C buffer
- Explicit lifetime: call Frame.release() or use "with Frame:" to release promptly
- No hidden threads; non-blocking I/O via readiness polling
- Predictable ownership: you own the FD as long as the WebSocket exists

Notes
- Always release frames quickly to avoid backpressure on the recv buffer
- Close explicitly to teardown cleanly; do not rely on GC
"""

import asyncio
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

import wibesocket as _c


def _run_sync(coro):
    """Run an async coroutine from sync code safely."""
    try:
        loop = asyncio.get_event_loop()
        if loop.is_running():
            return asyncio.run(coro)
        return loop.run_until_complete(coro)
    except RuntimeError:
        return asyncio.run(coro)


class FrameType(IntEnum):
    """WebSocket frame types per RFC 6455."""

    CONTINUATION = 0x0
    TEXT = 0x1
    BINARY = 0x2
    CLOSE = 0x8
    PING = 0x9
    PONG = 0xA


@dataclass
class Frame:
    """Zero-copy received frame.

    Use as a context manager or call release() promptly to allow subsequent recv() calls.

    Attributes:
        conn: Capsule object referencing the underlying C connection
        type: FrameType for this frame
        data: memoryview over the payload (no copy)
        is_final: whether this is the final fragment in a message
    """

    conn: object  # C capsule
    type: FrameType
    data: memoryview
    is_final: bool
    _released: bool = False

    def release(self) -> None:
        """Release the pinned payload buffer back to the C layer."""
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
    """Zero-copy WebSocket client with async-first API and sync fallbacks."""

    def __init__(self, capsule: object):
        self._c = capsule

    @classmethod
    async def connect(
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

    @classmethod
    def connect_sync(
        cls,
        uri: str,
        *,
        handshake_timeout_ms: int = 5000,
        max_frame_size: int = 1 << 20,
        user_agent: Optional[str] = None,
        origin: Optional[str] = None,
        protocol: Optional[str] = None,
    ) -> "WebSocket":
        return _run_sync(
            cls.connect(
                uri,
                handshake_timeout_ms=handshake_timeout_ms,
                max_frame_size=max_frame_size,
                user_agent=user_agent,
                origin=origin,
                protocol=protocol,
            )
        )

    # Sending
    async def send(self, data: bytes | str) -> None:
        if isinstance(data, (bytes, memoryview)):
            ok = _c.send_binary(self._c, memoryview(data))
        else:
            ok = _c.send_text(self._c, data)
        if not ok:
            raise RuntimeError("send failed")

    def send_sync(self, data: bytes | str) -> None:
        _run_sync(self.send(data))

    # Receiving
    async def recv(self, timeout_ms: int = 0) -> Optional[Frame]:
        # Poll for readiness, then do a non-blocking recv
        ready = _c.poll_events(self._c, timeout_ms=timeout_ms)
        if not ready:
            return None
        res = _c.recv(self._c, timeout_ms=0)
        if res is None:
            return None
        ftype, data, is_final = res
        return Frame(self._c, FrameType(ftype), data, bool(is_final))

    def recv_sync(self, timeout_ms: int = 0) -> Optional[Frame]:
        return _run_sync(self.recv(timeout_ms=timeout_ms))

    async def ping(self, data: bytes = b"") -> None:
        return None

    async def close(self, code: int = 1000, reason: Optional[str] = None) -> None:
        _c.send_close(self._c, code, reason or "")
        _c.close(self._c)

    def close_sync(self, code: int = 1000, reason: Optional[str] = None) -> None:
        _run_sync(self.close(code, reason))

    # Integration
    def fileno(self) -> int:
        return int(_c.fileno(self._c))


class AsyncWebSocket:
    """Asyncio wrapper using add_reader for zero-copy reads."""

    def __init__(self, ws: WebSocket, loop: Optional[asyncio.AbstractEventLoop] = None):
        self._ws = ws
        self._loop = loop or asyncio.get_running_loop()
        self._fd = ws.fileno()
        self._fut: Optional[asyncio.Future] = None

    @classmethod
    async def connect(cls, uri: str, **kwargs) -> "AsyncWebSocket":
        return cls(await WebSocket.connect(uri, **kwargs))

    def _on_readable(self) -> None:
        if not self._fut or self._fut.done():
            return
        fr = self._ws.recv_sync(timeout_ms=0)
        if fr is not None:
            self._fut.set_result(fr)

    async def recv(self, timeout: float | None = None) -> Frame:
        """Await a frame with an optional timeout (seconds)."""
        self._fut = self._loop.create_future()
        self._loop.add_reader(self._fd, self._on_readable)
        try:
            return await asyncio.wait_for(self._fut, timeout=timeout)
        finally:
            self._loop.remove_reader(self._fd)

    # Proxy helpers
    def send_text(self, data: str | bytes) -> None:
        self._ws.send_sync(data)

    def send_binary(self, data: bytes | memoryview) -> None:
        self._ws.send_sync(data)

    def close(self, code: int = 1000, reason: Optional[str] = None) -> None:
        self._ws.close_sync(code, reason)

from __future__ import annotations

"""High-level, pythonic wrappers over the zero-copy C extension.

Exposes:
- FrameType: IntEnum of RFC 6455 frame types
- Frame: zero-copy frame wrapper (context-manageable), with helper to decode text
- WebSocket: async-first client with sync fallbacks
- AsyncWebSocket: thin asyncio adapter using add_reader

Design goals
- Zero-copy receive path: payload is a memoryview into the C buffer
- Explicit lifetime: call Frame.release() or use "with Frame:" to release promptly
- No hidden threads; non-blocking I/O via readiness polling
- Predictable ownership: you own the FD as long as the WebSocket exists

Notes
- Always release frames quickly to avoid backpressure on the recv buffer
- Close explicitly to teardown cleanly; do not rely on GC
"""

import asyncio
import contextlib
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

import wibesocket as _c


class FrameType(IntEnum):
    """WebSocket frame types per RFC 6455."""

    CONTINUATION = 0x0
    TEXT = 0x1
    BINARY = 0x2
    CLOSE = 0x8
    PING = 0x9
    PONG = 0xA


@dataclass
class Frame:
    """Zero-copy received frame.

    Use as a context manager or call release() promptly to allow subsequent recv() calls.

    Attributes:
        conn: Capsule object referencing the underlying C connection
        type: FrameType for this frame
        data: memoryview over the payload (no copy)
        is_final: whether this is the final fragment in a message
    """

    conn: object  # C capsule
    type: FrameType
    data: memoryview
    is_final: bool
    _released: bool = False

    def release(self) -> None:
        """Release the pinned payload buffer back to the C layer.

        This invalidates the memoryview. Always call before recv() again,
        or use "with frame:" to release automatically.
        """
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
        """Decode the payload as UTF-8 text.

        Args:
            errors: error handling strategy for decode
        """
        return self.data.tobytes().decode("utf-8", errors)


class WebSocket:
    """Synchronous, zero-copy WebSocket client.

    Use connect() to create a client. Send with send_text/send_binary.
    Receive with recv(), which yields a Frame (memoryview). Release frames quickly.

    Example:
        >>> from wibesocket import WebSocket
        >>> with WebSocket.connect("ws://127.0.0.1:8765") as ws:
        ...     ws.send_text("hello")
        ...     fr = ws.recv(timeout_ms=1000)
        ...     if fr:
        ...         with fr:
        ...             print(fr.text())
    """

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
        """Connect to a WebSocket server.

        Args:
            uri: ws://host:port/path URL (TLS termination must occur upstream)
            handshake_timeout_ms: handshake timeout
            max_frame_size: maximum allowed frame size
            user_agent: optional User-Agent
            origin: optional Origin
            protocol: optional subprotocol
        """
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
        """Send a text message (UTF-8).

        Args:
            data: str (encoded as UTF-8) or bytes
        """
        ok = _c.send_text(self._c, data if isinstance(data, bytes) else data)
        if not ok:
            raise RuntimeError("send_text failed")

    def send_binary(self, data: bytes | memoryview) -> None:
        """Send binary data.

        Args:
            data: bytes-like object
        """
        ok = _c.send_binary(self._c, memoryview(data))
        if not ok:
            raise RuntimeError("send_binary failed")

    # Receiving
    def recv(self, timeout_ms: int = 0) -> Optional[Frame]:
        """Receive the next frame.

        Returns a Frame or None on timeout. Use "with Frame:" or call release().
        """
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
        """Close the connection with a CLOSE frame, then free resources."""
        # Explicit close order to avoid destructor-based closes
        _c.send_close(self._c, code, reason or "")
        _c.close(self._c)

    # Integration
    def fileno(self) -> int:
        return int(_c.fileno(self._c))


class AsyncWebSocket:
    """Asyncio wrapper using add_reader for zero-copy reads.

    Example:
        >>> import asyncio
        >>> from wibesocket import AsyncWebSocket
        >>> async def main():
        ...     aws = await AsyncWebSocket.connect("ws://127.0.0.1:8765")
        ...     aws.send_text("hello")
        ...     fr = await aws.recv(timeout=3)
        ...     with fr:
        ...         print(fr.text())
        ...     aws.close()
    """

    def __init__(self, ws: WebSocket, loop: Optional[asyncio.AbstractEventLoop] = None):
        self._ws = ws
        self._loop = loop or asyncio.get_running_loop()
        self._fd = ws.fileno()
        self._fut: Optional[asyncio.Future] = None

    @classmethod
    async def connect(cls, uri: str, **kwargs) -> "AsyncWebSocket":
        """Async constructor building on the sync connect."""
        return cls(WebSocket.connect(uri, **kwargs))

    def _on_readable(self) -> None:
        if not self._fut or self._fut.done():
            return
        fr = self._ws.recv(timeout_ms=0)
        if fr is not None:
            self._fut.set_result(fr)

    async def recv(self, timeout: float | None = None) -> Frame:
        """Await a frame with an optional timeout (seconds)."""
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


