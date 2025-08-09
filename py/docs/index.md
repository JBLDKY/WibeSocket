# WibeSocket Python API

Zero-copy, high-performance Python bindings for the WibeSocket C core.

## Quick start

```python
from wibesocket import WebSocket

with WebSocket.connect("ws://echo.websocket.events") as ws:
    ws.send_text("hello")
    fr = ws.recv(timeout_ms=1000)
    if fr:
        with fr:
            print(fr.text())
```

See the API pages for details.
