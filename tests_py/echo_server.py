import asyncio
import signal
import sys

try:
    import websockets
except ImportError:
    print("websockets package not installed", file=sys.stderr)
    sys.exit(2)

HOST = "127.0.0.1"
PORT = 8765


async def handler(ws):
    try:
        async for msg in ws:
            await ws.send(msg)
    except websockets.ConnectionClosed:
        pass


async def main():
    stop = asyncio.Event()

    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)

    async with websockets.serve(handler, HOST, PORT):
        print(f"echo server on ws://{HOST}:{PORT}")
        await stop.wait()


if __name__ == "__main__":
    asyncio.run(main())


