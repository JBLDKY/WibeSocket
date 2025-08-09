### WibeSocket Roadmap (Minimal, Correct, Performant C Core)

#### Overview
This document tracks the current state and the prioritized plan to evolve WibeSocket into a minimal but correct, high‑performance WebSocket client core in C, conforming to RFC 6455 and the project rules.

#### Current State Summary
- Build: `libwibesocket.so` builds from `src/wibesocket_stub.c`, `src/parser.c`, `src/event_loop.c`.
- Public API: Defined in `include/wibesocket/wibesocket.h` (states, frame types, config, errors, send/recv/close).
- Implementation: `src/wibesocket_stub.c` only; `src/parser.c` and `src/event_loop.c` are empty.
- Prototype (non‑conforming): `src/wibesocket.c` (“ultraws”) uses blocking sockets and OpenSSL; not built and inconsistent with API.
- Tests: `tests/test_wibesocket.c` passes (API smoke). `tests/test_handshake.c` and `tests/test_parser.c` are empty.
- Examples: `examples/simple_echo.c` compiles against the stub.
- Python: `py/module.c` references `ultraws` (non‑existent header), not integrated.
- Benchmarks/CI: `bench/*` and `ci/ci.yml` are empty.

#### Protocol Coverage
- Implemented: API scaffolding only; no real handshake, framing, or event loop.
- Missing: RFC 6455 client handshake, frame build/parse (masking, lengths 126/127), fragmentation/continuations, control frames (ping/pong/close) rules, UTF‑8 validation, close handshake, non‑blocking I/O (epoll/kqueue), zero‑copy receive, error mapping.

#### Risks / Divergence
- The `src/wibesocket.c` “ultraws” prototype and `py/module.c` are inconsistent with project rules and the public API; keep them quarantined to avoid confusion.

#### Prioritized Plan
1) Repo hygiene and scaffolding
   - Move `src/wibesocket.c` and `py/module.c` to an `attic/` folder (or delete). Keep public API stable.
   - Add internal headers in `src/internal/` for buffers, handshake, frames, and backend abstraction.
   - Enable `-Wall -Wextra -Werror`; add ASan/UBSan config for CI.

2) Handshake (client, non‑blocking)
   - Implement in `src/handshake.c`: Sec‑WebSocket‑Key (16 random bytes), HTTP/1.1 Upgrade request, response validation for 101 and `Sec-WebSocket-Accept`.
   - Provide minimal in‑tree `sha1.c` and `base64.c` (no OpenSSL).
   - Non‑blocking connect + handshake state machine with timeout from `wibesocket_config_t`.
   - Tests in `tests/test_handshake.c` with canned positive/negative responses; fuzz header parsing.

3) Frames: builder and incremental parser
   - Builder: text/binary/ping/pong/close frames; masking for client; extended lengths (126/127). Use `iovec` for `writev`.
   - Parser: incremental header/payload parsing, control‑frame rules, fragmentation, RSV checks, `max_frame_size`, UTF‑8 for text/close reason.
   - Zero‑copy: expose payload as pointer into a receive ring buffer with `is_final`.
   - Tests in `tests/test_parser.c`: exhaustive cases incl. fragmentation and protocol errors; fuzz the parser.

4) Evented I/O backend and engine
   - Backend: `epoll` (Linux) and `kqueue` (macOS) under `include/wibesocket/event_loop.h`.
   - Engine: non‑blocking connect/handshake, `readv` into ring buffer, parse frames, queue messages; `writev` batching; backpressure handling.
   - API: `wibesocket_recv(conn, msg, timeout_ms)` returns next message or timeout without spinning; `wibesocket_send_*` enqueue + flush.
   - Integration test: echo round‑trip via `WIBESOCKET_TEST_ECHO_URI` and/or local echo helper.

5) Close handshake and shutdown
   - Client‑initiated and peer‑initiated close per RFC 6455; map errors to close codes.
   - Tests for both sequences, including timeouts and TCP shutdown.

6) Python bindings (sync + asyncio, zero‑copy)
   - New `py/wibesocket_module.c` wrapping `wibesocket_*`: sync connect/send/recv/close, fd accessors for asyncio, `memoryview` on recv without copy.
   - Packaging: simple wheel build; Python tests for sync and asyncio paths.

7) Benchmarks and CI gates
   - Implement `bench/bench_throughput.c` and `bench/bench_latency.c` driving an echo server; record p50/p99 and throughput.
   - CI (`ci/ci.yml`): ASan/UBSan builds, unit + fuzz smoke, benches with regression gate (<5% slowdown fails).

#### Concrete Tasks (short checklist)
- Create `src/handshake.c`, `src/internal/{base64.h,base64.c,sha1.h,sha1.c}` and wire into CMake.
- Implement non‑blocking connect/handshake with timeout and tests/fuzz.
- Implement frame builder/parser with ring buffers and tests/fuzz.
- Implement `epoll`/`kqueue` backends and integrate with API.
- Implement close handshake + UTF‑8 validation + error mapping.
- Build new Python bindings aligned to `wibesocket_*` with zero‑copy.
- Write benches and wire CI (sanitizers + perf guardrails).

#### Performance & Compliance Notes
- Use `epoll`/`kqueue`; no `select`/`poll`.
- Prefer `readv`/`writev`; preallocate ring/slab buffers; avoid heap churn and dynamic formatting in hot paths.
- No TLS in core; no third‑party WebSocket libs.
- Zero‑copy for payloads: expose stable buffer slices; ensure safe lifetime.


