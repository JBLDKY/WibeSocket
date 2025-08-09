## WebSocket

Synchronous, zero-copy WebSocket client.

Designed for low overhead and predictable ownership:

- Zero-copy receive path via memoryview-backed `Frame`
- Explicit lifetime management using context managers or `Frame.release()`
- No hidden threads; blocking behavior is explicit

### Quickstart

```python
from wibesocket import WebSocket

with WebSocket.connect("ws://127.0.0.1:8765") as ws:
    ws.send_text("hello")
    fr = ws.recv(timeout_ms=1000)
    if fr:
        with fr:
            print(fr.text())
```

### API Highlights

- `WebSocket.connect(uri, *, handshake_timeout_ms=5000, max_frame_size=1048576, user_agent=None, origin=None, protocol=None) -> WebSocket`
- `send_text(data: str | bytes) -> None`
- `send_binary(data: bytes | memoryview) -> None`
- `recv(timeout_ms: int = 0) -> Frame | None`
- `close(code: int = 1000, reason: str | None = None) -> None`
- `fileno() -> int`

### Frames

`Frame` is a zero-copy wrapper around the received payload:

- Use `with Frame:` or call `Frame.release()` before calling `recv()` again
- `Frame.text()` decodes UTF-8 text payloads

### Reference

::: wibesocket.wrappers.WebSocket
