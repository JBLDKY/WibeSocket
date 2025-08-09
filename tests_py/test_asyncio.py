import os
import asyncio
import time
import unittest

import wibesocket

ECHO_URI = os.environ.get("WIBESOCKET_TEST_ECHO_URI", "ws://echo.websocket.events")


class TestAsyncioClient(unittest.IsolatedAsyncioTestCase):
    async def test_asyncio_connect_send_recv(self):
        print(f"[asyncio] connecting to {ECHO_URI}")
        conn = wibesocket.connect(ECHO_URI, handshake_timeout_ms=4000, max_frame_size=1 << 20)
        if conn is None:
            self.skipTest("connect failed (no network or server), skipping")
            return

        loop = asyncio.get_running_loop()
        fd = wibesocket.fileno(conn)
        fut = loop.create_future()

        def on_readable():
            res = wibesocket.recv(conn, timeout_ms=0)
            if res is None:
                return
            if not fut.done():
                fut.set_result(res)

        loop.add_reader(fd, on_readable)
        try:
            payload = f"hello-async-{int(time.time())}"
            ok = wibesocket.send_text(conn, payload)
            print("[asyncio] send_text:", ok)
            self.assertTrue(ok)

            res = await asyncio.wait_for(fut, timeout=5.0)
            ftype, data, is_final = res
            print("[asyncio] recv:", ftype, data.tobytes()[:64], "final:", is_final)
            self.assertIn(b"hello-async-", data.tobytes())
        finally:
            loop.remove_reader(fd)
            wibesocket.close(conn)


if __name__ == "__main__":
    asyncio.run(unittest.main())


