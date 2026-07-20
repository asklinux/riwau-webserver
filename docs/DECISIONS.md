# Decisions

This file records important technical decisions. Add a new entry whenever design direction changes.

## ADR-0001: Persistent Project Memory

- Date: 2026-07-18
- Status: Accepted

Decision:

Create `AGENTS.md` and documentation under `docs/` as a persistent project-memory system. Every Codex session must read these files before work and update the relevant files after work.

Reason:

The project is expected to grow into a complex C++ web server. Persistent memory reduces repeated discovery and prevents future sessions from losing architecture context.

## ADR-0002: Use C++20 With CMake

- Date: 2026-07-18
- Status: Accepted

Decision:

Use C++20 and CMake as the initial build system.

Reason:

C++20 provides modern standard library features while remaining practical for systems programming. CMake is widely supported for C++ libraries, tests, and packaging.

## ADR-0003: Implement HTTP/1.1 Scaffold First

- Date: 2026-07-18
- Status: Accepted

Decision:

Start with a minimal HTTP/1.1 static file server before implementing HTTP/2 and HTTP/3.

Reason:

HTTP/1.1 gives a testable end-to-end foundation: socket accept, request parse, response generation, static file serving, config, and validation. HTTP/2 and HTTP/3 can then reuse core config, logging, serving, and routing decisions.

Consequence:

The current server is not yet equivalent to nginx/OpenLiteSpeed. It is a scaffold and must not be documented as production-ready.

## ADR-0004: No Database In Initial Server

- Date: 2026-07-18
- Status: Superseded by ADR-0008

Decision:

Do not add a database for the initial scaffold.

Reason:

The current product is a web server runtime. Static serving, listener config, and protocol handling do not require persistent application data.

Future control plane, metrics, cache metadata, or admin features may require storage. Needs verification.

## ADR-0005: Use Proven Libraries For HTTP/2 And HTTP/3

- Date: 2026-07-18
- Status: Proposed

Decision:

Use mature protocol libraries for HTTP/2 and HTTP/3 rather than hand-writing full protocol stacks from scratch.

Candidate direction:

- HTTP/2: `nghttp2`. Needs verification.
- HTTP/3: `ngtcp2` + `nghttp3` or `quiche`. Needs verification.
- TLS: bundled static OpenSSL for HTTP/1.1 HTTPS; ALPN/HTTP2 integration still needs verification.

Reason:

HTTP/2 and HTTP/3 require complex and security-sensitive behavior: frame parsing, HPACK/QPACK, stream multiplexing, flow control, QUIC, TLS 1.3, and ALPN. A proven library reduces risk.

## ADR-0006: Keep Secrets Out Of Repo Memory

- Date: 2026-07-18
- Status: Accepted

Decision:

Do not store passwords, tokens, API keys, private keys, production secrets, or real credentials in docs or committed config examples.

Reason:

Project memory files are intended for repeated use by automated agents. They must be safe to read and update.

## ADR-0007: Adapt Proxygen Concepts Without Copying Code

- Date: 2026-07-18
- Status: Accepted

Decision:

Use Proxygen as an architectural reference for Rimau Web Server, especially its separation of server/session, transaction, request handler, request handler factory, and downstream response writer concepts.

Implementation:

- Add Rimau-native `RequestHandler`.
- Add Rimau-native `RequestHandlerFactory`.
- Add Rimau-native `Transaction`.
- Add Rimau-native `ResponseSink`.
- Add Rimau-native `ResponseBuilder`.
- Refactor HTTP/1.1 static serving to use this pipeline.

Reason:

HTTP/1.1, HTTP/2, and HTTP/3 have different wire formats, but the server should reuse request routing and application/static-file handling across protocols. This mirrors the useful architecture lesson from Proxygen while keeping Rimau's code small, original, and dependency-light.

Consequence:

HTTP/2 and HTTP/3 still remain planned. The new pipeline only prepares the internal structure for later protocol implementations.

## ADR-0008: Store Runtime Configuration In SQLite

- Date: 2026-07-18
- Status: Accepted

Decision:

Read all runtime server configuration from a SQLite database instead of a key-value config file.

Implementation:

- Default database path is `data/rimau.sqlite3`.
- Runtime table is `rimau_config`.
- Supported keys are `host`, `port`, `document_root`, `max_request_bytes`, `http_keep_alive_enabled`, `http_keep_alive_timeout_seconds`, `http_keep_alive_max_requests`, `listen_backlog`, `server_name`, `worker_threads`, `epoll_max_events`, `reuse_port_enabled`, `tcp_keepalive_enabled`, `tcp_keepalive_idle_seconds`, `tcp_keepalive_interval_seconds`, `tcp_keepalive_probe_count`, `graceful_shutdown_timeout_seconds`, `connection_pool_size`, `http1_enabled`, `http2_enabled`, `http3_enabled`, `tls_enabled`, `tls_certificate_file`, and `tls_private_key_file`.
- `rimau-server --database path --check-config` bootstraps, reads, validates, and prints config.
- `rimau-server --database path --set key=value` writes supported values into SQLite.
- `--config` is removed and returns an error.

Reason:

The project owner requested SQLite as the source of configuration. A database-backed config also prepares the project for future admin/control-plane features.

Consequence:

SQLite is now the required runtime configuration store. Multi-step migrations, config reload, backup policy, and production database ownership still need verification. At this decision point SQLite was linked as a normal CMake dependency; ADR-0021 later changes the SQLite engine to a bundled static build so system `libsqlite3` is no longer a runtime dependency.

Note:

ADR-0015 later adds TLS/security/timeout/rate-limit/IP-list keys to the same SQLite configuration table.
ADR-0016 later adds multi-certificate SNI, IPv4/IPv6 IP list validation, and security header value keys.
ADR-0017 later adds virtual host and reverse proxy keys.
ADR-0021 later bundles SQLite as a static dependency.
ADR-0027 later adds SQLite config schema metadata and future-version rejection.

## ADR-0009: Bundle OpenSSL For Rimau TLS

- Date: 2026-07-18
- Status: Accepted

Decision:

Build OpenSSL from source as a bundled static dependency for Rimau Web Server instead of linking against the operating system OpenSSL.

Implementation:

- CMake target `rimau_bundled_openssl` downloads and builds OpenSSL.
- Bundled OpenSSL tag is `openssl-4.0.1`.
- Release archive SHA256 is pinned in `CMakeLists.txt`.
- OpenSSL is configured with `no-shared`, `no-tests`, and `no-docs`.
- Rimau links to `build/_deps/openssl/install/lib/libssl.a` and `libcrypto.a`.
- The generated `rimau-server` binary does not dynamically link to `libssl` or `libcrypto`.
- Dev certificate generation uses `build/_deps/openssl/install/bin/openssl`.

Reason:

The project owner requested SSL support to be built into Rimau so installation does not depend on external OpenSSL packages and OpenSSL can be updated for Rimau independently.

Consequence:

Build time is longer because OpenSSL is compiled from source. Updating OpenSSL requires changing the pinned tag and SHA256 in CMake, then rebuilding. This is not a custom TLS implementation; TLS correctness still relies on the upstream OpenSSL project.

## ADR-0010: Protocol Config Gates Must Not Pretend Implementation

- Date: 2026-07-18
- Status: Accepted

Decision:

Add SQLite protocol enable flags and protocol capability reporting for HTTP/1.1, HTTP/2, and HTTP/3, but keep HTTP/2 and HTTP/3 marked `not implemented` until real wire-level support exists.

Implementation:

- Add config keys `http1_enabled`, `http2_enabled`, and `http3_enabled`.
- `--protocols` now loads SQLite config and prints both configured state and implementation state.
- Startup fails if HTTP/1.1 is disabled while only unimplemented HTTP/2/HTTP/3 remain configured.
- HTTP/2 and HTTP/3 gateways return explicit unavailable status messages.
- At this decision point, ALPN `h2` and `h3` were not advertised yet.

Note: ADR-0024 later allows h2c-only startup when `http1_enabled=false`, `http2_enabled=true`, and `tls_enabled=false`. ADR-0025 later allows partial TLS ALPN `h2` request serving when HTTP/2 is enabled.

Reason:

The project needs a control-plane path for future protocol work, but clients would break if Rimau advertised HTTP/2 or HTTP/3 before frame/session/QUIC support exists.

Consequence:

HTTP/2 and HTTP/3 are now visible in config/status, but they remain planned at the wire protocol layer. Full support still requires mature protocol libraries, integration tests, and documentation updates.

Note:

ADR-0023 later adds partial native HTTP/2 and HTTP/3 wire codec primitives. ADR-0024 later adds partial cleartext h2c HTTP/2 request serving. Full TLS HTTP/2 and HTTP/3 request serving still remains pending.

## ADR-0011: Use Asynchronous Non-Blocking Runtime I/O

- Date: 2026-07-18
- Status: Superseded by ADR-0012

Decision:

Use an asynchronous event-loop architecture with non-blocking listener and client sockets for the main server runtime.

Implementation:

- Replace the thread-per-accepted-client runtime with a single `poll()` event loop in `src/core/server.cpp`.
- Set the listener socket and accepted client sockets to `O_NONBLOCK`.
- Manage each HTTP/1.1 client through a readiness-driven `ClientConnection` state machine.
- Handle TLS handshake, TLS read, TLS write, and TLS shutdown through OpenSSL `SSL_get_error()` with `SSL_ERROR_WANT_READ` and `SSL_ERROR_WANT_WRITE`.
- Remove the legacy blocking `Http1Connection` module from the build.

Reason:

The project owner requires Rimau to use asynchronous and non-blocking I/O. This also prepares the runtime for future HTTP/2 multiplexing, HTTP/3/QUIC, timeouts, backpressure, and high-concurrency serving.

Consequence:

The initial implementation used portable `poll()`. It has been replaced by Linux `epoll` per-worker reactors in ADR-0012.

## ADR-0012: Linux epoll Worker Reactor Runtime

- Date: 2026-07-18
- Status: Accepted

Decision:

Use Linux `epoll` with the Reactor pattern as the main runtime architecture. Each worker thread owns its own event loop and non-blocking listener socket.

Implementation:

- `worker_threads=0` auto-detects CPU core count through `std::thread::hardware_concurrency()`.
- Workers run as `std::jthread`.
- Each worker creates its own TCP listener and `epoll` fd.
- Listener sockets use `SO_REUSEADDR` and `SO_REUSEPORT`.
- Accepted client sockets use non-blocking I/O and optional TCP keepalive.
- HTTP/1.1 clients are managed by a per-worker `ClientConnection` state machine.
- OpenSSL TLS handshake/read/write/shutdown remains non-blocking through `SSL_ERROR_WANT_READ` and `SSL_ERROR_WANT_WRITE`.
- Each worker has a connection object pool. Reused connection objects keep their string buffer capacity to reduce malloc/free churn.
- SIGTERM and SIGINT trigger graceful shutdown. Workers stop accepting and drain active connections until `graceful_shutdown_timeout_seconds`.
- SIGHUP performs limited live reload for SQLite config values that do not require listener, worker, or TLS context recreation.

Reason:

The requested target architecture is an event-driven Linux web server. `epoll` fits a Reactor model where readiness events drive non-blocking accept/read/write work inside each worker. `SO_REUSEPORT` lets the kernel distribute new connections across per-worker listeners without adding a cross-thread queue.

Consequence:

This removes the need for a lock-free accept queue for the current design. A lock-free queue may become useful later if Rimau adds an internal load balancer, cross-worker handoff, async disk I/O completion path, or task scheduler. Performance still needs benchmark validation, and static file reads are still synchronous inside the handler path.

## ADR-0013: HTTP/1.1 Keep-Alive And Limited SQLite Live Reload

- Date: 2026-07-18
- Status: Accepted

Decision:

Add HTTP/1.1 persistent connections initially for bodyless requests and implement SIGHUP reload for SQLite config values that can safely change without restarting listener sockets, worker threads, connection pools, or TLS context.

Implementation:

- Add SQLite config keys `http_keep_alive_enabled`, `http_keep_alive_timeout_seconds`, and `http_keep_alive_max_requests`.
- Keep HTTP/1.1 connections open by default unless the client sends `Connection: close`, keep-alive is disabled, the per-connection request cap is reached, or the request has body framing that Rimau does not implement yet. ADR-0014 later adds `Content-Length` and chunked request body framing support.
- Allow HTTP/1.0 keep-alive only when the client sends `Connection: keep-alive`.
- Add per-connection idle timeout handling for keep-alive connections.
- Preserve non-blocking read/write behavior inside the existing Linux `epoll` worker reactor.
- Store a reloadable config snapshot in runtime control and publish new snapshots after accepted SIGHUP reloads.
- Reject SIGHUP changes to `host`, `port`, `listen_backlog`, `worker_threads`, `epoll_max_events`, `reuse_port_enabled`, `connection_pool_size`, `http1_enabled`, `tls_enabled`, `tls_certificate_file`, and `tls_private_key_file` because those require listener, worker, pool, or TLS recreation.

Reason:

HTTP/1.1 persistent connections are required for practical web server behavior and reduce TCP/TLS setup churn. SIGHUP reload is expected operational behavior, but only config values with safe runtime semantics should change without restart.

Consequence:

At this decision point, keep-alive was limited to request-header-only flows. ADR-0014 later expanded HTTP/1.1 body framing and basic pipelining. TLS certificate reload remains future work. Live reload currently updates dynamic behavior such as document root, max request size, keep-alive settings, TCP keepalive settings for new sockets, protocol status flags, and graceful shutdown timeout.

## ADR-0014: Expand HTTP/1.1 Scaffold Before HTTP/2/HTTP/3

- Date: 2026-07-18
- Status: Accepted

Decision:

Expand the HTTP/1.1 implementation with practical protocol behavior before starting HTTP/2 and HTTP/3 wire protocol work.

Implementation:

- Parse URL-decoded paths, query strings, query parameters, headers, and request body.
- Detect complete HTTP/1.1 messages using headers, `Content-Length`, or `Transfer-Encoding: chunked`.
- Decode chunked request bodies before dispatching to the handler pipeline.
- Keep basic request pipelining by buffering later requests and dispatching the next request only after the previous response is written.
- Support GET, HEAD, OPTIONS, POST, PUT, PATCH, and DELETE at the handler layer.
- Keep GET/HEAD as static file behavior.
- Return JSON metadata/body scaffold responses for POST, PUT, PATCH, and DELETE. These methods do not mutate files yet.
- Add single byte-range static file responses for large files and video clients.
- Add broader MIME type detection.
- Add gzip compression for compressible static responses when `Accept-Encoding: gzip` is present.
- Add basic RFC 6455 WebSocket upgrade and two-way echo for text/binary frames, ping/pong, and close.

Reason:

HTTP/2 and HTTP/3 need a stronger HTTP/1.1 foundation: body framing, keep-alive, pipelining behavior, static file range support, compression, JSON behavior, and WebSocket upgrade are all common web-server surfaces. Implementing these in HTTP/1.1 first makes the later shared transaction and routing layers more concrete.

Consequence:

This is still not a production-complete HTTP server. Request bodies are buffered in memory, chunked trailers are not exposed to handlers, JSON is not parsed into a structured DOM, multipart ranges are not implemented, compression is gzip only through zlib, WebSocket support is a basic echo endpoint without fragmentation/extensions/subprotocol routing, and static file reads remain synchronous.

## ADR-0015: Add TLS And HTTP Security Hardening Controls

- Date: 2026-07-18
- Status: Accepted

Decision:

Add SQLite-configured TLS hardening and baseline HTTP security controls to the current HTTP/1.1 runtime before starting HTTP/2/HTTP/3 wire-level work.

Implementation:

- Keep bundled static OpenSSL as the only TLS provider.
- Enforce TLS protocol range through `tls_min_version` and `tls_max_version`; defaults are TLS 1.2 through TLS 1.3.
- Configure TLS 1.2 cipher list and TLS 1.3 ciphersuites through SQLite.
- Add ALPN callback, but at this decision point advertise only `http/1.1` until HTTP/2/HTTP/3 are actually implemented.
- Add SNI callback for hostname validation against `tls_sni_hosts`; `tls_sni_required` can reject clients without SNI.
- Reload TLS certificate/key/version/cipher/ALPN/SNI settings on SIGHUP by publishing a new TLS context for new connections. Existing TLS sessions keep their original context.
- Add request/header/body/idle timeout controls.
- Add global and per-IP active connection limits.
- Add in-memory fixed-window per-IP request rate limiting.
- Add IPv4 exact/CIDR allowlist and blocklist.
- Reject invalid HTTP framing that can lead to request smuggling: duplicate/invalid `Content-Length`, `Content-Length` with `Transfer-Encoding`, duplicate/unsupported `Transfer-Encoding`, obs-fold, and bare CR/LF.
- Sanitize serialized response header values and allow disabling the `Server` header.
- Add default security headers when `security_headers_enabled=true`.
- Add `websocket_max_frame_bytes`.
- Recanonicalize directory index paths during static file serving to reduce symlink escape risk.

Reason:

The project owner requested security behavior comparable to a real web server. These controls protect the current HTTP/1.1 scaffold from common operational risks while preserving the non-blocking epoll architecture and SQLite-backed configuration model.

Consequence:

This is still baseline hardening, not complete production security. At this decision point, SNI did not select multiple certificates yet, rate limiting was in-memory and per process, IP allow/block lists were IPv4-only, security headers were fixed defaults, and HTTP/2/HTTP/3 ALPN remained disabled until real protocol support existed.

Note:

ADR-0016 later implements multi-certificate SNI selection, IPv4/IPv6 IP lists, and SQLite-configurable values for the fixed security header set.

## ADR-0016: Add SNI Multi-Certificate, IPv6 IP Lists, And Configurable Security Header Values

- Date: 2026-07-19
- Status: Accepted

Decision:

Extend the existing SQLite-backed TLS/security controls without adding new runtime config files.

Implementation:

- Add SQLite key `tls_sni_certificates` with semicolon-separated entries in the form `hostname-pattern=certificate.pem|private-key.pem`.
- Keep `tls_certificate_file` and `tls_private_key_file` as the default/fallback certificate context.
- Create one OpenSSL `SSL_CTX` per configured SNI certificate entry, using the same TLS version, cipher, ciphersuite, SNI, and ALPN rules as the default context.
- Select the matching certificate context in the OpenSSL SNI callback when a client sends a matching hostname. Exact names and simple `*.domain` wildcards are supported.
- Preserve `tls_sni_hosts` as an optional SNI allowlist. SNI certificate patterns are also accepted by the callback.
- Add IPv6 support to listener bind when `host` is an IPv6 literal.
- Add IPv4/IPv6 exact and CIDR matching for `ip_allowlist` and `ip_blocklist`.
- Validate IP lists during SQLite config load and `--set` updates.
- Add SQLite keys for the existing security header values:
  - `security_header_content_security_policy`
  - `security_header_strict_transport_security`
  - `security_header_x_content_type_options`
  - `security_header_x_frame_options`
  - `security_header_referrer_policy`
  - `security_header_cross_origin_opener_policy`
- Empty security header values disable that individual header while `security_headers_enabled=true`.
- At this decision point, continue advertising only ALPN `http/1.1`.

Reason:

These changes close practical hardening gaps while preserving the core constraints: SQLite is the only runtime configuration source, TLS remains bundled OpenSSL, and at this decision point HTTP/2/HTTP/3 were not advertised before real wire-level implementations existed.

Consequence:

Rate limiting remains in-memory and per process. Security header names are still a fixed set, though values can now be changed or disabled. Bind config accepts IPv4 or IPv6 literals, but there is no hostname resolution or explicit dual IPv4/IPv6 listener mode yet. HTTP/2 and HTTP/3 remain unimplemented at the wire level.

## ADR-0017: SQLite Virtual Hosts, Baseline Reverse Proxy, And Runtime Declarations

- Date: 2026-07-19
- Status: Accepted

Decision:

Add virtual host routing and baseline reverse proxy support through SQLite configuration, without adding new runtime config files.

Implementation:

- Add SQLite keys `virtual_hosts_enabled`, `virtual_hosts`, `reverse_proxy_connect_timeout_seconds`, `reverse_proxy_read_timeout_seconds`, and `reverse_proxy_max_response_bytes`.
- Parse `virtual_hosts` as semicolon-separated rules in the form `host=static:path`, `host=proxy:http://upstream:port/base`, or `host=script:runtime:path`.
- Match exact hostnames first, then simple wildcard hostnames in the form `*.domain`.
- Keep global `document_root` as fallback when no virtual host rule matches.
- Add static virtual hosts with per-host document roots.
- Add HTTP reverse proxy virtual hosts for upstream `http://` targets.
- Strip hop-by-hop proxy headers, set upstream `Host`, set `Connection: close`, forward `X-Forwarded-Host`, and enforce a buffered upstream response limit.
- Add script virtual host declarations for runtime names such as `php`, `python`, and `perl`, but return explicit `501 Not Implemented` until a bundled runtime is integrated.
- Keep runtime configuration reloadable through SIGHUP for virtual host and reverse proxy settings.

Reason:

The project owner requested virtual hosts, reverse proxy, and server-side language selection from virtual host config. SQLite remains the project-wide runtime configuration source, so vhost/proxy routing must live there instead of in separate config files.

Consequence:

Reverse proxy is a baseline implementation only. Upstream support is HTTP-only, response handling is buffered, DNS lookup uses `getaddrinfo`, and the handler waits on upstream readiness with `poll()` rather than integrating upstream sockets into each worker reactor. There is no HTTPS upstream, WebSocket proxying, load balancing, retry policy, health checks, circuit breaker, response streaming, or upstream connection pooling yet.

PHP, Python, Perl, and arbitrary server-side languages are not implemented. Calling system interpreters would violate the no-external-runtime-dependency requirement. Bundling any runtime needs a separate decision, pinned source/version policy, build integration, security model, routing contract, and tests. Needs verification.

Note:

ADR-0018 later adds HTTPS upstream transport, multiple upstream targets, basic round-robin selection, and retry/failover. ADR-0019 later adds WebSocket reverse proxy tunneling.

## ADR-0018: Reverse Proxy HTTPS Upstreams, Upstream Pools, And Retry

- Date: 2026-07-19
- Status: Accepted

Decision:

Extend the baseline reverse proxy with HTTPS upstream transport, multiple upstream targets per virtual host, basic round-robin selection, and retry/failover.

Implementation:

- Keep `virtual_hosts` as the SQLite source of reverse proxy routing.
- Allow comma-separated upstream URLs inside one `proxy:` rule.
- Support `http://` and `https://` upstream schemes.
- Use bundled OpenSSL for upstream HTTPS client transport.
- Add SQLite key `reverse_proxy_retry_count` for additional attempts after the first upstream failure.
- Add SQLite key `reverse_proxy_tls_verify_upstream` to enable upstream HTTPS certificate verification through OpenSSL default trust paths.
- Rotate the first attempted upstream with a process-local atomic counter for basic round-robin behavior.
- Retry/failover handles connection, write, read, TLS handshake, and invalid upstream response failures.
- Continue stripping hop-by-hop headers and buffering upstream responses under `reverse_proxy_max_response_bytes`.

Reason:

Reverse proxy usefulness depends on at least basic upstream pools, failover, and HTTPS upstream support. These features can be added without changing the current SQLite configuration model or adding a system TLS dependency.

Consequence:

At this decision point, this was still not a production-complete reverse proxy. DNS resolution still used blocking `getaddrinfo`, upstream sockets still waited with `poll()` inside the handler instead of being integrated into the per-worker `epoll` reactor, and response bodies were still buffered. HTTPS upstream verification could be enabled, but per-upstream CA bundles, pinning, verify-depth, and operational policy still needed a dedicated design. At this decision point there was no health-check scheduler, circuit breaker, advanced load-balancing policy, upstream connection pooling, request/response streaming, or WebSocket proxying yet.

Note:

ADR-0019 later adds WebSocket reverse proxy tunneling. ADR-0020 later adds passive circuit breaker while leaving normal HTTP proxy response streaming, active health checks, and upstream connection pooling as future work.

## ADR-0019: WebSocket Reverse Proxy Tunnel For Proxy Virtual Hosts

- Date: 2026-07-19
- Status: Accepted

Decision:

Route HTTP/1.1 WebSocket Upgrade requests to reverse proxy virtual hosts before the local echo handler. After a successful upstream `101 Switching Protocols` response, tunnel client/upstream bytes through the same worker `epoll` reactor.

Implementation:

- Keep WebSocket proxy routing inside SQLite `virtual_hosts`; no new config file is added.
- Reuse reverse proxy upstream pools, retry/failover, timeout, and `reverse_proxy_tls_verify_upstream` settings.
- Support upstream `http://` and `https://` WebSocket handshakes. HTTPS upstream transport uses bundled OpenSSL.
- Rebuild and sanitize the upstream `101 Switching Protocols` response before sending it to the client.
- Add an `upstream` epoll token so `ClientConnection` can own both client fd and WebSocket upstream fd.
- Tunnel bytes in both directions after handshake; client TLS and upstream TLS are handled through OpenSSL non-blocking read/write state.
- Keep non-proxy WebSocket requests on the existing basic local echo path.
- Count WebSocket proxy handshakes against per-IP request rate limiting.

Reason:

Reverse proxy virtual hosts need WebSocket Upgrade support for modern application backends. The data tunnel belongs in the worker reactor so a WebSocket proxy connection does not require a dedicated blocking thread.

Consequence:

This is still a baseline WebSocket proxy. DNS resolution, TCP connect, TLS handshake, upstream request write, and upstream response header read occur before tunnel mode and still use short wait operations. Once the tunnel starts, data movement is event-driven through the worker `epoll` loop. The tunnel does not yet implement full WebSocket frame policy for fragmentation, subprotocol negotiation, extensions, or per-frame proxy inspection. Needs verification for high-load backpressure behavior.

## ADR-0020: Process-Local Passive Reverse Proxy Circuit Breaker

- Date: 2026-07-19
- Status: Accepted

Decision:

Add a process-local passive circuit breaker for reverse proxy upstreams. The breaker opens after a configurable number of upstream failures and skips that upstream until a configurable cooldown expires.

Implementation:

- Add SQLite keys:
  - `reverse_proxy_circuit_breaker_enabled`
  - `reverse_proxy_circuit_breaker_failure_threshold`
  - `reverse_proxy_circuit_breaker_cooldown_seconds`
- Store circuit breaker state in memory, keyed by upstream scheme, authority, and base path.
- Share the same state between normal HTTP reverse proxy handling and WebSocket reverse proxy setup.
- Record success after a valid upstream response/handshake and clear the upstream circuit state.
- Record failure for connect, TLS handshake, write, read, invalid upstream response, or invalid WebSocket upstream handshake.
- Return `503 Service Unavailable` with `Retry-After` when every selected upstream is skipped because its circuit is open.

Reason:

Repeatedly selecting a just-failed upstream wastes request latency and can amplify failure load. A passive circuit breaker gives baseline resilience without adding a health-check scheduler, background worker, or external dependency.

Consequence:

The circuit breaker is in-memory and per process. It is not distributed across nodes and is not persisted in SQLite. It is passive: upstreams are reopened only after cooldown, not by active probing. Health checks, circuit-breaker metrics, per-failure-class policies, and upstream connection pooling remain planned.

## ADR-0021: Bundle SQLite And zlib Static Dependencies

- Date: 2026-07-19
- Status: Accepted

Decision:

Build SQLite and zlib as bundled static dependencies for Rimau Web Server instead of linking the generated server binary to system `libsqlite3` or `libz`.

Implementation:

- Add CMake option `RIMAU_USE_BUNDLED_SQLITE=ON` and reject system SQLite builds.
- Add CMake option `RIMAU_USE_BUNDLED_ZLIB=ON` and reject system zlib builds.
- Add CMake target `rimau_bundled_sqlite` using official SQLite amalgamation `3.53.3`.
- Pin SQLite amalgamation release id `3530300` and ZIP SHA256 in `CMakeLists.txt`.
- Compile `sqlite3.c` into `build/_deps/sqlite/install/lib/libsqlite3.a`.
- Compile SQLite with `SQLITE_OMIT_LOAD_EXTENSION=1` so runtime loadable SQLite extensions are disabled.
- Add CMake target `rimau_bundled_zlib` using official zlib `1.3.2`.
- Pin zlib release archive SHA256 in `CMakeLists.txt`.
- Link Rimau against `build/_deps/zlib/install/lib/libz.a`.
- Add CMake option `RIMAU_STATIC_CXX_RUNTIME=ON` to static-link `libstdc++` and `libgcc` for `rimau-server` on Linux GNU/Clang builds when supported.

Reason:

The project owner requires Rimau to avoid external library dependencies where practical, and earlier validation still showed dynamic links to `libz.so.1` and `libsqlite3.so.0`. Bundling these small C libraries keeps SQLite-based configuration and gzip compression while making the deployed server less dependent on host packages.

Consequence:

Build time and first-build network dependency increase because zlib and SQLite source archives are downloaded and built. Updating SQLite or zlib now requires changing the pinned version, URL, and SHA256 in CMake, then rebuilding. The server binary no longer dynamically links to `libssl`, `libcrypto`, `libz`, `libsqlite3`, `libstdc++`, or `libgcc_s` in the current Linux validation. ADR-0022 later changes the Linux deployment target to link `rimau-server` as a fully static ELF through a bundled glibc sysroot.

## ADR-0022: Build glibc From Original Source For Fully Static Server

- Date: 2026-07-19
- Status: Accepted

Decision:

Build GNU glibc from original upstream source inside the Rimau CMake build and link the deployable `rimau-server` binary as a fully static Linux x86_64 ELF through that bundled sysroot.

Implementation:

- Add CMake option `RIMAU_FULLY_STATIC_SERVER=ON` for static Linux ELF linking.
- Add CMake option `RIMAU_USE_BUNDLED_GLIBC=ON` and reject use without full static linking.
- Restrict bundled glibc wiring to Linux x86_64 until other architectures are explicitly implemented and validated.
- Add `rimau_bundled_bison` to build GNU Bison `3.8.2` from GNU source.
- Add `rimau_bundled_linux_headers` to install UAPI headers from Linux kernel source `6.18.7`.
- Add `rimau_bundled_glibc` to build GNU glibc `2.43` from GNU source into `build/_deps/glibc/sysroot`.
- Configure glibc with `--prefix=/usr`, `--disable-werror`, `--without-selinux`, `--enable-kernel=3.2.0`, and `--with-headers=<bundled-linux-headers>/include`.
- Link `rimau-server` with `-static`, `-B<glibc-sysroot>/usr/lib`, `--sysroot=<glibc-sysroot>`, and `-L<glibc-sysroot>/usr/lib`.
- Keep bundled OpenSSL configured with `no-dso` so the static server does not require OpenSSL provider/module loading through host dynamic libraries.

Reason:

The project owner explicitly requested glibc to be built into the system from original source, not linked or imported from the host operating system. Building a Rimau-specific glibc sysroot makes the runtime binary independent from host `libc.so.6` and the Linux dynamic loader for the current Linux x86_64 target.

Consequence:

First build time is significantly longer and requires network access unless source archives are cached. Current validation shows `ldd build/rimau-server` reports `not a dynamic executable`, `file build/rimau-server` reports statically linked, and `readelf -l build/rimau-server` has no dynamic interpreter. glibc configure warns that `makeinfo` is missing, so some glibc documentation/tests are disabled on this machine. Static glibc still has a DNS/NSS caveat: linker warnings report that `getaddrinfo` and OpenSSL `gethostbyname` in statically linked applications can require matching glibc shared libraries/modules at runtime. Reverse proxy upstream hostnames therefore need dedicated production validation; literal IP upstreams avoid that specific hostname-resolution path. Needs verification.

Note:

GitHub Actions fast CI configures `RIMAU_FULLY_STATIC_SERVER=OFF` and `RIMAU_USE_BUNDLED_GLIBC=OFF`; glibc-related ExternalProject targets are excluded from default `all` and are only pulled into the build when the fully static server target depends on bundled glibc.

## ADR-0023: Add Native HTTP/2 And HTTP/3 Wire Codec Primitives First

- Date: 2026-07-19
- Status: Accepted

Decision:

Add small native wire codec primitives for HTTP/2 and HTTP/3 before integrating a full session library or live QUIC stack.

Implementation:

- Add HTTP/2 frame parser/serializer, SETTINGS payload parser/serializer, and basic frame validation.
- Add HPACK baseline support for static-table indexed fields and literal fields without Huffman or dynamic table indexing.
- Add cleartext h2c prior-knowledge handling in `ClientConnection`: parse client preface + SETTINGS, reply SETTINGS + SETTINGS ACK + GOAWAY, then close.
- Add HTTP/3 QUIC varint parser/serializer.
- Add HTTP/3 frame parser/serializer and SETTINGS payload parser/serializer.
- Add CTest targets `rimau_http2_wire` and `rimau_http3_wire`.
- At this decision point, keep ALPN `h2` and `h3` disabled until HTTP/2/HTTP/3 request serving is implemented and tested end-to-end.

Reason:

The project owner asked to start closing the HTTP/2 and HTTP/3 wire-level gap. Implementing minimal codecs gives testable wire-level building blocks while avoiding a premature claim that full stream/session/QUIC behavior is production-ready.

Consequence:

HTTP/2 and HTTP/3 status is now `partial`, not merely planned. At this decision point, HTTP/2 could perform a limited h2c preface/SETTINGS/ACK/GOAWAY exchange when `http2_enabled=true`, but it could not serve HTTP/2 requests yet. ADR-0024 later extends this to partial h2c request serving. HTTP/3 has wire codec primitives only; it still has no UDP listener, QUIC transport, TLS 1.3 QUIC handshake, QPACK, or request adapter. A future decision is still needed for full HTTP/2 session handling and full HTTP/3 QUIC/QPACK integration, likely using mature bundled source dependencies. Needs verification.

## ADR-0024: Add Partial Native h2c HTTP/2 Request Serving

- Date: 2026-07-19
- Status: Accepted

Decision:

Extend the native HTTP/2 path from handshake-only to partial cleartext h2c request serving while keeping ALPN `h2` disabled for this phase.

Implementation:

- Keep HTTP/2 support inside the existing non-blocking `ClientConnection` worker reactor for this phase.
- After cleartext h2c prior-knowledge preface and client SETTINGS, reply with server SETTINGS and SETTINGS ACK, then keep the connection open.
- Parse HTTP/2 SETTINGS ACK, PING, RST_STREAM, WINDOW_UPDATE, HEADERS, and DATA basics.
- Decode HEADERS through the existing HPACK baseline.
- Add HPACK decode support for literal fields with incremental indexing, but do not persist or reference a dynamic table yet.
- Build `rimau::http::Request` from HTTP/2 pseudo-headers and regular headers, then dispatch through the shared `Transaction` and `VirtualHostHandlerFactory` pipeline.
- Serialize handler responses as HTTP/2 HEADERS and DATA frames.
- Allow h2c-only startup when `http1_enabled=false`, `http2_enabled=true`, and `tls_enabled=false`.
- Keep TLS ALPN `h2` disabled for this phase until the TLS HTTP/2 path is added and tested.

Reason:

The project owner asked to continue the missing HTTP/2 wire-level implementation. Cleartext h2c request serving gives an end-to-end HTTP/2 path that reuses the existing request pipeline without prematurely introducing a large third-party session stack.

Consequence:

HTTP/2 remains partial, not production-complete. At this decision point, the path was cleartext h2c only. HPACK Huffman, persistent dynamic table behavior, CONTINUATION assembly, full stream lifecycle, production multiplexing semantics, flow control, priority handling, TLS ALPN `h2`, and real-client integration tests remained pending. Full HTTP/2 may still need a mature bundled source dependency such as `nghttp2`. Needs verification.

Note:

ADR-0025 later adds partial TLS ALPN `h2` request serving, but the broader HTTP/2 production gaps still remain.

## ADR-0025: Add Partial TLS ALPN h2 HTTP/2 Request Serving

- Date: 2026-07-20
- Status: Accepted

Decision:

Allow TLS ALPN `h2` when `http2_enabled=true` and route negotiated TLS `h2` connections into the existing partial native HTTP/2 request-serving path, while continuing to mark HTTP/2 as partial rather than production-complete.

Implementation:

- Allow `tls_alpn_protocols` to contain `h2` only when HTTP/2 is enabled for the configured TLS serving path.
- Keep rejecting ALPN `h3` until HTTP/3 live serving exists.
- Build the OpenSSL ALPN wire list from enabled protocol flags so disabled protocols are not advertised.
- Store the selected ALPN protocol after `SSL_accept`.
- When selected ALPN is `h2`, require the HTTP/2 client connection preface over TLS.
- Reuse the partial HTTP/2 server path from ADR-0024: client preface + SETTINGS, SETTINGS response + ACK, HEADERS/DATA decode through the HPACK baseline, shared `Transaction` dispatch, and HTTP/2 HEADERS/DATA response serialization.
- Send GOAWAY with protocol error when a TLS connection negotiates `h2` but does not send the HTTP/2 client preface.
- Allow HTTP/2-only TLS startup when `http1_enabled=false`, `http2_enabled=true`, `tls_enabled=true`, and `tls_alpn_protocols` includes `h2`.

Reason:

The project already had partial HTTP/2 request serving for h2c. Routing TLS ALPN `h2` into that same state machine closes the next protocol gap without introducing a second HTTP/2 implementation or claiming full HTTP/2 support.

Consequence:

HTTP/2 over TLS is now usable for basic non-Huffman HEADERS/DATA request/response smoke tests, but it is not production-complete. HPACK Huffman, dynamic table persistence, CONTINUATION assembly, production stream lifecycle, broad multiplexing semantics, priority, and full flow control remain pending. Current TLS `h2` validation uses a Python stdlib SSL client with manually built HTTP/2 frames; automated real-client HTTP/2 tests are still needed. HTTP/3 remains wire-codec-only with no ALPN `h3`.

## ADR-0026: Add Built-In ModSecurity-Compatible WAF Subset

- Date: 2026-07-20
- Status: Accepted

Decision:

Add a built-in WAF module that is ModSecurity-compatible at the request-inspection/anomaly-score concept level, using a small OWASP CRS-inspired rule subset compiled into Rimau source. Do not vendor or claim full `libmodsecurity` or the full OWASP Core Rule Set in this phase.

Implementation:

- Add `rimau::http::WafSettings`, `WafResult`, `WafMatch`, `inspect_request`, and `waf_block_response`.
- Add SQLite config keys `modsecurity_enabled`, `modsecurity_owasp_crs_enabled`, `modsecurity_blocking_enabled`, `modsecurity_anomaly_threshold`, `modsecurity_max_inspection_bytes`, and `modsecurity_audit_log_enabled`.
- Default `modsecurity_enabled=false` to avoid unexpected blocking on existing installs.
- Inspect HTTP/1.1 requests before `Transaction` dispatch.
- Inspect WebSocket upgrade requests before local echo or reverse proxy tunnel setup.
- Inspect partial HTTP/2 request objects before shared handler dispatch.
- Block with `403 Forbidden` and `x-rimau-waf-*` headers when blocking mode is enabled and anomaly score reaches threshold.
- Add CTest target `rimau_waf`.

Reason:

The project owner requested ModSecurity and OWASP rules to be built into the system. A complete `libmodsecurity` plus full OWASP CRS integration is a larger dependency, licensing, rule-update, parser, phase-engine, and performance project. The built-in subset gives immediate request protection without adding external runtime dependencies, while keeping the documentation honest about what is not yet implemented.

Consequence:

The current WAF is useful but partial. It does not parse ModSecurity rule syntax, does not implement the full ModSecurity transaction phase model, does not bundle the full OWASP Core Rule Set, does not provide per-virtual-host rule tuning, and does not persist structured audit logs. Full `libmodsecurity` and full OWASP CRS source bundling remain planned and need version/license/build validation before being claimed. Needs verification.

## ADR-0027: Version The SQLite Config Schema

- Date: 2026-07-20
- Status: Accepted

Decision:

Add explicit SQLite config schema metadata through `rimau_schema_migrations` and define current config schema version `1`.

Implementation:

- Add table `rimau_schema_migrations(version INTEGER PRIMARY KEY NOT NULL, name TEXT NOT NULL, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)`.
- Bootstrap version `1` with name `initial rimau_config schema`.
- Add `rimau::core::config_schema_version` for tests and diagnostics.
- Existing databases that have `rimau_config` but no metadata are bootstrapped to version `1`.
- Reject SQLite config databases whose recorded schema version is newer than the binary supports.
- Add config database test coverage for fresh DB bootstrap, legacy metadata bootstrap, and future-version rejection.

Reason:

Rimau now stores all runtime configuration in SQLite. Without schema metadata, future config-table changes could silently run against an unknown database layout. A minimal migration history table gives the project a durable upgrade marker without adding an ORM or a larger admin subsystem.

Consequence:

The current migration system records version history and prevents accidental downgrade reads, but it is not yet a full multi-step migration framework. Future schema changes still need explicit migration functions, downgrade policy, backup/restore guidance, and production ownership rules. Needs verification.

## ADR-0028: GPL-3.0 License Status Needs Owner Verification

- Date: 2026-07-20
- Status: Needs verification

Observation:

The repository contains a root `LICENSE` file with GNU General Public License version 3 text. This file was present in the target GitHub repository and is preserved in the local workspace.

Decision:

No final project-license decision is recorded by the project owner in this workspace beyond the existing `LICENSE` file.

Reason:

Licensing is a project ownership decision, not an implementation inference. The presence of a GPL-3.0 license file is strong repository evidence, but the project owner has not explicitly confirmed in these docs that GPL-3.0 is the intended final license for Rimau Web Server.

Consequence:

Treat GPL-3.0 as the current repository license marker when preserving files and planning bundled dependencies, but keep the final license intent marked `Needs verification` until the project owner confirms it.

## ADR-0029: Use File-Backed HTTP/1.1 Request Body Spooling As Interim Large-Upload Support

- Date: 2026-07-20
- Status: Accepted

Decision:

For the current HTTP/1.1 path, large request bodies are accumulated through a bounded-memory `RequestBodyAccumulator` before handler dispatch. Bodies stay in memory up to 16 KiB; larger bodies are written to a `mkstemp`-created temporary file and exposed through `rimau::http::RequestBodyFile`.

Implementation:

- Add `RequestBodyFile` RAII cleanup for temporary request-body files.
- Add `Request::body_size()`, `Request::body_spooled_to_file()`, and `Request::body_text(max_bytes)`.
- When `next_http1_request_frame` reports headers complete but body incomplete, `ClientConnection` switches into a body accumulation state for Content-Length or chunked bodies.
- WAF inspection reads bounded body text through `Request::body_text(limit)`.
- Static method scaffold responses report `body_spooled` and `body_truncated`.

Reason:

Rimau needs to stop requiring large uploads to live entirely in `Request::body` memory before the full handler-level streaming API is designed. File-backed spooling gives a smaller, testable step that keeps the existing transaction pipeline stable.

Consequence:

This does not complete streaming request body support. Handler dispatch still waits for the full request body. Normal HTTP reverse proxy forwarding still reads the spooled body into memory before sending upstream. A future design must add handler-visible body streams, reverse proxy request/response streaming, producer-side async response backpressure, and explicit backpressure semantics.

## ADR-0030: Add Basic HTTP/1.1 Chunked Response API

- Date: 2026-07-20
- Status: Accepted

Decision:

Add a basic handler-facing chunked response API for HTTP/1.1. Handlers can send response chunks without knowing `Content-Length` up front through `ResponseSink::send_chunked` or `ResponseBuilder::send_chunked`.

Implementation:

- Add `ResponseSink::send_chunked` with a default fallback that concatenates chunks for sinks that do not override it.
- Add `ResponseBuilder::send_chunked`.
- Add `Response::to_http_chunked_string` and `encode_chunked_body`.
- Update the HTTP/1.1 `BufferedResponseSink` to serialize chunked responses with `Transfer-Encoding: chunked` and without `Content-Length`.
- Keep HTTP/2 handling compatible by retaining the concatenated response body in the captured response object.

Reason:

P1 requires handlers to be able to send a response without a known `Content-Length`. HTTP/1.1 chunked transfer encoding is the compatible baseline for that behavior and fits the current request handler pipeline.

Consequence:

This is not a full asynchronous response producer model. The current implementation still serializes chunked output into the connection response buffer before socket write. Producer-side async chunk generation, response backpressure, and reverse proxy response streaming remain pending.
