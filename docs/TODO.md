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
- Add integration tests for virtual host routing, fallback host behavior, reverse proxy success/failure, WebSocket proxy HTTP/HTTPS upstream behavior, and script-placeholder 501 behavior.
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

- Add production certificate management guidance.
- Add OCSP stapling. Needs verification.
- Add automated TLS ALPN `h2` integration test using a real HTTP/2 client, beyond the current Python SSL raw-frame smoke.
- Add automated test that ALPN advertises only implemented or explicitly partial-tested protocols and still rejects `h3`.
- Add tests for TLS config failure modes.
- Add automated integration test for multi-certificate SNI certificate selection.
- Add automated check that `rimau-server` is a fully static ELF and has no dynamic interpreter.
- Add automated check that the link command uses the bundled glibc sysroot when `RIMAU_USE_BUNDLED_GLIBC=ON`.
- Add documented bundled dependency update script to refresh OpenSSL, SQLite, zlib, Bison, Linux headers, and glibc versions and SHA256 pins.
- Add integration tests for reverse proxy hostname resolution with static glibc DNS/NSS behavior. Needs verification.

## P1 - Security Hardening

- Decide whether to integrate full `libmodsecurity` as a bundled source dependency or keep Rimau-native WAF rules only. Needs verification.
- If full OWASP Core Rule Set is bundled, pin the upstream source/version, license notes, update process, test corpus, and rule exclusion policy. Needs verification.
- Add WAF false-positive regression corpus for normal browser/curl traffic.
- Add per-virtual-host WAF enable/disable, anomaly threshold, and rule exception controls if required. Needs verification.
- Add structured WAF audit log format and persistence policy. Needs verification.
- Add ModSecurity rule syntax/parser support only if full compatibility is accepted. Needs verification.
- Add CTest or integration harness for IPv6 listener and IPv6 allowlist/blocklist behavior.
- Add support for arbitrary custom security header names if the fixed built-in set is not enough. Needs verification.
- Add token-bucket or leaky-bucket rate limiting if fixed-window behavior is too bursty. Needs verification.
- Add shared/distributed rate limiting strategy for multi-process or multi-node deployments. Needs verification.
- Add configurable deny response behavior for IP blocklist and connection-limit rejection.
- Add fuzz tests for HTTP parser/framing.
- Add hostname resolution or explicit dual IPv4/IPv6 listener configuration. Needs verification.

## P2 - HTTP/2

- Decide whether to continue native HTTP/2 session implementation or bundle a mature library such as `nghttp2` for full stream/session handling. Needs verification.
- Replace current inline partial h2c/TLS ALPN `h2` request-serving path with a dedicated HTTP/2 connection/session module.
- Add complete HTTP/2 stream lifecycle handling.
- Add CONTINUATION frame assembly.
- Complete HPACK support including Huffman and persistent dynamic table behavior, or replace baseline HPACK with chosen library.
- Add flow control integration.
- Harden TLS ALPN `h2` serving after HTTP/2 session behavior is validated.
- Add integration tests using a real HTTP/2 client for h2c and TLS `h2`.

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
- Move normal HTTP reverse proxy upstream I/O into the per-worker reactor or a dedicated async upstream state machine.
- Add per-upstream reverse proxy HTTPS CA bundle, certificate pinning, verify-depth, and verification override policy. Needs verification.
- Add reverse proxy response/request streaming and backpressure for normal HTTP traffic.
- Add fully asynchronous reverse proxy DNS/connect/TLS/handshake state machine for WebSocket proxy setup.
- Add active reverse proxy health checks, circuit-breaker metrics/admin inspection, and upstream connection pooling.
- Add advanced reverse proxy load balancing policies beyond current basic round-robin.
- Add compression configuration for gzip/Brotli enablement, minimum size, and MIME allowlist.
- Add caching.
- Add admin/control endpoint. Needs verification.
- Add metrics endpoint. Needs verification.

## P2 - Server-Side Runtimes

- Decide whether Rimau should bundle PHP, Python, Perl, one smaller embedded VM, or only expose optional CGI/FastCGI adapters. Needs verification.
- If bundling PHP, pin upstream source/version, license implications, build flags, static-link feasibility, extension policy, and security model. Needs verification.
- If bundling Python, pin upstream source/version, embedded interpreter model, module path policy, package isolation, and security model. Needs verification.
- If bundling Perl, pin upstream source/version, embedding API, module path policy, and security model. Needs verification.
- Define virtual host script routing contract, entrypoint discovery, environment variables, request body streaming, timeout, memory limit, and process/thread isolation.
- Add tests proving script vhost never shells out to system `php`, `python`, `perl`, or other external interpreters unless an explicitly documented optional external mode is accepted.

## P3 - Deployment

- Add install layout decision.
- Add systemd service file.
- Add container image.
- Add package build.
- Add log rotation config.
- Add production hardening guide.

## Documentation Maintenance

- Update `docs/PROGRESS.md` after every meaningful implementation change.
- Update `docs/ARCHITECTURE.md` after module or runtime flow changes.
- Update `docs/DECISIONS.md` when a technical decision is accepted or changed.
- Update this TODO file whenever work is completed or new work is discovered.
- Add plan files under `docs/plans/` for large features before implementation.
