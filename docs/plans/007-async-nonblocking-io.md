# Plan 007: Asynchronous Non-Blocking I/O

Date: 2026-07-18

## Goal

Ensure Rimau Web Server uses an asynchronous architecture with non-blocking I/O for the main runtime.

## Implemented In This Phase

- Replaced the previous thread-per-accepted-client runtime with Linux `epoll` worker reactors.
- Set each worker TCP listener socket to non-blocking mode.
- Set every accepted client socket to non-blocking mode.
- Added worker thread pool using `std::jthread`.
- `worker_threads=0` auto-detects CPU core count.
- Each worker owns its own `epoll` fd and event loop.
- Each worker owns its own listener socket.
- `SO_REUSEADDR` and `SO_REUSEPORT` are enabled by default for listeners.
- TCP keepalive is configurable and enabled by default for accepted sockets.
- Added a per-client state machine for:
  - TLS handshake
  - request read
  - transaction dispatch
  - response write
  - TLS shutdown
  - connection close
- Integrated OpenSSL non-blocking behavior through `SSL_ERROR_WANT_READ` and `SSL_ERROR_WANT_WRITE`.
- Removed the legacy blocking `Http1Connection` module from the build.
- Kept HTTP request handling on the existing `Transaction` and `RequestHandler` pipeline.
- Added per-worker connection object pooling to reduce repeated allocation.
- Added graceful SIGTERM/SIGINT shutdown.
- Added HTTP/1.1 keep-alive for requests without body framing.
- Added keep-alive idle timeout cleanup in worker event loops.
- Added limited SIGHUP SQLite config live reload for values that did not require listener, worker, pool, or TLS context recreation at this phase. Plan 009 later adds TLS context reload for new connections.

## Current Limits

- Static file reading is still synchronous inside the handler path.
- SIGHUP live reload does not recreate listener sockets, worker threads, connection pools, or TLS context.
- There is no lock-free queue because `SO_REUSEPORT` currently handles connection distribution in the kernel.
- At this phase there was no request timeout or slow-client timeout yet. Plan 009 later adds baseline request/header/body/idle timeout handling.
- No backpressure policy beyond readiness-driven writes.

## Next Steps

1. Add advanced slow-client timeout behavior beyond the baseline timeout handling added in Plan 009.
2. Add integration tests for keep-alive idle timeout and max request cap.
3. Split protocol codec/session logic out of `src/core/server.cpp`.
4. Benchmark the Linux `epoll` backend and decide whether `io_uring` is worth adding. Needs verification.
5. Add benchmark tests for many concurrent idle and active clients.
6. Add zero-copy or bounded-buffer static file response path. Needs verification.
7. Add automated integration tests for SIGHUP reload of multi-certificate SNI contexts.

## Validation

Completed on 2026-07-18:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make start
curl -i --max-time 5 http://127.0.0.1:18080/
make stop
curl -k -I --max-time 5 https://127.0.0.1:18444/
```

Result:

- Build passed.
- CTest passed, 4/4 tests.
- HTTP smoke test returned `HTTP/1.1 200 OK`.
- HTTPS smoke test returned `HTTP/1.1 200 OK`.
- Multi-worker `worker_threads=2` smoke tests passed for HTTP and HTTPS.
- SIGTERM graceful shutdown and SIGHUP handling were smoke-tested.
