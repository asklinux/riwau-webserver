# Plan 012: Reverse Proxy Upstream Pools, HTTPS, And Retry

Date: 2026-07-19

Status: Implemented for HTTPS upstream transport, optional default-trust-path certificate verification, multiple upstream parsing, basic round-robin, and retry/failover. WebSocket proxying was added later in Plan 013, and passive circuit breaker was added in Plan 014. Per-upstream certificate policy, active health checks, streaming, and full reactor-integrated normal HTTP upstream I/O remain planned.

## Goal

Continue the reverse proxy work from Plan 011 by adding practical upstream behavior while keeping configuration in SQLite and preserving bundled OpenSSL as the TLS provider.

## Scope For This Phase

- Implemented: Support `https://` reverse proxy upstream URLs using bundled OpenSSL.
- Implemented: Support multiple upstream targets per proxy virtual host.
- Implemented: Add basic round-robin upstream selection.
- Implemented: Add basic retry/failover across upstream targets.
- Implemented: Add SQLite key `reverse_proxy_retry_count`.
- Implemented: Add SQLite key `reverse_proxy_tls_verify_upstream`.
- Implemented: Keep response handling buffered and enforce `reverse_proxy_max_response_bytes`.

## Config Format

Multiple upstreams use comma-separated URLs inside one `proxy:` rule:

```text
api.test=proxy:http://127.0.0.1:9001,http://127.0.0.1:9002
secure-api.test=proxy:https://127.0.0.1:9443
```

## Deliberate Limits

- Upstream sockets still use non-blocking sockets but wait inside the handler with `poll()`.
- DNS resolution still uses `getaddrinfo`.
- HTTPS upstream certificate verification can be enabled through default trust paths, but per-upstream CA/pinning/verify-depth policy is not production-ready in this phase. Needs verification.
- WebSocket proxying was not part of this phase; Plan 013 later added a WebSocket tunnel path before the local echo handler.
- No health-check scheduler or upstream connection pooling yet. Passive circuit breaker was not part of this phase and was added later in Plan 014.
- No streaming request/response body path yet.

## Implementation Result

- Updated `include/rimau/http/virtual_host.hpp` to store multiple upstream targets and retry settings.
- Updated `src/http/virtual_host.cpp` with upstream TLS client transport, multi-upstream parsing, round-robin selection, and retry/failover.
- Updated `include/rimau/core/config.hpp` and `src/core/config.cpp` with `reverse_proxy_retry_count` and `reverse_proxy_tls_verify_upstream`.
- Updated `src/core/server.cpp` to pass retry settings into `ReverseProxySettings`.
- Updated tests for multi-upstream and HTTPS upstream parsing.

## Validation

Latest validation is recorded in `docs/PROGRESS.md`.
