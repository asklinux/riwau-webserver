# TODO

## P0 - Foundation

- Use `docs/plans/021-ordered-update-checklist.md` as the one-by-one implementation checklist and mark items only after code, tests, docs, and validation are complete.
- Add parser tests for malformed request lines, duplicate headers, long headers, and path traversal cases.
- Add response tests for GET, HEAD, 404, 403, 405, and MIME types.
- Add multi-step SQLite migration framework, downgrade policy, and backup/restore workflow beyond current schema version table.
- Decide coding style and formatter. Needs verification.
- Get project-owner confirmation that the existing GitHub repository `LICENSE` GPL-3.0 is the intended final license for Rimau Web Server. Needs verification.

## P1 - HTTP/1.1 Completion

- Add transaction IDs that are unique per connection/session.
- Add remaining integration tests for virtual host routing, fallback host behavior, reverse proxy success/failure, and WebSocket proxy HTTP/HTTPS upstream behavior. Script-placeholder 501/no-shell-out behavior is covered in `rimau_http1_network`.
- Add live in-flight request body streaming before handler dispatch and explicit network-level backpressure contract; current HTTP/1.1 code provides file-backed body spooling and handler pull reads through `RequestBodyReader`.
- Expose chunked trailers to handlers or explicitly discard them through a documented policy.
- Add producer-side async response streaming/backpressure beyond current basic chunked response serialization.
- Revisit Brotli only if a bundled dependency and deployment approach are accepted; P1 decision is to defer Brotli.
- Add WebSocket application routing, fragmentation support, subprotocol negotiation, extension policy, frame-aware proxy limits, and backpressure policy.
- Add structured JSON parser/serializer integration if application routing needs JSON DOM access. Needs verification.
- Add adaptive slow-client protection beyond fixed timeout. Needs verification.
- Add graceful shutdown integration tests for active/idle connections.
- Add access log format.

## P1 - Configuration

- Add integration tests for SIGHUP reload accepted and rejected key changes.
- Add integration tests for SIGHUP TLS certificate/key/context reload.
- Add backup/restore guidance for config database.
- Add production ownership and permission rules for `data/rimau.sqlite3`.
- Add validation for document root existence at config-check time. Needs verification.

## P1 - Performance Architecture

- Add Proxygen-style worker lifecycle hooks around `RequestHandlerFactory::on_server_start()` and `on_server_stop()`.
- Benchmark current Linux `epoll` worker reactor.
- Decide whether `io_uring` is worth adding after benchmarks. Needs verification.
- Add metrics for worker load, accepted connections, active connections, and connection-pool reuse.
- Add lock-free queue only if an internal load-balancer path replaces kernel `SO_REUSEPORT`. Needs verification.
- Add zero-copy static file send path such as `sendfile`. Needs verification.
- Add buffer pool and memory limits.
- Add benchmark harness.

## P1 - TLS And Protocol Negotiation

- Add automated test that ALPN advertises only implemented or explicitly partial-tested protocols and still rejects `h3`.
- Add tests for TLS config failure modes.
- Add automated check that the link command uses the bundled glibc sysroot when `RIMAU_USE_BUNDLED_GLIBC=ON`.
- Add documented bundled dependency update script to refresh OpenSSL, SQLite, zlib, Bison, Linux headers, and glibc versions and SHA256 pins.
- Add integration tests for reverse proxy hostname resolution with static glibc DNS/NSS behavior. Needs verification.

## P1 - Security Hardening

- Revisit full `libmodsecurity` plus full OWASP Core Rule Set after P1 only with pinned source/version, license notes, build plan, update process, test corpus, and rule exclusion policy. Needs verification.
- Add dedicated persistent WAF audit sink if stderr/journald/container logging is not enough. Needs verification.
- Add ModSecurity rule syntax/parser support only if future full compatibility is accepted. Needs verification.
- Add CTest or integration harness for IPv6 listener and IPv6 allowlist/blocklist behavior.
- Add support for arbitrary custom security header names if the fixed built-in set is not enough. Needs verification.
- Add token-bucket or leaky-bucket rate limiting if fixed-window behavior is too bursty. Needs verification.
- Add shared/distributed rate limiting strategy for multi-process or multi-node deployments. Needs verification.
- Add configurable deny response behavior for IP blocklist and connection-limit rejection.
- Add hostname resolution or explicit dual IPv4/IPv6 listener configuration. Needs verification.

## P2 - HTTP/2

- Phase 4 native HTTP/2 production path is implemented per ADR-0040: live `ClientConnection` delegates to `rimau::protocol::http2::ServerSession`, HPACK Huffman/dynamic-table decode exists, CONTINUATION assembly exists, stream lifecycle basics and inbound flow-control accounting exist, and real-client h2c/TLS `h2` curl tests require successful responses.
- Add HTTP/2 multiplexing stress tests for many concurrent streams and interleaved DATA/HEADERS. Needs verification.
- Add outbound HTTP/2 response flow-control scheduler for large responses beyond current buffered serialization. Needs verification.
- Add producer-side async response streaming/backpressure for HTTP/2 responses.
- Decide whether deprecated HTTP/2 priority semantics need compatibility behavior beyond safe ignore. Needs verification.

## P2 - HTTP/3

- Choose QUIC/HTTP/3 library or native implementation path. Candidates: `ngtcp2` + `nghttp3` or `quiche`. Needs verification.
- Replace current `http3_enabled` status gate with real UDP/QUIC listener behavior.
- Add UDP listener.
- Add QUIC connection lifecycle.
- Add HTTP/3 request/response adapter.
- Add QPACK support through chosen library or native implementation.
- Add integration tests using a real HTTP/3 client.

## P2 - Server Features

- Add request handler filter chain.
- Add access log filter.
- Add tracing/stats filter.
- Add rewrite rules.
- Add advanced virtual host routing such as per-host TLS policy, per-host security headers, and per-host access logs. Needs verification.
- Extend the HTTP/1.1 reactor-driven normal reverse proxy path to HTTP/2 dispatch or define a separate async per-stream proxy adapter.
- Add per-upstream reverse proxy HTTPS CA bundle, certificate pinning, verify-depth, and verification override policy. Needs verification.
- Add reverse proxy response/request streaming and backpressure for normal HTTP traffic; current HTTP/1.1 async proxy path still buffers request and response bodies.
- Add fully asynchronous reverse proxy DNS/connect/TLS/handshake state machine for WebSocket proxy setup.
- Add active reverse proxy health checks, circuit-breaker metrics/admin inspection, and upstream connection pooling.
- Continue reverse proxy policy hardening after implemented `round_robin`, `failover`, and `stable_hash` selection; weighted policies remain planned if needed.
- Add compression configuration for gzip/Brotli enablement, minimum size, and MIME allowlist.
- Add caching.
- Add admin/control endpoint. Needs verification.
- Add metrics endpoint. Needs verification.

## P2 - Server-Side Runtimes

- P2 baseline decision accepted in ADR-0044: keep `script:runtime:path` declaration-only and do not shell out to system interpreters until a runtime-specific ADR is accepted.
- If bundling PHP, pin upstream source/version, license implications, build flags, static-link feasibility, extension policy, and security model. Needs verification.
- If bundling Python, pin upstream source/version, embedded interpreter model, module path policy, package isolation, and security model. Needs verification.
- If bundling Perl, pin upstream source/version, embedding API, module path policy, and security model. Needs verification.
- Define executable runtime contract beyond the current non-execution contract: entrypoint discovery, environment variables, request body streaming, timeout, memory limit, and process/thread isolation.
- Add implementation tests for the chosen future runtime path after an ADR accepts bundled runtime or explicitly external CGI/FastCGI behavior.

## P3 - Deployment

- Add install layout decision.
- Add systemd service file.
- Add container image.
- Add package build.
- Add log rotation config.
- Add production hardening guide.
- Revisit OCSP stapling only with an accepted design for issuer chain handling, responder fetch/cache/refresh, failure policy, SIGHUP behavior, outbound network policy, and real-client tests. Needs verification.

## Documentation Maintenance

- Update `docs/PROGRESS.md` after every meaningful implementation change.
- Update `docs/ARCHITECTURE.md` after module or runtime flow changes.
- Update `docs/DECISIONS.md` when a technical decision is accepted or changed.
- Update this TODO file whenever work is completed or new work is discovered.
- Add plan files under `docs/plans/` for large features before implementation.
