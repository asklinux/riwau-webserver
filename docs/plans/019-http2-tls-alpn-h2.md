# Plan 019: HTTP/2 TLS ALPN h2 Request Serving

## Goal

Add basic HTTP/2 request serving over TLS ALPN `h2` by reusing the partial HTTP/2 h2c request-serving path, while keeping HTTP/2 documented as partial and not production-complete.

## Implemented

- Allowed SQLite `tls_alpn_protocols` to include `h2` when HTTP/2 is enabled for TLS serving.
- Kept ALPN `h3` rejected because HTTP/3 live serving is not implemented.
- Built the OpenSSL ALPN wire list from enabled protocol flags so disabled protocols are not advertised.
- Recorded the selected ALPN protocol after a successful `SSL_accept`.
- Routed TLS connections with selected ALPN `h2` into HTTP/2 client-preface detection.
- Required the HTTP/2 client connection preface for TLS `h2`; non-preface bytes receive GOAWAY with protocol error.
- Reused the existing partial HTTP/2 request path: SETTINGS, SETTINGS ACK, HEADERS/DATA decode, shared handler dispatch, and HTTP/2 HEADERS/DATA response serialization.
- Allowed HTTP/2-only TLS startup when `http1_enabled=false`, `http2_enabled=true`, `tls_enabled=true`, and `tls_alpn_protocols` includes `h2`.
- Updated protocol capability/status reporting and project-memory docs.

## Validation

Commands run on 2026-07-20:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
ldd build/rimau-server
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter'
make status
make status-https
git status --short --branch
```

Manual TLS `h2` smoke:

```text
temporary SQLite database:
  tls_enabled=true
  http2_enabled=true
  tls_alpn_protocols=h2,http/1.1

Python stdlib SSL client:
  offers ALPN h2
  verifies selected ALPN is h2
  sends HTTP/2 client preface + SETTINGS + HEADERS GET /

Rimau:
  responds SETTINGS + SETTINGS ACK + response HEADERS + response DATA
```

Manual TLS HTTP/2-only smoke:

```text
temporary SQLite database:
  http1_enabled=false
  tls_enabled=true
  http2_enabled=true
  tls_alpn_protocols=h2

Python stdlib SSL client offers ALPN h2 and sends GET / as HTTP/2 frames
Rimau starts successfully and responds with HTTP/2 HEADERS/DATA
```

Result:

- Build passed; existing static glibc DNS/NSS linker warnings for `getaddrinfo` and OpenSSL `gethostbyname` remain.
- CTest passed, 8/8 tests.
- `make test` passed, 8/8 tests.
- `make check` passed.
- TLS ALPN `h2` smoke passed.
- TLS HTTP/2-only startup and GET smoke passed with `http1_enabled=false`, `http2_enabled=true`, `tls_enabled=true`, and `tls_alpn_protocols=h2`.
- `ldd build/rimau-server` reported `not a dynamic executable`.
- `file build/rimau-server` reported a statically linked Linux x86-64 ELF.
- `readelf` found no dynamic interpreter.
- `make status` and `make status-https` reported no background server running.
- `git status --short --branch` failed because this directory is not a Git repository.

## Known Limits

- HTTP/2 support remains partial, not production-complete.
- HPACK Huffman is not implemented.
- HPACK dynamic table persistence and dynamic table references are not implemented.
- CONTINUATION frame assembly is not implemented.
- Flow control is not implemented beyond accepting valid WINDOW_UPDATE frames.
- Priority behavior is ignored.
- Stream lifecycle and multiplexing behavior are basic and not production-complete.
- Current TLS `h2` smoke is raw-frame based, not a full automated real-client integration test.
- HTTP/3 remains wire-codec-only; UDP/QUIC/QPACK/live request serving and ALPN `h3` are not implemented.
