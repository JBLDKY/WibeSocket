# WibeSocket Python API

Zero-copy, high-performance Python bindings for the WibeSocket C core.

## Installation

Build C library and install editable Python package:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
python -m venv .venv && . .venv/bin/activate
pip install -e py
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
```

## Quick start (sync)

```python
from wibesocket import WebSocket

with WebSocket.connect("ws://echo.websocket.events") as ws:
    ws.send_text("hello")
    fr = ws.recv(timeout_ms=1000)
    if fr:
        with fr:  # release zero-copy buffer automatically
            print(fr.text())
```

## Quick start (asyncio)

```python
import asyncio
from wibesocket import AsyncWebSocket

async def main():
    ws = await AsyncWebSocket.connect("ws://echo.websocket.events")
    ws.send_text("hello")
    fr = await ws.recv(timeout=3)
    with fr:
        print(fr.text())
    ws.close()

asyncio.run(main())
```

See the API pages for details.
