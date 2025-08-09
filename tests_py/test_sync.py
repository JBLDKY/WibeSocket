import os
import sys
import time
import unittest

import wibesocket


ECHO_URI = os.environ.get("WIBESOCKET_TEST_ECHO_URI", "ws://echo.websocket.events")


class TestSyncClient(unittest.TestCase):
    def test_connect_send_recv(self):
        print(f"[sync] connecting to {ECHO_URI}")
        conn = wibesocket.connect(ECHO_URI, handshake_timeout_ms=4000, max_frame_size=1 << 20)
        if conn is None:
            self.skipTest("connect failed (no network or server), skipping")
            return

        payload = f"hello-sync-{int(time.time())}"
        ok = wibesocket.send_text(conn, payload)
        print("[sync] send_text:", ok)
        self.assertTrue(ok)

        deadline = time.time() + 5.0
        received = None
        while time.time() < deadline:
            res = wibesocket.recv(conn, timeout_ms=500)
            if res is None:
                continue
            ftype, data, is_final = res
            print("[sync] recv:", ftype, data.tobytes()[:64], "final:", is_final)
            received = data.tobytes().decode("utf-8", "ignore")
            break

        wibesocket.close(conn)
        self.assertIsNotNone(received, "did not receive echo")
        self.assertIn("hello-sync-", received)


if __name__ == "__main__":
    unittest.main(verbosity=2)


