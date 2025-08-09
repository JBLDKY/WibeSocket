import os
import sys
import time
import unittest

from wibesocket import WebSocket


ECHO_URI = os.environ.get("WIBESOCKET_TEST_ECHO_URI", "ws://127.0.0.1:8765")


class TestSyncClient(unittest.TestCase):
    def test_connect_send_recv(self):
        print(f"[sync] connecting to {ECHO_URI}")
        try:
            ws = WebSocket.connect(ECHO_URI, handshake_timeout_ms=4000, max_frame_size=1 << 20)
        except Exception:
            self.skipTest("connect failed (no network or server), skipping")
            return

        payload = f"hello-sync-{int(time.time())}"
        ws.send_text(payload)
        print("[sync] send_text: True")

        deadline = time.time() + 5.0
        received = None
        while time.time() < deadline:
            fr = ws.recv(timeout_ms=500)
            if fr is None:
                continue
            with fr:
                print("[sync] recv:", fr.type, fr.data.tobytes()[:64], "final:", fr.is_final)
                received = fr.text(errors="ignore")
            break

        ws.close()
        self.assertIsNotNone(received, "did not receive echo")
        self.assertIn("hello-sync-", received)


if __name__ == "__main__":
    unittest.main(verbosity=2)


