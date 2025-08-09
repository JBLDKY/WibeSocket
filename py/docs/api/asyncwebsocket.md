## AsyncWebSocket

Asyncio wrapper around the synchronous `WebSocket`, integrating with the event loop via `add_reader`.

- No background threads
- Uses file descriptor readiness for non-blocking receives

### Quickstart

```python
import asyncio
from wibesocket import AsyncWebSocket

async def main():
    ws = await AsyncWebSocket.connect("ws://127.0.0.1:8765")
    ws.send_text("hello")
    fr = await ws.recv(timeout=3)
    with fr:
        print(fr.text())
    ws.close()

asyncio.run(main())
```

### API Highlights

- `AsyncWebSocket.connect(uri: str, **kwargs) -> AsyncWebSocket`
- `recv(timeout: float | None = None) -> Frame`
- `send_text(data: str | bytes) -> None`
- `send_binary(data: bytes | memoryview) -> None`
- `close(code: int = 1000, reason: str | None = None) -> None`

### Reference

::: wibesocket.wrappers.AsyncWebSocket
