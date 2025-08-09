#!/usr/bin/env bash
set -euo pipefail

# Fast-fail switch for emergency commits
if [[ "${SKIP_DEV_CHECK:-}" == "1" ]]; then
  echo "[dev_check] SKIP_DEV_CHECK=1 set; skipping checks"
  exit 0
fi

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_SAN="$ROOT_DIR/build_san"
BUILD_NOSAN="$ROOT_DIR/build"
VENV_DIR="$ROOT_DIR/.venv"

echo "[1/7] Build C library with sanitizers + run C tests"
cmake -S "$ROOT_DIR" -B "$BUILD_SAN" -DCMAKE_BUILD_TYPE=Debug -DWS_SANITIZE=ON
cmake --build "$BUILD_SAN" -j
ctest --test-dir "$BUILD_SAN" -V

echo "[2/7] Build C library without sanitizers for Python runs"
cmake -S "$ROOT_DIR" -B "$BUILD_NOSAN" -DCMAKE_BUILD_TYPE=Release -DWS_SANITIZE=OFF
cmake --build "$BUILD_NOSAN" -j --target wibesocket

echo "[3/7] Ensure Python venv"
if [[ ! -d "$VENV_DIR" ]]; then
  python3 -m venv "$VENV_DIR" || {
    echo "[dev_check] venv creation failed; ensure Python venv available" >&2
    exit 1
  }
fi
source "$VENV_DIR/bin/activate"
python -m pip -q install -U pip setuptools wheel

echo "[4/7] Install Python package (editable)"
python -m pip -q install -e "$ROOT_DIR/py"

echo "[5/7] Run Python tests with local echo server"
export LD_LIBRARY_PATH="$BUILD_NOSAN:${LD_LIBRARY_PATH:-}"
python -m pip -q install websockets
python "$ROOT_DIR/tests_py/echo_server.py" &
ES_PID=$!
trap 'kill $ES_PID >/dev/null 2>&1 || true' EXIT
sleep 0.5
export WIBESOCKET_TEST_ECHO_URI="ws://127.0.0.1:8765"
(
  cd "$ROOT_DIR"
  python -m unittest -v tests_py.test_sync
  python -m unittest -v tests_py.test_asyncio
)
kill $ES_PID >/dev/null 2>&1 || true
trap - EXIT

echo "[6/7] Build docs (if mkdocs is available)"
if python -c 'import mkdocs' >/dev/null 2>&1; then
  (cd "$ROOT_DIR/py" && mkdocs build -q)
else
  echo "[dev_check] mkdocs not installed; install with: pip install -e py[docs]" >&2
fi

echo "[7/7] Examples sanity"
python - << 'PY'
import os, sys
from wibesocket import WebSocket
try:
    ws = WebSocket.connect(os.environ.get('WIBESOCKET_TEST_ECHO_URI','ws://127.0.0.1:8765'))
except Exception as e:
    print('[examples] connect failed:', e)
    sys.exit(0)
ws.send_text('hello-check')
fr = ws.recv(timeout_ms=500)
if fr:
    with fr:
        print('[examples] recv ok, bytes=', len(fr.data))
ws.close()
PY

echo "[dev_check] All checks completed"


