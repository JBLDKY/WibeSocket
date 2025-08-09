import asyncio
import wibesocket

async def main():
    uri = "ws://echo.websocket.events"
    conn = wibesocket.connect(uri, handshake_timeout_ms=5000, max_frame_size=1<<20)
    if conn is None:
        raise RuntimeError("connect failed")

    loop = asyncio.get_running_loop()
    fd = wibesocket.fileno(conn)
    recv_fut = loop.create_future()

    def on_readable():
        # Try to receive without blocking (short timeout)
        res = wibesocket.recv(conn, timeout_ms=0)
        if res is None:
            return
        ftype, data, is_final = res
        if not recv_fut.done():
            recv_fut.set_result((ftype, data, is_final))

    loop.add_reader(fd, on_readable)

    assert wibesocket.send_text(conn, "hello from asyncio")
    try:
        ftype, data, is_final = await asyncio.wait_for(recv_fut, timeout=3.0)
        print("recv:", ftype, data.tobytes().decode('utf-8', 'ignore'), is_final)
    finally:
        loop.remove_reader(fd)
        wibesocket.close(conn)

if __name__ == "__main__":
    asyncio.run(main())


