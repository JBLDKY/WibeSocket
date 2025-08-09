"""Top-level Python API for WibeSocket.

This module tries to import the native extension ``_core``. If it is not
available (e.g., during documentation builds), the import is gracefully
skipped and Python stubs are provided so that imports succeed.
"""

from .wrappers import WebSocket, AsyncWebSocket, Frame, FrameType

try:
    from ._core import (
        connect,
        send_text,
        send_binary,
        recv,
        release_payload,
        fileno,
        send_close,
        close,
    )
except Exception as _e:  # pragma: no cover - used only during docs/import fallback
    def _stub(*_args, **_kwargs):  # type: ignore
        raise ImportError(
            "wibesocket._core is not available in this environment. "
            "Ensure the native library is built and importable."
        )

    connect = _stub
    send_text = _stub
    send_binary = _stub
    recv = _stub
    release_payload = _stub
    fileno = _stub
    send_close = _stub
    close = _stub

__all__ = [
    "connect",
    "send_text",
    "send_binary",
    "recv",
    "release_payload",
    "fileno",
    "send_close",
    "close",
    "WebSocket",
    "AsyncWebSocket",
    "Frame",
    "FrameType",
]


