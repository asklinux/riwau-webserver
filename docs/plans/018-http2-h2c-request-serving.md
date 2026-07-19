# Plan 018: HTTP/2 h2c Request Serving

## Goal

Move HTTP/2 beyond handshake-only behavior by serving basic cleartext h2c requests through the existing Rimau handler pipeline, while still avoiding claims of full HTTP/2 production support.

## Implemented

- Changed cleartext HTTP/2 prior-knowledge handling from SETTINGS/ACK/GOAWAY to SETTINGS/ACK plus persistent h2c frame processing.
- Added per-connection HTTP/2 stream state inside `ClientConnection`.
- Added HTTP/2 handling for SETTINGS, SETTINGS ACK, PING, RST_STREAM, WINDOW_UPDATE, HEADERS, and DATA basics.
- Added HPACK decode support for literal fields with incremental indexing, without dynamic-table persistence.
- Added translation from HTTP/2 pseudo-headers and regular headers into `rimau::http::Request`.
- Reused the existing `Transaction` and `VirtualHostHandlerFactory` pipeline for h2c requests.
- Added HTTP/2 response serialization as HEADERS and DATA frames.
- Added h2c-only startup allowance when `http1_enabled=false`, `http2_enabled=true`, and `tls_enabled=false`.
- Updated protocol status text so HTTP/2 reports partial h2c request serving instead of handshake-only behavior.

## Validation

Commands run on 2026-07-19:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
```

Manual h2c smoke tests:

```text
client sends HTTP/2 connection preface + SETTINGS + HEADERS GET /
server responds SETTINGS + SETTINGS ACK + HEADERS + DATA

client sends HTTP/2 connection preface + SETTINGS + HEADERS GET / + HEADERS/DATA POST /submit
server responds with HEADERS/DATA for stream 1 and stream 3

with http1_enabled=false, http2_enabled=true, and tls_enabled=false
client sends HTTP/2 connection preface + SETTINGS + HEADERS GET /
server starts successfully and responds with HEADERS/DATA
```

Result:

- Build passed.
- CTest passed, 8/8 tests.
- `make test` passed, 8/8 tests.
- `make check` passed.
- `ldd build/rimau-server` reported `not a dynamic executable`.
- `file build/rimau-server` reported a statically linked Linux x86-64 ELF.
- `readelf` found no dynamic interpreter.
- Basic h2c GET smoke passed.
- Basic h2c GET plus POST DATA smoke passed.
- h2c-only startup smoke passed with `http1_enabled=false`, `http2_enabled=true`, and `tls_enabled=false`.

## Known Limits

- This plan covered cleartext h2c only. `docs/plans/019-http2-tls-alpn-h2.md` later adds partial TLS ALPN `h2` request serving using the same HTTP/2 request path.
- HPACK Huffman is not implemented.
- HPACK dynamic table persistence and dynamic table references are not implemented.
- CONTINUATION frame assembly is not implemented.
- Flow control is not implemented beyond accepting valid WINDOW_UPDATE frames.
- Priority behavior is ignored.
- Stream lifecycle and multiplexing behavior are basic and not production-complete.
- No automated real-client HTTP/2 integration test exists yet.
- HTTP/3 remains wire-codec-only; UDP/QUIC/QPACK/live request serving is not implemented.
