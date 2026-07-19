# Plan 009: Security And TLS Hardening

Date: 2026-07-18

## Goal

Harden the current HTTP/1.1 and HTTPS scaffold before deeper HTTP/2 and HTTP/3 work.

## Implemented Scope

- OpenSSL TLS context now enforces SQLite-configured TLS minimum and maximum versions.
- Defaults allow TLS 1.2 through TLS 1.3.
- TLS 1.2 cipher list and TLS 1.3 ciphersuites are configurable through SQLite.
- ALPN callback advertises only implemented protocols; current default is `http/1.1`.
- SNI callback validates optional configured hostnames and can require SNI.
- TLS certificate, key, TLS version, cipher, ALPN, and SNI settings can be reloaded for new connections on SIGHUP when `tls_enabled` itself does not change.
- Response serialization supports default security headers and can disable the `Server` header.
- Response header values are sanitized to prevent response splitting.
- HTTP header framing rejects bare CR/LF, obs-fold, duplicate `Content-Length`, invalid `Content-Length`, duplicate `Transfer-Encoding`, unsupported `Transfer-Encoding`, and `Content-Length` plus `Transfer-Encoding` conflicts.
- Request/header/body/idle timeout config keys were added.
- Global and per-IP connection limits were added.
- Fixed-window per-IP request rate limiting was added.
- IPv4 exact/CIDR allowlist and blocklist controls were added in this phase. Plan 010 later extends this to IPv6.
- WebSocket max frame size uses dedicated config key `websocket_max_frame_bytes`.
- Static file directory-index resolution recanonicalizes after appending `index.html` to reduce symlink escape risk.

## Deliberate Limits

- Multi-certificate SNI selection was not implemented in this phase. Plan 010 later adds it.
- ALPN currently advertises `http/1.1` only. ALPN `h2` and `h3` remain disabled until wire-level HTTP/2/HTTP/3 support exists.
- Rate limiting is in-memory and per process. It is not shared across multiple Rimau processes or machines.
- Rate limiting uses a fixed one-minute window, not a token bucket or leaky bucket.
- IP allowlist/blocklist support was IPv4 exact match or CIDR only in this phase. Plan 010 later adds IPv6 exact/CIDR support.
- Timeouts close or return `408` for basic slow-client cases; there is no advanced adaptive slow-client scoring yet.
- Security headers were a fixed default set controlled by one SQLite boolean in this phase. Plan 010 later adds SQLite-configurable values for that fixed header set.
- Certificate reload affects new TLS connections. Existing TLS sessions continue using the context they were created with.

## Validation

Commands used:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
```

Runtime smoke tests with temporary SQLite databases covered:

- Security headers and disabled `Server` header.
- Duplicate `Content-Length`.
- `Content-Length` and `Transfer-Encoding` conflict.
- Invalid `Content-Length`.
- Oversized headers.
- Per-IP rate limiting.
- Header timeout slow-client behavior.
- WebSocket oversized frame close.
- HTTPS with TLS 1.2.
- HTTPS with TLS 1.3.
- ALPN negotiation for `http/1.1`.
- SNI required with accepted hostname.
- SIGHUP reload of SNI settings for new TLS connections.
