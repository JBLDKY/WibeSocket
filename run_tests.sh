#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
VENV_DIR="$ROOT_DIR/.venv"

echo "[1/4] Build C library"
cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT_DIR/build" -j

echo "[2/4] Ensure venv at $VENV_DIR"
python3 -m venv "$VENV_DIR"
source "$VENV_DIR/bin/activate"
python -m pip install -U pip setuptools wheel >/dev/null

echo "[3/4] Install Python extension (editable)"
python -m pip install -e "$ROOT_DIR/py" >/dev/null

echo "[4/4] Run Python tests (with local echo server)"
# Ensure runtime linker finds libwibesocket.so
export LD_LIBRARY_PATH="$ROOT_DIR/build:${LD_LIBRARY_PATH:-}"
# Start local echo server in background
python -m pip install -q websockets >/dev/null
python tests_py/echo_server.py &
ES_PID=$!
sleep 0.5
# Ensure env points to local server
export WIBESOCKET_TEST_ECHO_URI="ws://127.0.0.1:8765"
python -m unittest -v tests_py/test_sync.py || true
python -m unittest -v tests_py/test_asyncio.py || true
# Teardown
kill $ES_PID >/dev/null 2>&1 || true

echo "Done. Note: tests require a reachable echo server. Set WIBESOCKET_TEST_ECHO_URI to override."


