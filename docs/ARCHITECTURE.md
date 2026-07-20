# Architecture

## Overview

Rimau Web Server disusun sebagai web server C++ modular. Matlamat jangka panjang ialah menyokong HTTP/1.1, HTTP/2, dan HTTP/3 dengan architecture yang boleh berkembang ke arah event-driven, TLS/ALPN, reverse proxy, static serving, caching, observability, dan plugin.

Status semasa ialah scaffold awal yang sudah mempunyai HTTP/1.1 praktikal, HTTPS/TLS hardening asas, kawalan keselamatan runtime asas, built-in ModSecurity-compatible WAF dengan subset OWASP CRS-inspired rules, SQLite virtual host routing, baseline reverse proxy dengan HTTP/HTTPS upstream, multi-upstream round-robin asas, retry/failover asas, WebSocket reverse proxy tunneling, passive circuit breaker, HTTP/2 wire codec partial, partial cleartext h2c dan TLS ALPN `h2` request serving, dan HTTP/3 wire codec primitives. HTTP/2 production session behavior, HTTP/3 live request serving, full `libmodsecurity`, dan full OWASP Core Rule Set masih belum lengkap.

Pada 2026-07-18, Rimau mula mengadaptasi konsep daripada Proxygen (`https://github.com/facebook/proxygen`) secara seni bina, bukan salinan kod. Konsep yang diambil ialah pemisahan connection/session, transaction, request handler, request handler factory, dan downstream response writer.

## Runtime Flow Semasa

```text
main()
  -> load_config_from_database()
  -> SQLite rimau_config table
  -> protocol capability/config gate checks
  -> Server::run()
  -> install SIGTERM/SIGINT/SIGHUP handlers
  -> resolve worker_threads from SQLite or CPU cores
  -> std::jthread worker pool
  -> each worker creates non-blocking SO_REUSEPORT listener
  -> each worker owns a Linux epoll reactor
  -> enforce IPv4/IPv6 IP allow/block list and global/per-IP connection limits
  -> non-blocking accept/read/write per client in worker
  -> optional non-blocking TLS 1.2/1.3 handshake with SNI certificate selection and ALPN http/1.1 or h2 if tls_enabled=true
  -> enforce request/header/body/idle timeouts
  -> if cleartext HTTP/2 preface or TLS ALPN h2 preface and http2_enabled=true: parse SETTINGS, write SETTINGS + SETTINGS ACK, then process HTTP/2 HEADERS/DATA frames
  -> for complete HTTP/2 streams: decode HPACK baseline headers, build Request, dispatch through Transaction, serialize response as HTTP/2 HEADERS/DATA frames
  -> detect complete HTTP/1.1 message through rimau::http::next_http1_request_frame headers, Content-Length, or chunked transfer decoding
  -> for HTTP/1.1 headers-complete/body-incomplete requests: stream incoming Content-Length or chunked body bytes into RequestBodyAccumulator with 16 KiB memory threshold and mkstemp-backed temporary-file spooling
  -> reject invalid HTTP framing and request-smuggling patterns
  -> parse_request()
  -> if WebSocket Upgrade matches proxy vhost: connect upstream, validate upstream 101, then tunnel client/upstream via same worker epoll reactor
  -> if WebSocket Upgrade does not match proxy vhost: local basic WebSocket echo state
  -> enforce per-IP request rate limit
  -> if modsecurity_enabled=true: inspect request with built-in ModSecurity-compatible OWASP CRS-inspired WAF subset
  -> Transaction
  -> VirtualHostHandlerFactory
  -> select static, reverse proxy, script-placeholder, or fallback static handler
  -> reverse proxy checks passive upstream circuit breaker before upstream connect when route is proxy
  -> ResponseSink
  -> serialize HTTP/1.1 response
  -> keep connection alive or close based on HTTP/1.1 connection policy
```

## Main Components

### Core

Location:

- `include/rimau/core/`
- `src/core/`

Responsibility:

- CLI config loading
- SQLite config bootstrap/read/update through bundled static SQLite
- TLS context setup through bundled OpenSSL, including SNI certificate contexts
- TLS context live swap for new connections on accepted SIGHUP reloads
- Source-built dependency sysroot setup for fully static Linux x86_64 server builds
- Static deployment ELF validation through CTest `rimau_static_elf_checks`
- Runtime server setup
- TCP listener lifecycle through per-worker non-blocking Linux `epoll` reactor
- Runtime security counters for global/per-IP connections and per-IP rate limits
- Basic logging
- Version metadata

Important files:

- `src/main.cpp`
- `src/core/server.cpp`
- `src/core/config.cpp`
- `src/core/logger.cpp`

### HTTP

Location:

- `include/rimau/http/`
- `src/http/`

Responsibility:

- Request representation
- HTTP/1.1 request header parsing and buffered message framing
- HTTP/1.1 file-backed request body accumulation for large Content-Length or chunked uploads before handler dispatch
- Response serialization
- Static file response
- Basic MIME detection
- Host-based virtual host routing
- Baseline HTTP reverse proxy
- Server-side runtime declaration placeholder that returns explicit `501`
- Built-in ModSecurity-compatible WAF inspection with OWASP CRS-inspired rule subset
- Request handler pipeline
- Transaction dispatch abstraction
- Protocol-independent response sink abstraction

Important files:

- `src/http/http1_session.cpp`
- `src/http/parser.cpp`
- `src/http/response.cpp`
- `src/http/response_builder.cpp`
- `src/http/static_file_handler.cpp`
- `src/http/transaction.cpp`
- `src/http/virtual_host.cpp`
- `src/http/waf.cpp`

### Protocol

Location:

- `include/rimau/protocol/`
- `src/protocol/`

Responsibility:

- Protocol status/gateway integration points
- Protocol capability reporting
- Protocol enable flags and support status reporting
- HTTP/2 wire frame codec, SETTINGS codec, HPACK baseline, and partial h2c/TLS ALPN `h2` request serving
- HTTP/3 QUIC varint, frame, and SETTINGS codec primitives
- Adapting protocol-specific wire I/O into the shared HTTP transaction pipeline

Important files:

- `src/protocol/http2_gateway.cpp`
- `src/protocol/http3_gateway.cpp`
- `src/protocol/protocol.cpp`

## Database Design

SQLite is currently used for runtime server configuration. The SQLite engine is built from bundled static SQLite 3.53.3 through CMake target `rimau_bundled_sqlite`; the server must not depend on system `libsqlite3`.

Default database path:

```text
data/rimau.sqlite3
```

Runtime table:

```sql
CREATE TABLE rimau_config (
  key TEXT PRIMARY KEY NOT NULL,
  value TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

Schema migration metadata table:

```sql
CREATE TABLE rimau_schema_migrations (
  version INTEGER PRIMARY KEY NOT NULL,
  name TEXT NOT NULL,
  applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

Current config schema version:

```text
1
```

Version `1` is `initial rimau_config schema`. Existing SQLite databases that already have `rimau_config` but no migration metadata are bootstrapped with version `1` on the next config load. If `rimau_schema_migrations` contains a version newer than the binary supports, Rimau rejects the database instead of trying to read it with an older config model.

TLS-related config values store file paths only. Certificate and private key files themselves are not stored inside SQLite.

Production certificate path, permission, reload, rotation, and rollback guidance is documented in `docs/plans/022-production-certificate-management.md`. OCSP stapling is not supported; ADR-0039 records the decision to defer it instead of adding placeholder config keys.

Supported keys:

- `host`
- `port`
- `document_root`
- `directory_index`
- `error_page`
- `max_request_bytes`
- `http_keep_alive_enabled`
- `http_keep_alive_timeout_seconds`
- `http_keep_alive_max_requests`
- `listen_backlog`
- `server_name`
- `http1_enabled`
- `http2_enabled`
- `http3_enabled`
- `tls_enabled`
- `tls_certificate_file`
- `tls_private_key_file`
- `worker_threads`
- `epoll_max_events`
- `reuse_port_enabled`
- `tcp_keepalive_enabled`
- `tcp_keepalive_idle_seconds`
- `tcp_keepalive_interval_seconds`
- `tcp_keepalive_probe_count`
- `graceful_shutdown_timeout_seconds`
- `connection_pool_size`
- `tls_min_version`
- `tls_max_version`
- `tls_cipher_list`
- `tls_ciphersuites`
- `tls_alpn_protocols`
- `tls_sni_hosts`
- `tls_sni_certificates`
- `tls_sni_required`
- `request_timeout_seconds`
- `header_timeout_seconds`
- `body_timeout_seconds`
- `idle_timeout_seconds`
- `global_connection_limit`
- `per_ip_connection_limit`
- `rate_limit_enabled`
- `rate_limit_requests_per_minute`
- `ip_allowlist`
- `ip_blocklist`
- `websocket_max_frame_bytes`
- `security_headers_enabled`
- `security_header_content_security_policy`
- `security_header_strict_transport_security`
- `security_header_x_content_type_options`
- `security_header_x_frame_options`
- `security_header_referrer_policy`
- `security_header_cross_origin_opener_policy`
- `server_header_enabled`
- `virtual_hosts_enabled`
- `virtual_hosts`
- `virtual_host_waf_overrides`
- `reverse_proxy_connect_timeout_seconds`
- `reverse_proxy_read_timeout_seconds`
- `reverse_proxy_max_response_bytes`
- `reverse_proxy_retry_count`
- `reverse_proxy_tls_verify_upstream`
- `reverse_proxy_circuit_breaker_enabled`
- `reverse_proxy_circuit_breaker_failure_threshold`
- `reverse_proxy_circuit_breaker_cooldown_seconds`
- `modsecurity_enabled`
- `modsecurity_owasp_crs_enabled`
- `modsecurity_blocking_enabled`
- `modsecurity_anomaly_threshold`
- `modsecurity_max_inspection_bytes`
- `modsecurity_audit_log_enabled`

The server bootstraps these tables and default rows when the SQLite database is missing. Runtime configuration is read from SQLite, not a key-value config file.

There are no ORM models. The migration system is currently a minimal version-history table, not a full multi-step migration framework.

Future admin API, cache metadata, metrics retention, downgrade policy, backup policy, or control plane storage may require additional tables and migration steps. Needs verification.

## Bundled Build Architecture

The default CMake path builds these bundled source dependencies:

- OpenSSL `openssl-4.0.1` static with `no-shared`, `no-dso`, `no-tests`, and `no-docs`.
- SQLite `3.53.3` amalgamation as `libsqlite3.a` with loadable extensions disabled.
- zlib `1.3.2` static as `libz.a`.
- GNU Bison `3.8.2` from GNU source as the glibc build-time parser generator.
- Linux kernel source `6.18.7` UAPI headers through `make ARCH=x86_64 headers_install`.
- GNU glibc `2.43` from GNU source into `build/_deps/glibc/sysroot`.

`rimau_bundled_bison`, `rimau_bundled_linux_headers`, and `rimau_bundled_glibc` are excluded from the default `all` target unless `rimau-server` depends on `rimau_bundled_glibc` through `RIMAU_FULLY_STATIC_SERVER=ON` and `RIMAU_USE_BUNDLED_GLIBC=ON`. This keeps the GitHub Actions fast path from building glibc when it intentionally configures `RIMAU_FULLY_STATIC_SERVER=OFF` and `RIMAU_USE_BUNDLED_GLIBC=OFF`.

For Linux x86_64 GNU/Clang builds, `rimau-server` links with:

```text
-static -B<glibc-sysroot>/usr/lib --sysroot=<glibc-sysroot>
```

Current validation shows `ldd build/rimau-server` reports `not a dynamic executable` and `readelf -l build/rimau-server` has no `INTERP` program header. The test binaries are build artifacts for validation and are not the deployment target.

CTest target `rimau_static_elf_checks` automates the deployable binary checks:

```bash
ctest --test-dir build --output-on-failure -R rimau_static_elf_checks
```

For default fully static builds it runs `ldd`, `file`, and `readelf -l` against `rimau-server`. For fast CI builds configured with `RIMAU_FULLY_STATIC_SERVER=OFF` or `RIMAU_USE_BUNDLED_GLIBC=OFF`, the test reports a CTest skip rather than failing a deliberately non-static binary.

Build-time still requires host compiler/tooling such as `gcc`/`g++`, `make`, `perl`, `gawk`, `msgfmt`, `python3`, and `m4`. These are build requirements, not runtime shared-library dependencies for the deployable `rimau-server`.

glibc is configured with `--without-selinux` so the bundled libc build does not absorb a host `libselinux` dependency. `makeinfo` is missing in the current machine, so glibc documentation/test features are disabled during configure; server build output still completed. Needs verification if full glibc documentation or glibc upstream test suites become release requirements.

Caveat: static glibc hostname resolution through `getaddrinfo` or OpenSSL `gethostbyname` can require matching glibc NSS shared modules at runtime. Rimau currently uses those calls in reverse proxy hostname paths. Reverse proxy to literal IP upstreams avoids that specific hostname-resolution caveat; production hostname behavior needs integration tests. Needs verification.

## API Routes

This project is a web server, not an application API server.

Current external HTTP behavior:

```text
GET     /path    Serve static file from document_root
HEAD    /path    Serve static file headers from document_root
OPTIONS /path    Return allowed HTTP methods and CORS-style method/header hints
POST    /path    Return JSON metadata/body scaffold response
PUT     /path    Return JSON metadata/body scaffold response
PATCH   /path    Return JSON metadata/body scaffold response
DELETE  /path    Return JSON metadata/body scaffold response
```

Default document root:

```text
public/
```

Default route:

```text
GET /
```

This maps to:

```text
public/index.html
```

Unsupported methods return:

```text
405 Method Not Allowed
Allow: GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS
```

GET/HEAD static file responses support URL-decoded paths, query parsing, MIME detection, single byte-range requests, and gzip compression when the client sends `Accept-Encoding: gzip`.

Virtual host behavior:

```text
Host: site.test         static vhost document root
Host: *.tenant.test     simple wildcard static vhost document root
Host: api.test          reverse proxy to configured http:// or https:// upstream pool
Host: app.test          script runtime declaration, returns 501 until bundled runtime exists
```

If no virtual host rule matches, the request falls back to global `document_root`.

WebSocket behavior:

```text
GET /path
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Version: 13
```

The current WebSocket implementation accepts RFC 6455 version 13 handshakes. For non-proxy hosts it provides basic two-way echo for text/binary frames plus ping/pong and close handling. For reverse proxy virtual hosts, Rimau forwards the upstream WebSocket handshake to the selected `http://` or `https://` upstream and then tunnels bytes in both directions through the same worker `epoll` reactor.

Application routing, rewrite rules, admin/control API, full WebSocket application routing/subprotocol policy, and real server-side runtime execution are planned but not implemented.

## Configuration

Configuration source:

```text
SQLite database
```

Supported keys:

- `host`
- `port`
- `document_root`
- `directory_index`
- `error_page`
- `max_request_bytes`
- `http_keep_alive_enabled`
- `http_keep_alive_timeout_seconds`
- `http_keep_alive_max_requests`
- `listen_backlog`
- `server_name`
- `http1_enabled`
- `http2_enabled`
- `http3_enabled`
- `tls_enabled`
- `tls_certificate_file`
- `tls_private_key_file`
- `worker_threads`
- `epoll_max_events`
- `reuse_port_enabled`
- `tcp_keepalive_enabled`
- `tcp_keepalive_idle_seconds`
- `tcp_keepalive_interval_seconds`
- `tcp_keepalive_probe_count`
- `graceful_shutdown_timeout_seconds`
- `connection_pool_size`
- `tls_min_version`
- `tls_max_version`
- `tls_cipher_list`
- `tls_ciphersuites`
- `tls_alpn_protocols`
- `tls_sni_hosts`
- `tls_sni_certificates`
- `tls_sni_required`
- `request_timeout_seconds`
- `header_timeout_seconds`
- `body_timeout_seconds`
- `idle_timeout_seconds`
- `global_connection_limit`
- `per_ip_connection_limit`
- `rate_limit_enabled`
- `rate_limit_requests_per_minute`
- `ip_allowlist`
- `ip_blocklist`
- `websocket_max_frame_bytes`
- `security_headers_enabled`
- `security_header_content_security_policy`
- `security_header_strict_transport_security`
- `security_header_x_content_type_options`
- `security_header_x_frame_options`
- `security_header_referrer_policy`
- `security_header_cross_origin_opener_policy`
- `server_header_enabled`
- `virtual_hosts_enabled`
- `virtual_hosts`
- `virtual_host_waf_overrides`
- `reverse_proxy_connect_timeout_seconds`
- `reverse_proxy_read_timeout_seconds`
- `reverse_proxy_max_response_bytes`
- `reverse_proxy_retry_count`
- `reverse_proxy_tls_verify_upstream`
- `reverse_proxy_circuit_breaker_enabled`
- `reverse_proxy_circuit_breaker_failure_threshold`
- `reverse_proxy_circuit_breaker_cooldown_seconds`
- `modsecurity_enabled`
- `modsecurity_owasp_crs_enabled`
- `modsecurity_blocking_enabled`
- `modsecurity_anomaly_threshold`
- `modsecurity_max_inspection_bytes`
- `modsecurity_audit_log_enabled`

Default database:

```text
data/rimau.sqlite3
```

The server creates the database and inserts default config rows if the database does not exist. Use `--set key=value` to update SQLite config.

Examples:

```bash
./build/rimau-server --database data/rimau.sqlite3 --set port=18080
./build/rimau-server --database data/rimau.sqlite3 --set http1_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set http2_enabled=false
./build/rimau-server --database data/rimau.sqlite3 --set http3_enabled=false
./build/rimau-server --database data/rimau.sqlite3 --set http_keep_alive_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set http_keep_alive_timeout_seconds=15
./build/rimau-server --database data/rimau.sqlite3 --set http_keep_alive_max_requests=100
./build/rimau-server --database data/rimau.sqlite3 --set worker_threads=0
./build/rimau-server --database data/rimau.sqlite3 --set reuse_port_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set tcp_keepalive_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set tls_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set tls_certificate_file=certs/rimau-dev.crt
./build/rimau-server --database data/rimau.sqlite3 --set tls_private_key_file=certs/rimau-dev.key
./build/rimau-server --database data/rimau.sqlite3 --set tls_min_version=TLSv1.2
./build/rimau-server --database data/rimau.sqlite3 --set tls_max_version=TLSv1.3
./build/rimau-server --database data/rimau.sqlite3 --set tls_alpn_protocols=http/1.1
./build/rimau-server --database data/rimau.sqlite3 --set "tls_sni_certificates=api.example.test=certs/api.crt|certs/api.key;*.tenant.test=certs/tenant.crt|certs/tenant.key"
./build/rimau-server --database data/rimau.sqlite3 --set request_timeout_seconds=30
./build/rimau-server --database data/rimau.sqlite3 --set header_timeout_seconds=10
./build/rimau-server --database data/rimau.sqlite3 --set body_timeout_seconds=30
./build/rimau-server --database data/rimau.sqlite3 --set global_connection_limit=10000
./build/rimau-server --database data/rimau.sqlite3 --set per_ip_connection_limit=100
./build/rimau-server --database data/rimau.sqlite3 --set rate_limit_requests_per_minute=600
./build/rimau-server --database data/rimau.sqlite3 --set ip_allowlist=127.0.0.1,::1,10.0.0.0/8,2001:db8::/32
./build/rimau-server --database data/rimau.sqlite3 --set ip_blocklist=192.0.2.1,2001:db8:ffff::/48
./build/rimau-server --database data/rimau.sqlite3 --set server_header_enabled=false
./build/rimau-server --database data/rimau.sqlite3 --set "security_header_content_security_policy=default-src 'self'; frame-ancestors 'self'; base-uri 'self'"
./build/rimau-server --database data/rimau.sqlite3 --set security_header_x_frame_options=SAMEORIGIN
./build/rimau-server --database data/rimau.sqlite3 --set virtual_hosts_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set "virtual_hosts=site.test=static:public/site;api.test=proxy:http://127.0.0.1:19090,https://127.0.0.1:19443;app.test=script:php:public/app"
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_connect_timeout_seconds=5
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_read_timeout_seconds=30
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_max_response_bytes=1048576
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_retry_count=1
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_tls_verify_upstream=false
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_circuit_breaker_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_circuit_breaker_failure_threshold=3
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_circuit_breaker_cooldown_seconds=10
./build/rimau-server --database data/rimau.sqlite3 --set modsecurity_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set modsecurity_owasp_crs_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set modsecurity_blocking_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set modsecurity_anomaly_threshold=5
./build/rimau-server --database data/rimau.sqlite3 --set modsecurity_max_inspection_bytes=131072
./build/rimau-server --database data/rimau.sqlite3 --set modsecurity_audit_log_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set "virtual_host_waf_overrides=site.test=enabled:false;api.test=threshold:10,rule_exceptions:930100|942100"
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --database data/rimau.sqlite3
```

`tls_sni_certificates` uses semicolon-separated entries in this format:

```text
hostname-pattern=certificate.pem|private-key.pem
```

The default `tls_certificate_file` and `tls_private_key_file` remain the fallback certificate. `tls_sni_hosts` remains the optional SNI allowlist; configured SNI certificate patterns are also accepted by the SNI callback.

`virtual_hosts` uses semicolon-separated entries:

```text
host=static:document-root;host=proxy:http://upstream:port/base,https://backup:port/base;host=script:runtime:script-root
```

`virtual_host_waf_overrides` uses semicolon-separated entries with comma-separated options:

```text
site.test=enabled:false;api.test=threshold:10,rule_exceptions:930100|942100;*.tenant.test=blocking:false
```

When `modsecurity_audit_log_enabled=true`, WAF matches emit a structured `rimau_waf_audit` JSON payload through the existing stderr logger. The event records outcome, worker, client IP, engine, ruleset, first rule id, severity, tag, score, threshold, blocking mode, match count, method, host, path, and variable name. It intentionally does not log request body, query string, header values, cookies, authorization values, or WAF evidence. String values are JSON-escaped and bounded.

Rimau does not yet own a dedicated audit file, retention engine, or rotation engine. Production deployments should route stderr through systemd-journald, a container log driver, or supervisor-managed files with logrotate. Audit logs should be readable only by the service owner/security operators. A 7-30 day retention window is a reasonable starting policy until the operator defines a site-specific retention requirement. Needs verification.

SIGHUP reloads SQLite config values that can safely change without recreating listener sockets or worker threads. Dynamic values include `document_root`, `directory_index`, `error_page`, `max_request_bytes`, HTTP keep-alive settings, TCP keepalive settings for newly accepted sockets, protocol status flags, graceful shutdown timeout, request/header/body/idle timeout, rate-limit settings, IPv4/IPv6 IP allow/block lists, security header behavior/values, virtual host and reverse proxy settings, WAF settings, and TLS certificate/key/SNI/TLS settings for new TLS connections. Changes to bind address, port, listener backlog, worker count, epoll batch size, `SO_REUSEPORT`, connection pool sizing, HTTP/1 enablement, or `tls_enabled` require restart.

For TLS certificate operations, use `--check-config` before SIGHUP and follow `docs/plans/022-production-certificate-management.md` for rotation and rollback. Existing TLS connections keep their negotiated context after reload; new TLS connections use the reloaded context.

Production config ownership, backup, and migration behavior are not final. Needs verification.

## Protocol Architecture

`rimau::protocol::protocol_capabilities(config)` reports each protocol with:

- implementation status
- SQLite configured state
- expected transport
- current notes/limitations

Current rule:

- HTTP/1.1 is implemented and enabled by default.
- HTTP/2 can be toggled in SQLite for partial cleartext h2c request serving and partial TLS ALPN `h2` request serving, but full HPACK, continuation assembly, and production flow-control/session behavior are not implemented yet.
- HTTP/3 can be toggled in SQLite for status visibility, but HTTP/3 network serving is not implemented yet.
- If HTTP/1.1 is disabled, startup is allowed for cleartext h2c partial serving when `http2_enabled=true` and `tls_enabled=false`, or for TLS ALPN `h2` partial serving when `http2_enabled=true`, `tls_enabled=true`, and `tls_alpn_protocols` includes `h2`; otherwise startup fails because no client-served protocol remains enabled.
- ALPN advertises `http/1.1` when HTTP/1.1 is enabled and can advertise `h2` when HTTP/2 is enabled. ALPN `h3` is intentionally not advertised until HTTP/3 live serving exists.

## Proxygen-Inspired Pipeline

Rimau does not vendor or copy Proxygen. The local implementation uses small project-native abstractions:

- `RequestHandler`: handles one HTTP request.
- `RequestHandlerFactory`: creates a handler for a request.
- `Transaction`: owns one request-response lifecycle and dispatches to a handler.
- `ResponseSink`: downstream interface used by handlers to send normal or chunked responses.
- `ResponseBuilder`: helper for constructing normal or chunked response objects before sending.

Current HTTP/1.1 connection flow:

```text
Linux epoll reactor worker
  -> ClientConnection state machine
  -> optional non-blocking TLS handshake/read/write
  -> frame HTTP/1.1 headers and body boundary
  -> if body is incomplete, accumulate body chunks into memory up to 16 KiB then mkstemp-backed temporary file
  -> parse raw HTTP/1.1 bytes into Request, or attach spooled RequestBodyFile for large bodies
  -> built-in WAF inspection when enabled
  -> create Transaction
  -> VirtualHostHandlerFactory selects static, reverse proxy, script-placeholder, or fallback static handler
  -> handler can read request body chunks through RequestBodyReader and sends normal Response or chunked Response through buffered ResponseSink
  -> non-blocking response write
```

Future target:

```text
HTTP/1.1 codec/session
HTTP/2 codec/session
HTTP/3 codec/session
  -> shared WAF inspection when enabled
  -> shared Transaction
  -> shared RequestHandler pipeline
  -> protocol-specific ResponseSink
```

This keeps static serving and future routing/reverse-proxy logic independent from the wire protocol.

### HTTP/1.1

Current:

- Request header parser for HTTP/1.0 and HTTP/1.1.
- Static GET and HEAD.
- POST, PUT, PATCH, DELETE, and OPTIONS scaffold behavior.
- Content-Length request body parsing.
- Chunked transfer decoding for request bodies.
- File-backed request body accumulation for large Content-Length or chunked bodies; `Request::body_size()` reports total body size, `Request::body_spooled_to_file()` reports whether a temporary spool file is used, `Request::body_text(max_bytes)` reads bounded text, and `Request::open_body_reader()` returns a pull reader for chunked handler reads.
- URL decoding and query parameter parsing.
- JSON request detection and JSON response generation for method scaffold responses.
- Single-range and multipart static file responses with `206 Partial Content`, per-part `Content-Range`, strong ETag/Last-Modified validators, and `If-Range` handling.
- SQLite-configurable `directory_index` and optional custom `error_page` for static file responses.
- MIME type detection for common text, image, video, wasm, and pdf extensions.
- Gzip static response compression through bundled static zlib 1.3.2 when requested by the client.
- Basic chunked response API through `ResponseSink::send_chunked`, `ResponseBuilder::send_chunked`, and `Response::to_http_chunked_string`; HTTP/1.1 serialization emits `Transfer-Encoding: chunked` and omits `Content-Length`.
- Basic WebSocket upgrade and two-way echo.
- WebSocket reverse proxy tunnel for reverse proxy virtual hosts with `http://` or `https://` upstreams.
- Request-smuggling protection for duplicate/invalid `Content-Length`, `Content-Length` plus `Transfer-Encoding`, duplicate/unsupported `Transfer-Encoding`, obs-fold, and bare CR/LF.
- Header injection and response-splitting mitigation.
- Request, header, body, and idle timeouts.
- Slowloris mitigation through header/body timeouts.
- Global connection limit and per-IP connection limit.
- Per-IP fixed-window request rate limiting.
- IPv4/IPv6 exact/CIDR allowlist and blocklist.
- Configurable WebSocket max frame size.
- SQLite-configurable default security header values and configurable `Server` header emission.
- Host-based virtual host routing with exact host and simple `*.domain` wildcard matching.
- Static virtual hosts with host-specific document roots.
- Baseline reverse proxy virtual hosts for HTTP and HTTPS upstream targets.
- Multiple reverse proxy upstreams per vhost with basic round-robin selection and retry/failover.
- Process-local passive reverse proxy circuit breaker with SQLite-configured threshold and cooldown.
- Script virtual host declarations such as `script:php:path`; execution is not implemented and returns `501`.
- HTTP/1.1 keep-alive.
- Configurable keep-alive idle timeout through `http_keep_alive_timeout_seconds`.
- Configurable per-connection request cap through `http_keep_alive_max_requests`.
- Basic request pipelining: pipelined bytes remain buffered and the next request is processed after the previous response is written.
- Linux `epoll` reactor pattern.
- Worker pool uses `std::jthread`; `worker_threads=0` means CPU core auto-detection.
- Each worker owns its own event loop and listener socket.
- `SO_REUSEADDR` and `SO_REUSEPORT` are enabled for listener sockets by default.
- TCP keepalive is enabled on accepted sockets by default.
- Connection objects are recycled through a per-worker pool; request/response buffers retain capacity between uses.
- SIGTERM and SIGINT trigger graceful shutdown; workers stop accepting and drain active connections up to `graceful_shutdown_timeout_seconds`.
- SIGHUP performs limited live reload for SQLite config values that do not require listener/worker recreation.
- Non-blocking TLS handshake/read/write when SQLite config `tls_enabled=true`.
- TLS 1.2 and TLS 1.3 are supported through bundled OpenSSL.
- TLS cipher list/ciphersuites are SQLite-configurable.
- SNI validates configured hostnames and can select alternate certificate contexts through `tls_sni_certificates`.
- Automated CTest `rimau_tls_sni_cert_selection` verifies default fallback, exact-host, and simple wildcard SNI certificate selection using bundled OpenSSL fingerprints.
- ALPN selects `http/1.1` or `h2` based on `tls_alpn_protocols` and enabled protocol flags.
- TLS certificate/key/settings reload on SIGHUP applies to new connections.
- Requests are dispatched through `Transaction` and `RequestHandler`.
- Optional HTTPS when SQLite config `tls_enabled=true`.
- TLS uses bundled static OpenSSL 4.0.1 built by CMake.
- Enabled by SQLite config key `http1_enabled=true`.

Missing:

- Live in-flight request body streaming before handler dispatch and full network backpressure contract. Current code accumulates large HTTP/1.1 bodies into temporary files before dispatch, then exposes a pull-style `RequestBodyReader` to handlers.
- Producer-side async response streaming/backpressure. Current basic chunked responses are chunk-encoded before socket write rather than produced incrementally by the event loop.
- Brotli compression.
- Full WebSocket fragmentation/extensions/subprotocol support.
- Full request pipelining stress validation.
- Arbitrary custom security header names.
- Distributed or shared rate limiting across processes.
- Reverse proxy HTTP request/response streaming, per-upstream HTTPS CA/pinning/verify-depth policy, advanced load balancing, active health checks, upstream connection pooling, and a fully async upstream state machine for normal HTTP proxy traffic.
- Bundled PHP/Python/Perl or other server-side runtime execution.

### HTTP/2

Current:

- SQLite config key `http2_enabled`.
- Gateway status reporting through `http2_status(config)`.
- Protocol capability reporting shows `partial` status for wire codec work and configured state separately from full request-serving implementation state.
- HTTP/2 client connection preface detection for cleartext h2c prior-knowledge clients and TLS clients after ALPN `h2` negotiation.
- HTTP/2 frame parser/serializer for the 9-byte header, frame length, type, flags, stream id, and payload.
- HTTP/2 SETTINGS payload parser/serializer.
- Basic HTTP/2 frame validation for SETTINGS, PING, GOAWAY, WINDOW_UPDATE, RST_STREAM, DATA, HEADERS, and CONTINUATION.
- HPACK baseline for static-table indexed fields, literal fields without Huffman, and literal incremental decode without dynamic-table persistence.
- When `http2_enabled=true`, Rimau accepts h2c or TLS ALPN `h2` client preface + SETTINGS, replies SETTINGS + SETTINGS ACK, handles SETTINGS ACK/PING/RST_STREAM/WINDOW_UPDATE basics, decodes HEADERS/DATA for complete streams, dispatches to `Transaction`, and writes HTTP/2 response HEADERS/DATA frames.
- Automated CTest `rimau_tls_alpn_h2_curl` starts a temporary TLS Rimau server and uses `curl --http2` as a real HTTP/2 client to verify ALPN selects `h2`.

Required future work:

- Upgrade the current curl ALPN smoke to require successful HTTP/2 responses after HPACK Huffman/dynamic-table behavior is implemented.
- Dedicated HTTP/2 connection/session module instead of the current `ClientConnection` inline partial path.
- Full HPACK header compression, including Huffman and dynamic table persistence.
- Complete stream lifecycle and broad multiplexing behavior.
- CONTINUATION frame assembly.
- Flow control.
- Priority handling. Needs verification for current RFC expectations.
- Integration tests with a real HTTP/2 client.

Candidate library:

- `nghttp2`. Needs verification.

### HTTP/3

Current:

- SQLite config key `http3_enabled`.
- Gateway status reporting through `http3_status(config)`.
- Protocol capability reporting shows `partial` status for codec primitives and configured state separately from live implementation state.
- QUIC variable-length integer parser/serializer.
- HTTP/3 frame parser/serializer.
- HTTP/3 SETTINGS payload parser/serializer.
- No live UDP/QUIC listener yet.

Required future work:

- QUIC transport.
- TLS 1.3.
- ALPN.
- HTTP/3 request adapter into `Transaction`.
- QPACK.
- UDP listener.
- Connection migration behavior. Needs verification.
- Integration tests with a real HTTP/3 client.

Candidate libraries:

- `ngtcp2` + `nghttp3`. Needs verification.
- `quiche`. Needs verification.

## Deployment Architecture

Local development:

```bash
cmake -S . -B build
cmake --build build
./build/rimau-server --database data/rimau.sqlite3
```

Local HTTPS development:

```bash
make start-https
curl -k -I https://127.0.0.1:18443/
make stop-https
```

Production deployment is not designed yet.

Needs verification:

- Linux distro targets.
- Package format.
- systemd unit.
- Container image.
- TLS certificate path and reload behavior.
- User/group privilege model.
- Log directory and rotation.
- SQLite database directory.
- Static file root convention.

## Security Notes

Implemented:

- Basic path traversal protection with canonical path checks.
- Static file symlink escape mitigation through canonical path checks.
- Runtime SQLite databases are ignored by `.gitignore`.
- Request header size limit.
- TLS for HTTP/1.1 and partial HTTP/2 ALPN `h2` through bundled static OpenSSL.
- Runtime config database access through bundled static SQLite.
- Gzip compression through bundled static zlib.
- `rimau-server` static-links `libstdc++` and `libgcc` on Linux GNU/Clang builds when supported.
- `rimau-server` links as a fully static Linux x86_64 ELF through bundled glibc 2.43 sysroot in the current validation.
- TLS 1.2 and TLS 1.3.
- SNI validation and multi-certificate SNI selection.
- Automated SNI certificate selection coverage for default fallback, exact host, and simple wildcard host patterns.
- ALPN `http/1.1` and partial `h2`.
- TLS certificate/key/settings reload for new connections on SIGHUP.
- Request/header/body/idle timeout enforcement.
- Global and per-IP connection limits.
- Per-IP fixed-window rate limiting.
- IPv4/IPv6 exact/CIDR allowlist and blocklist.
- Basic request-smuggling protection.
- Header injection and response splitting mitigation.
- Request body spool files are created with `mkstemp` and removed by `RequestBodyFile` RAII cleanup when the request object is released.
- SQLite-configurable default security headers and configurable `Server` header.
- WebSocket frame size limit.
- Built-in ModSecurity-compatible WAF can block requests by anomaly threshold before HTTP handler dispatch. Current built-in rules cover scanner user agents, encoded CRLF/request splitting, path traversal, XSS, SQLi, RCE, PHP wrapper injection, and Java/JNDI exploit patterns.
- Reverse proxy upstream response size limit.
- Reverse proxy HTTPS upstream transport through bundled OpenSSL.
- WebSocket proxy upstream handshake validation and sanitized `101 Switching Protocols` response serialization.
- Passive reverse proxy circuit breaker returns `503 Service Unavailable` with `Retry-After` when every selected upstream circuit is open.
- Generated dev certificates and private keys are ignored by `.gitignore`.

Missing:

- Production certificate management.
- ALPN `h3`.
- Distributed rate limiting.
- Sandboxing.
- Privilege dropping.
- Full parser hardening beyond the deterministic `rimau_http_fuzz` CTest smoke.
- Continuous or sanitizer-backed fuzz testing beyond the deterministic CTest smoke.
- Full real-client HTTP/2 request success with curl/nghttp2; the current curl test verifies ALPN `h2` selection but accepts the known HPACK Huffman `COMPRESSION_ERROR` request path.
- Full `libmodsecurity` transaction engine integration and full OWASP Core Rule Set bundle are deferred beyond P1 by ADR-0034. Needs verification.
- Rich WAF rule tuning beyond ADR-0035, dedicated WAF audit log persistence beyond ADR-0036, and ModSecurity rule syntax parsing.
- Advanced slow-client scoring.
- Safe default production file permissions.
- Reverse proxy request/response policy hardening beyond current hop-by-hop header stripping, buffered-size limit, WebSocket proxy handshake validation, passive circuit breaker, basic retry, and TLS transport.
- Production validation for static glibc DNS/NSS hostname behavior in reverse proxy paths. Needs verification.

## Performance Notes

Current implementation is intentionally simple and not yet comparable to nginx/OpenLiteSpeed.

Major performance work still required:

- Benchmark the Linux `epoll` backend and decide whether `io_uring` or another abstraction is needed. Needs verification.
- Add sendfile or zero-copy static file path.
- Move normal HTTP reverse proxy upstream connections into the worker reactor or an async upstream state machine; current normal HTTP proxy path uses non-blocking upstream sockets but waits with `poll()` inside the handler and uses blocking DNS resolution through `getaddrinfo`. WebSocket proxy data tunneling is registered with the worker `epoll` reactor after the upstream handshake, but DNS/connect/handshake are still completed before tunnel mode starts.
- Add deeper memory pools for response buffers and file I/O.
- Add benchmark suite.
- Add memory allocator and buffer management strategy. Needs verification.
