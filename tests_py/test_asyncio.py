import os
import asyncio
import time
import unittest

from wibesocket import WebSocket
from wibesocket import AsyncWebSocket

ECHO_URI = os.environ.get("WIBESOCKET_TEST_ECHO_URI", "ws://127.0.0.1:8765")


class TestAsyncioClient(unittest.IsolatedAsyncioTestCase):
    async def test_asyncio_connect_send_recv(self):
        print(f"[asyncio] connecting to {ECHO_URI}")
        try:
            ws = WebSocket.connect(ECHO_URI, handshake_timeout_ms=4000, max_frame_size=1 << 20)
        except Exception:
            self.skipTest("connect failed (no network or server), skipping")
            return
        aws = AsyncWebSocket(ws)
        payload = f"hello-async-{int(time.time())}"
        aws.send_text(payload)
        fr = await aws.recv(timeout=5.0)
        with fr:
            print("[asyncio] recv:", fr.type, fr.data.tobytes()[:64], "final:", fr.is_final)
            self.assertIn("hello-async-", fr.text(errors="ignore"))
        aws.close()


if __name__ == "__main__":
    asyncio.run(unittest.main())


