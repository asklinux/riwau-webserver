# Plan 006: HTTP/2 And HTTP/3 Support System

Note: Plan 017 later adds partial HTTP/2 and HTTP/3 wire codecs, and Plan 018 later adds partial cleartext h2c HTTP/2 request serving. This plan remains historical for the earlier config/status gateway work.

Date: 2026-07-18

## Goal

Add the project structure needed to manage HTTP/2 and HTTP/3 support without falsely advertising protocol support before real codecs and transports exist.

## Implemented In This Phase

- Added SQLite config keys:
  - `http1_enabled`
  - `http2_enabled`
  - `http3_enabled`
- Added config parsing and validation for these booleans.
- Updated `--protocols` so it reads SQLite config before printing status.
- Protocol capability reporting now includes:
  - implementation status
  - configured state
  - expected target transport
  - limitation notes
- Added explicit HTTP/2 and HTTP/3 gateway status helpers.
- Added startup guard so the server refuses to run if HTTP/1.1 is disabled while HTTP/2 and HTTP/3 are still only planned.
- Added protocol capability unit test.

## Not Implemented Yet

- HTTP/2 frame parser/writer.
- HTTP/2 session and stream lifecycle.
- HPACK integration.
- ALPN `h2` advertisement.
- HTTP/3 UDP listener.
- QUIC transport.
- HTTP/3 frame layer.
- QPACK integration.
- ALPN `h3` advertisement.

## Important Safety Rule

Do not advertise ALPN `h2` or `h3` until Rimau can actually serve those protocols with real clients. At this historical phase, config flags were control-plane gates only; later work changes `http2_enabled` to enable partial cleartext h2c request serving.

## Next Implementation Steps

1. Verify and choose HTTP/2 dependency. Candidate: `nghttp2`. Needs verification.
2. Add bundled build path for the chosen HTTP/2 dependency if accepted.
3. Add an HTTP/2 session wrapper that maps streams into `rimau::http::Transaction`.
4. Add ALPN `h2` only after the HTTP/2 session can serve `GET /`.
5. Verify and choose HTTP/3 dependency. Candidates: `ngtcp2` + `nghttp3` or `quiche`. Needs verification.
6. Add UDP listener and QUIC lifecycle.
7. Map HTTP/3 requests into the shared handler pipeline.
8. Add integration tests with real HTTP/2 and HTTP/3 clients.

## Validation

Completed on 2026-07-18:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --check-config
make test
```

Result:

- Build passed.
- CTest passed, 4/4 tests.
- Protocol status reports HTTP/2 and HTTP/3 configured state separately from implementation state.
- Startup guard was smoke-tested with a temporary SQLite database.
