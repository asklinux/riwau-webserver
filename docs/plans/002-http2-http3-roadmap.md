# Plan 002: HTTP/2 And HTTP/3 Roadmap

Date: 2026-07-18

## Goal

Evolve Rimau Web Server from HTTP/1.1 scaffold into a server that supports HTTP/1.1, HTTP/2, and HTTP/3.

## Current State

Implemented:

- HTTP/1.1 static GET/HEAD scaffold.
- Protocol capability reporting.
- SQLite protocol flags `http1_enabled`, `http2_enabled`, and `http3_enabled`.
- Config-aware `--protocols` output.
- Startup guard that refuses to run when no implemented protocol is enabled.

Not implemented:

- HTTP/2 frame/session handling.
- HTTP/3 over QUIC.
- ALPN.

## HTTP/2 Plan

1. Choose protocol library. Candidate: `nghttp2`. Needs verification.
2. Reuse bundled OpenSSL TLS provider and add ALPN.
3. Add HTTP/2 session wrapper in `src/protocol/`.
4. Map HTTP/2 stream requests into shared routing/static-file pipeline.
5. Add flow control and timeout policy.
6. Add integration tests with real clients.
7. Update `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/PROGRESS.md`, and `docs/TODO.md`.

## HTTP/3 Plan

1. Choose QUIC/HTTP/3 library. Candidates: `ngtcp2` + `nghttp3` or `quiche`. Needs verification.
2. Add UDP listener path separate from TCP listener.
3. Add TLS 1.3 and ALPN for QUIC.
4. Add HTTP/3 request adapter into shared routing/static-file pipeline.
5. Add QPACK integration through selected library.
6. Add integration tests with real HTTP/3 client.
7. Add benchmark and packet-loss tests. Needs verification.
8. Update docs after each implementation phase.

## Acceptance Criteria

HTTP/2 can be marked `Implemented` only when:

- A real HTTP/2 client can negotiate protocol and fetch `public/index.html`.
- Tests cover normal request, 404, method rejection, and concurrent streams.
- Documentation records build flags, dependencies, and limitations.

HTTP/3 can be marked `Implemented` only when:

- A real HTTP/3 client can negotiate QUIC/TLS and fetch `public/index.html`.
- Tests cover normal request, 404, method rejection, and concurrent streams.
- Documentation records build flags, dependencies, UDP behavior, and limitations.

Until then, HTTP/2 and HTTP/3 remain `Planned`.

Current config/status gates are not enough to mark HTTP/2 or HTTP/3 as implemented.
