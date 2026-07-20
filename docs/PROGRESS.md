# Progress

Last updated: 2026-07-20

## Current Status

Scaffold awal projek sudah dibuat.

Implemented:

- CMake project.
- Root test wrappers: `make test` and `./scripts/test.sh`.
- Root dev server wrappers: `make serve` and `./scripts/serve-dev.sh`.
- `rimau-server` CLI.
- Project name exposed as `Rimau Web Server` in CLI/version and HTTP `server` header.
- SQLite-backed config loader.
- SQLite config schema metadata table `rimau_schema_migrations` with current version `1`.
- Future-version guard for SQLite config databases that are newer than the running binary.
- SQLite config update command `--set key=value`.
- Bundled static SQLite 3.53.3 build through CMake.
- Bundled static OpenSSL 4.0.1 build through CMake.
- Bundled static zlib 1.3.2 build through CMake.
- Bundled GNU Bison 3.8.2 build through CMake for glibc.
- Bundled Linux UAPI headers from Linux kernel source 6.18.7 through CMake.
- Bundled GNU glibc 2.43 source build through CMake for Linux x86_64.
- Fully static `rimau-server` linking through bundled glibc sysroot on current Linux x86_64 GNU/Clang validation.
- HTTP/1.1 HTTPS/TLS support through bundled OpenSSL.
- TLS 1.2/TLS 1.3 range configuration through SQLite.
- TLS cipher list and TLS 1.3 ciphersuites configuration through SQLite.
- SNI hostname validation and multi-certificate SNI selection.
- ALPN callback for `http/1.1` and partial `h2` when HTTP/2 is enabled.
- SIGHUP TLS certificate/key/settings reload for new connections.
- Dev certificate generation with bundled OpenSSL binary.
- HTTP/1.1 static file GET/HEAD.
- HTTP/1.1 method support for GET, POST, PUT, PATCH, DELETE, OPTIONS, and HEAD.
- Content-Length request body parsing.
- Chunked request body decoding.
- URL decoding and query parameter parsing.
- JSON request detection and JSON scaffold responses for methods with bodies.
- Single-range static file responses.
- Gzip static response compression through bundled static zlib.
- Basic WebSocket upgrade and two-way echo.
- WebSocket reverse proxy tunneling for proxy virtual hosts with `http://` and `https://` upstreams.
- Request/header/body/idle timeout controls.
- Slowloris mitigation through header/body timeout.
- Request smuggling checks for invalid/duplicate `Content-Length`, `Content-Length` plus `Transfer-Encoding`, duplicate/unsupported `Transfer-Encoding`, obs-fold, and bare CR/LF.
- Header injection and response splitting mitigation.
- Static file symlink escape mitigation through canonical path checks.
- Global and per-IP connection limits.
- Per-IP fixed-window request rate limiting.
- SQLite-configurable built-in ModSecurity-compatible WAF with OWASP CRS-inspired subset rules.
- IPv4/IPv6 exact/CIDR allowlist and blocklist.
- Configurable WebSocket max frame size.
- SQLite-configurable default security header values and configurable `Server` header emission.
- SQLite-configured virtual host routing with exact host and simple `*.domain` wildcard matching.
- Static virtual hosts with per-host document roots.
- Baseline reverse proxy virtual hosts for upstream `http://` and `https://`.
- Reverse proxy multiple upstream targets per vhost with basic round-robin selection.
- Reverse proxy retry/failover through SQLite `reverse_proxy_retry_count`.
- Optional HTTPS upstream certificate verification through SQLite `reverse_proxy_tls_verify_upstream`.
- Passive reverse proxy circuit breaker through SQLite threshold/cooldown keys.
- WAF blocking for HTTP/1.1, WebSocket upgrade, WebSocket reverse proxy upgrade, and partial HTTP/2 request dispatch.
- Script virtual host declarations such as `script:php:path`; execution returns explicit `501 Not Implemented`.
- Linux `epoll` reactor per worker thread for the main server runtime.
- Non-blocking listener and client socket I/O.
- Non-blocking OpenSSL TLS handshake/read/write/shutdown state handling.
- Worker pool based on CPU core count when `worker_threads=0`.
- Per-worker `SO_REUSEPORT` listeners.
- TCP keepalive on accepted client sockets.
- Per-worker connection object pool for connection/buffer reuse.
- HTTP/1.1 persistent connections, keep-alive, and basic request pipelining.
- HTTP/1.1 buffered request framing extracted from `ClientConnection` into `rimau::http::next_http1_request_frame`.
- SIGTERM/SIGINT graceful shutdown and limited SIGHUP live reload.
- SQLite protocol flags for `http1_enabled`, `http2_enabled`, and `http3_enabled`.
- Config-aware protocol capability reporting.
- HTTP/2 frame parser/serializer and SETTINGS payload codec.
- HTTP/2 HPACK baseline for static-table indexed fields, literal fields without Huffman, and literal incremental decode without dynamic-table persistence.
- Partial HTTP/2 h2c prior-knowledge request serving: client preface + SETTINGS in, SETTINGS + SETTINGS ACK out, HEADERS/DATA request parsing, shared handler pipeline dispatch, and HTTP/2 HEADERS/DATA responses.
- Partial HTTP/2 TLS ALPN `h2` request serving using the same preface, SETTINGS, HEADERS/DATA, shared handler pipeline, and HTTP/2 response serialization path.
- HTTP/3 QUIC varint parser/serializer.
- HTTP/3 frame parser/serializer and SETTINGS payload codec.
- WAF unit test melalui CTest.
- Basic HTTP request parser.
- Basic HTTP response serializer.
- Response serializer support for caller-controlled `Connection` headers.
- Proxygen-inspired request handler pipeline.
- `RequestHandler`, `RequestHandlerFactory`, `Transaction`, `ResponseSink`, `ResponseBuilder`, and `StaticFileHandler`.
- Static document root `public/`.
- HTTP/1.1 network integration CTest for keep-alive, max request cap, idle timeout, pipelining, chunked body, range, gzip, WebSocket echo, and WebSocket proxy.
- Parser unit test melalui CTest.
- Handler pipeline unit test melalui CTest.
- Response serializer unit test melalui CTest.
- HTTP/1.1 session/framing unit test melalui CTest.
- SQLite config database unit test melalui CTest.
- CLI integration test melalui CTest untuk `--database`, `--set`, `--check-config`, dan `--protocols`.
- Protocol capability unit test melalui CTest.
- Project-memory docs.

Partial:

- Event-loop performance architecture uses Linux `epoll`; benchmark results and possible `io_uring` direction need verification.
- Logging hanya ke stderr.
- Path traversal protection asas.
- MIME detection by common file extension.
- HTTP/1.1 support is partial; streaming request bodies, multipart ranges, Brotli, full WebSocket application routing, and broad stress validation are not implemented.
- Proxygen-inspired pipeline is partial; filter chain, async body streaming, and full protocol session adapters are not implemented.
- SQLite config is partial; schema version table exists, but multi-step migration framework, admin UI, and config backup policy are not implemented. SIGHUP live reload is implemented only for restart-free dynamic values.
- TLS is partial; production certificate lifecycle and OCSP are not implemented. HTTP/2 ALPN `h2` request serving exists only as a partial basic path, not production-complete HTTP/2.
- Virtual hosts are partial; exact host, simple wildcard, static roots, proxy rules, and script declarations exist, but rewrite rules and richer routing are not implemented.
- Reverse proxy is partial; HTTP/HTTPS upstream, optional default-trust-path certificate verification, buffered normal HTTP response, WebSocket tunnel data path through worker `epoll`, basic hop-by-hop header stripping, basic round-robin, retry/failover, and passive circuit breaker exist. Per-upstream HTTPS verification policy, normal HTTP proxy streaming, advanced load balancing, active health checks, and upstream connection pooling are not implemented.
- ModSecurity/WAF is partial; a built-in ModSecurity-compatible anomaly-scoring WAF with OWASP CRS-inspired subset rules exists, but full `libmodsecurity` and full OWASP Core Rule Set are not bundled or implemented.
- HTTP/2 and HTTP/3 support is partial: tested wire codec primitives exist, HTTP/2 has cleartext h2c and TLS ALPN `h2` request serving basics, and HTTP/3 remains wire-codec-only.
- Server-side language support is planned only; PHP/Python/Perl runtime execution is not bundled or implemented.
- Static glibc DNS/NSS hostname behavior for reverse proxy upstream hostnames still needs production validation because the static linker warns on `getaddrinfo` and OpenSSL `gethostbyname`. Needs verification.

Planned:

- HTTP/2 production-complete request serving, including full stream lifecycle, HPACK, continuation assembly, automated real-client testing, and flow control.
- HTTP/3 full UDP/QUIC request serving.
- HTTP/3 ALPN negotiation after HTTP/3 live serving is implemented.
- Bundled server-side runtime execution. Needs verification.
- Production deployment.
- Benchmark suite.

## Repository Inspection Notes

Semasa pemeriksaan awal pada 2026-07-18:

- Direktori repo kosong.
- Direktori belum menjadi Git repository.
- Tiada Git history untuk dianalisis.
- Tiada package files, tests, database files, deployment files, atau docs lama ditemui.

## Feature Status

| Area | Status | Notes |
| --- | --- | --- |
| Build system | Implemented | CMake with bundled static OpenSSL, SQLite, zlib, Bison, Linux UAPI headers, and glibc targets; `rimau-server` is fully static in current Linux x86_64 validation. |
| CLI | Partial | `--database`, `--set`, `--check-config`, `--protocols`, `--version`, `--help`. |
| HTTP/1.1 | Partial | GET/HEAD static, POST/PUT/PATCH/DELETE JSON scaffold, OPTIONS, Content-Length, chunked decode, keep-alive, basic pipelining, range, gzip, WebSocket echo, WebSocket reverse proxy tunnel, and extracted testable request framing. |
| HTTP/2 | Partial | Frame codec, SETTINGS/PING basics, HPACK baseline, cleartext h2c and TLS ALPN `h2` HEADERS/DATA request serving through the shared handler pipeline exist; no production flow control, CONTINUATION assembly, HPACK Huffman, or dynamic-table persistence yet. |
| HTTP/3 | Partial | QUIC varint, frame codec, and SETTINGS codec exist; no UDP/QUIC/QPACK/live serving yet. |
| TLS | Partial | HTTP/1.1 HTTPS and partial HTTP/2 ALPN `h2` via bundled static OpenSSL 4.0.1 with TLS 1.2/1.3, SNI validation, multi-certificate SNI selection, ALPN `http/1.1`/`h2`, safe cipher config, and SIGHUP context reload for new connections. |
| Static files | Partial | Serves from `document_root`. |
| Virtual hosts | Partial | Exact host and simple `*.domain` matching, static vhost, proxy vhost, and script declarations. |
| Reverse proxy | Partial | HTTP/HTTPS upstream, optional default-trust-path certificate verification, buffered normal HTTP response, WebSocket tunnel data path in worker `epoll`, basic round-robin, retry/failover, and passive circuit breaker; no per-upstream CA/pinning policy, active health checks, normal HTTP proxy streaming, or upstream connection pooling yet. |
| Server-side runtimes | Planned | `script:runtime:path` config returns `501`; PHP/Python/Perl are not bundled or executed yet. |
| Request pipeline | Partial | Handler/factory/transaction/response sink implemented for static, vhost, reverse proxy, and script-placeholder HTTP/1.1. |
| Config | Partial | SQLite table `rimau_config`, schema metadata table `rimau_schema_migrations`, protocol, TLS, keep-alive, timeout, rate-limit, IP-list, security-header, virtual-host/proxy keys, and limited SIGHUP reload behavior. |
| Security | Partial | Baseline HTTP framing hardening, timeout, rate limit, connection limit, built-in ModSecurity-compatible WAF subset, configurable security header values, and IPv4/IPv6 IP allow/block list are implemented; fuzzing, full ModSecurity/CRS integration, and production hardening remain pending. |
| Tests | Partial | Parser, HTTP/1.1 session/framing, HTTP/1.1 network integration, response serializer, handler pipeline, SQLite config, CLI config, protocol capability, HTTP/2 wire, HTTP/3 wire, virtual host, and WAF tests. |
| Deployment | Planned | No production deployment files. |
| Database | Partial | SQLite is used for runtime configuration only; the SQLite engine is bundled static and config schema version `1` is recorded. |
| I/O model | Partial | Linux `epoll` reactor with per-worker event loops and SO_REUSEPORT implemented; benchmarks still pending. |

## Last Validation

Completed on 2026-07-18:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
```

Result:

- CMake configure: passed with GNU C++ 13.3.0.
- Build: passed.
- Historical CTest result before protocol capability update: passed, 3/3 tests.
- Current CTest count after protocol capability update: passed, 4/4 tests.
- Default config validation: passed.
- SQLite config validation: passed.
- Protocol status command: passed.

Manual smoke test:

```bash
./build/rimau-server --database <ephemeral sqlite db on port 18080>
curl -i --max-time 5 http://127.0.0.1:18080/
```

Result:

- HTTP/1.1 response returned `200 OK`.
- `public/index.html` served successfully.
- Default/example port 8080 was already in use in the local environment, so the smoke test used port 18080 instead.

Additional validation after Proxygen-inspired pipeline refactor:

```bash
./build/rimau-server --version
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database <ephemeral sqlite db on port 18080>
curl -i --max-time 5 http://127.0.0.1:18080/
curl -I --max-time 5 http://127.0.0.1:18080/
```

Result:

- Version output: `Rimau Web Server 0.1.0`.
- GET `/`: returned `200 OK`.
- HEAD `/`: returned `200 OK` with no body.
- HTTP `server` header now reports `Rimau Web Server`.

Test command clarification:

- `ctest` from the source root prints `No tests were found!!!`.
- Correct command is `ctest --test-dir build --output-on-failure`.
- Added root shortcuts: `make test` and `./scripts/test.sh`.
- Verified `make test`: passed, 3/3 tests.
- Verified `./scripts/test.sh`: passed, 3/3 tests.

Server run clarification:

- `make test` only runs tests; it does not start the web server.
- `make run` uses SQLite database `data/rimau.sqlite3`, whose default port is 8080.
- In the local environment, port 8080 is already in use.
- Added `make serve` and `./scripts/serve-dev.sh` for development on `127.0.0.1:18080`.
- `make serve` and `./scripts/serve-dev.sh` run the server in the foreground; keep that terminal open while testing.
- Added `make start`, `make status`, and `make stop` for background dev server lifecycle.
- Verified `./scripts/serve-dev.sh`: server listened on `127.0.0.1:18080`.
- Verified `curl -i http://127.0.0.1:18080/`: returned `HTTP/1.1 200 OK`.
- Verified `make start`: server stayed running as background process on `127.0.0.1:18080`.
- Verified `make status`: reported the background server PID.

SQLite config migration:

- Removed key-value config file support from runtime.
- Removed `config/rimau.conf.example`.
- At this phase, added SQLite dependency through CMake `find_package(SQLite3 REQUIRED)`; this was later replaced by bundled static SQLite.
- Added default SQLite database path `data/rimau.sqlite3`.
- Added CLI `--database path`.
- Added CLI `--set key=value`.
- `--config` now exits with an error and points users to `--database`.

Bundled OpenSSL and HTTPS:

- Added CMake `ExternalProject_Add` target `rimau_bundled_openssl`.
- Pinned bundled OpenSSL to `openssl-4.0.1`.
- Verified bundled OpenSSL CLI: `OpenSSL 4.0.1 9 Jun 2026`.
- Verified `ldd build/rimau-server` does not show `libssl` or `libcrypto`.
- Added SQLite keys `tls_enabled`, `tls_certificate_file`, and `tls_private_key_file`.
- Added `make start-https`, `make stop-https`, `make status-https`, and `make serve-https`.
- Generated dev certificate in ignored `certs/` using bundled OpenSSL.
- Verified `curl -k -I https://127.0.0.1:18443/`: returned `HTTP/1.1 200 OK`.

HTTP/2/HTTP/3 support-system update:

- Added SQLite keys `http1_enabled`, `http2_enabled`, and `http3_enabled`.
- `--protocols` now loads SQLite config and prints `implemented`, `configured`, and `target_transport`.
- Added `http2_status(config)` and `http3_status(config)` gateway status helpers.
- Server startup now fails clearly if HTTP/1.1 is disabled while only unimplemented HTTP/2/HTTP/3 are configured.
- Added `tests/test_protocol_capabilities.cpp`.

Validation on 2026-07-18:

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

- Build: passed.
- CTest: passed, 4/4 tests.
- Default config validation: passed.
- SQLite config validation: passed.
- Protocol status command: passed and reports HTTP/2/HTTP/3 as configured separately from implemented status.
- Smoke check with temporary SQLite database confirmed startup exits with error when `http1_enabled=false` and only HTTP/2/HTTP/3 are enabled.

Asynchronous non-blocking I/O update:

- Replaced thread-per-client runtime in `src/core/server.cpp` with Linux `epoll` worker reactors.
- Listener and accepted client sockets are non-blocking.
- Added `ClientConnection` state machine for HTTP/1.1 request read, transaction dispatch, response write, and connection close.
- TLS mode now uses non-blocking OpenSSL state transitions for handshake/read/write/shutdown.
- Removed legacy blocking `include/rimau/protocol/http1_connection.hpp` and `src/protocol/http1_connection.cpp` from the project.
- `make dev-db`, `make https-db`, `scripts/serve-dev.sh`, and `scripts/serve-dev-https.sh` now reset protocol flags to HTTP/1.1 enabled and HTTP/2/HTTP/3 disabled for deterministic local serving.

Validation on 2026-07-18:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
make start
curl -i --max-time 5 http://127.0.0.1:18080/
make stop
curl -k -I --max-time 5 https://127.0.0.1:18444/
```

Result:

- Build: passed.
- CTest: passed, 4/4 tests.
- HTTP smoke test returned `HTTP/1.1 200 OK`.
- HTTPS smoke test on temporary SQLite database and port 18444 returned `HTTP/1.1 200 OK`.
- Historical runtime log for the earlier poll phase reported: `I/O model: asynchronous poll event loop with non-blocking listener and client sockets`.

Linux epoll worker reactor update:

- Added SQLite keys `worker_threads`, `epoll_max_events`, `reuse_port_enabled`, `tcp_keepalive_enabled`, `tcp_keepalive_idle_seconds`, `tcp_keepalive_interval_seconds`, `tcp_keepalive_probe_count`, `graceful_shutdown_timeout_seconds`, and `connection_pool_size`.
- Replaced the single `poll()` loop with per-worker Linux `epoll` reactors.
- Worker threads use `std::jthread`; `worker_threads=0` auto-detects CPU cores.
- Each worker owns its own listener socket and event loop.
- Listener sockets use `SO_REUSEADDR` and `SO_REUSEPORT`.
- Accepted sockets use non-blocking I/O and optional TCP keepalive.
- Per-worker connection pool reuses `ClientConnection` objects and retained string buffer capacity to reduce malloc/free churn.
- SIGTERM and SIGINT request graceful shutdown; workers stop accepting and drain active connections.
- Historical note for this phase: SIGHUP was handled and logged, but live reload was not implemented yet.

Validation on 2026-07-18:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database <temp db with worker_threads=2 on port 18081>
curl -i --max-time 5 http://127.0.0.1:18081/
kill -TERM <pid>
./build/rimau-server --database <temp db with worker_threads=2 and TLS on port 18444>
curl -k -I --max-time 5 https://127.0.0.1:18444/
kill -TERM <pid>
kill -HUP <pid>
```

Result:

- Build: passed.
- CTest: passed, 4/4 tests.
- HTTP epoll smoke test returned `HTTP/1.1 200 OK`.
- HTTPS epoll smoke test returned `HTTP/1.1 200 OK`.
- SIGTERM shutdown drained and stopped workers cleanly.
- SIGHUP was logged and the server continued serving requests.

Run failure triage on 2026-07-18:

- `./build/rimau-server --database data/rimau.sqlite3` initially failed because the local SQLite config was still set to `0.0.0.0:8080`, and port `8080` was already occupied on this machine.
- Updated the ignored local workspace database `data/rimau.sqlite3` to `host=127.0.0.1` and `port=18080`.
- Verified `./build/rimau-server --database data/rimau.sqlite3` starts successfully, serves `GET /` with `HTTP/1.1 200 OK`, and stops cleanly on SIGTERM.

HTTP keep-alive and SIGHUP reload update:

- Added SQLite keys `http_keep_alive_enabled`, `http_keep_alive_timeout_seconds`, and `http_keep_alive_max_requests`.
- At this phase, HTTP/1.1 connections stayed open by default for requests without body framing unless the client sent `Connection: close`, keep-alive was disabled, or the per-connection request cap was reached. Later HTTP/1.1 work added `Content-Length` and chunked request body framing support.
- HTTP/1.0 keep-alive is allowed only when the client sends `Connection: keep-alive`.
- Added keep-alive idle timeout cleanup in each worker event loop.
- Response serialization now honors caller-provided `Connection` and `Keep-Alive` headers.
- SIGHUP reload now rereads SQLite and publishes a new runtime config snapshot when changed keys do not require listener, worker, pool, or TLS recreation.
- SIGHUP reload rejects changes to listener, worker, pool, HTTP/1 enablement, and TLS keys with a restart-required warning.
- Added `tests/test_http_response.cpp`.

Validation on 2026-07-18:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --database <temp db on port 18180 with keep-alive enabled>
# single TCP socket: GET /, then GET / with Connection: close
kill -HUP <pid>
curl -sS --max-time 5 http://127.0.0.1:18180/
kill -TERM <pid>
```

Result:

- Build: passed.
- CTest: passed, 5/5 tests.
- Single-socket keep-alive smoke returned two `HTTP/1.1 200 OK` responses; first response used `connection: keep-alive`, second response used `connection: close`.
- SIGHUP reload changed `document_root` from SQLite without restarting the process; subsequent request served the new document root.
- SIGTERM shutdown drained and stopped the worker cleanly.
- `make test`: passed, 5/5 tests.
- `make check`: passed and printed the new HTTP keep-alive SQLite keys.
- `make start` plus `curl -i http://127.0.0.1:18080/`: returned `HTTP/1.1 200 OK`.
- `make start-https` plus `curl -k -I https://127.0.0.1:18443/`: returned `HTTP/1.1 200 OK`.
- `ldd build/rimau-server | rg 'ssl|crypto' || true`: no dynamic `libssl` or `libcrypto` links were reported.

HTTP/1.1 feature expansion update:

- Added URL-decoded path parsing, query string parsing, query parameter storage, and request body storage.
- Added complete-message detection for header-only, `Content-Length`, and `Transfer-Encoding: chunked` requests.
- Added chunked request body decoding before handler dispatch.
- Keep-alive now works for complete requests using supported body framing.
- Basic request pipelining keeps later request bytes buffered and processes the next request after the active response is written.
- Added handler-level support for GET, HEAD, OPTIONS, POST, PUT, PATCH, and DELETE.
- GET and HEAD continue to serve static files.
- POST, PUT, PATCH, and DELETE return JSON scaffold responses with method, target, path, query parameters, body size, JSON detection, and escaped body.
- Added single byte-range static file responses with `206 Partial Content`.
- Expanded MIME type detection for common text, image, video, wasm, and pdf extensions.
- Added gzip compression for compressible static responses when the client sends `Accept-Encoding: gzip`.
- Added basic RFC 6455 WebSocket upgrade and two-way echo for text/binary frames, ping/pong, and close.

Validation on 2026-07-18:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
make status
make status-https
git status --short --branch
```

Manual smoke result:

- OPTIONS returned `204 No Content`.
- POST, PUT, PATCH, and DELETE returned JSON scaffold responses.
- Chunked POST decoded the request body.
- Range GET returned `206 Partial Content`.
- Gzip GET returned `content-encoding: gzip`.
- Two pipelined GET requests over one raw socket returned two `HTTP/1.1 200 OK` responses in order.
- WebSocket raw socket test completed `101 Switching Protocols` and echoed text payload `rimau`.
- SIGTERM graceful shutdown completed.

Final validation after capability-status cleanup:

```bash
make test
make check
./build/rimau-server --protocols
ldd build/rimau-server | rg 'ssl|crypto|z' || true
make status && make status-https
```

Result:

- `make test`: passed, 5/5 tests.
- `make check`: passed.
- `--protocols` now reports the expanded HTTP/1.1 feature set and still reports HTTP/2/HTTP/3 as not implemented.
- Runtime smoke with a temporary SQLite database passed for OPTIONS, POST JSON scaffold, chunked POST, single range, gzip, pipelined GET, WebSocket echo, and shutdown.
- `ldd` showed `libz.so.1` for gzip; no dynamic `libssl` or `libcrypto` links were reported.
- `make status` and `make status-https` reported no background dev server running.

Security and TLS hardening update:

- Added SQLite keys for TLS min/max version, TLS 1.2 cipher list, TLS 1.3 ciphersuites, ALPN protocols, SNI hosts, SNI required mode, request/header/body/idle timeout, global/per-IP connection limits, rate limiting, IP allow/block lists, WebSocket frame size, security headers, and `Server` header emission.
- OpenSSL TLS context now enforces TLS 1.2/TLS 1.3 range, configured ciphers, SNI validation, and ALPN `http/1.1`.
- SIGHUP can publish a new TLS context for certificate/key/TLS setting changes without stopping the server; new connections use the new context.
- Added request-smuggling checks before handler dispatch.
- Added header/response-splitting mitigation in response serialization.
- Added default security headers and support for disabling the `Server` header.
- Added basic slowloris mitigation through header/body timeout.
- Added global/per-IP connection limit, fixed-window per-IP rate limiting, and IPv4 exact/CIDR allow/block list.
- Added dedicated WebSocket max frame size enforcement.
- Strengthened static directory-index canonicalization to reduce symlink escape risk.
- Updated Makefile and dev scripts to seed new SQLite keys for development databases.

Validation on 2026-07-18:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
```

Runtime smoke result with temporary SQLite databases:

- Security headers were emitted.
- `Server` header could be disabled.
- Duplicate `Content-Length` returned `400 Bad Request`.
- `Content-Length` plus `Transfer-Encoding` returned `400 Bad Request`.
- Invalid `Content-Length` returned `400 Bad Request`.
- Oversized headers returned `431 Request Header Fields Too Large`.
- Per-IP rate limiting returned `429 Too Many Requests`.
- Slow partial headers timed out or closed.
- Oversized WebSocket frame returned a close frame.
- HTTPS worked with TLS 1.2 and TLS 1.3.
- ALPN negotiated `http/1.1`.
- SNI required mode accepted configured hostname.
- SIGHUP SNI setting reload worked for new TLS connections.

Final validation after protocol capability note update:

```bash
make test && make check
```

Result:

- Passed, 5/5 tests.
- `--protocols` reports HTTP/1.1 baseline timeout/rate-limit/security controls and still reports HTTP/2/HTTP/3 as not implemented.

SNI, IPv6, and security header configuration update:

- Added SQLite key `tls_sni_certificates` for semicolon-separated `hostname-pattern=certificate.pem|private-key.pem` mappings.
- TLS now creates an OpenSSL `SSL_CTX` for each configured SNI certificate entry and selects the matching context in the SNI callback.
- The default `tls_certificate_file` and `tls_private_key_file` remain the fallback certificate.
- `tls_sni_hosts` remains an optional allowlist; configured SNI certificate patterns are also accepted by the callback.
- Listener bind now supports IPv4 or IPv6 literals.
- IP allowlist/blocklist matching now supports IPv4 and IPv6 exact addresses and CIDR ranges.
- SQLite config validation now rejects malformed IPv4/IPv6 IP list entries.
- Added SQLite-configurable values for the fixed security header set.
- Empty individual security header values disable that header while `security_headers_enabled=true`.
- Dev database Makefile/script targets now reset IP lists, SNI cert map, and security header values for deterministic local runs.

Validation on 2026-07-19:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make test && make check
```

Runtime smoke result with temporary SQLite databases:

- `make test && make check` passed, 5/5 tests.
- `--check-config` prints `tls_sni_certificates`, IPv4/IPv6 IP list keys, and configurable `security_header_*` values.
- `--protocols` still reports HTTP/2 and HTTP/3 as not implemented.
- `ldd build/rimau-server | rg 'ssl|crypto|z' || true` showed `libz.so.1` only; no dynamic `libssl` or `libcrypto`.
- `make status && make status-https` reported no background dev server running.
- `make dev-db && make https-db` passed after adding the new SQLite seed keys.
- IPv6 listener on `::1` returned `HTTP/1.1 200 OK`.
- IPv6 allowlist `::1/128` allowed the request.
- Security header override emitted `x-frame-options: DENY`.
- Empty `security_header_x_content_type_options` suppressed `x-content-type-options`.
- SNI multi-certificate smoke selected the default certificate for `default.rimau.test`.
- SNI multi-certificate smoke selected the configured alternate certificate for `api.rimau.test`.

Virtual host, reverse proxy, and server-side runtime declaration update:

- Added SQLite keys `virtual_hosts_enabled`, `virtual_hosts`, `reverse_proxy_connect_timeout_seconds`, `reverse_proxy_read_timeout_seconds`, and `reverse_proxy_max_response_bytes`.
- Added `rimau::http::VirtualHostHandlerFactory` to select static, reverse proxy, script-placeholder, or fallback static handlers from the request `Host` header.
- Added exact hostname matching and simple `*.domain` wildcard matching.
- Added static virtual hosts with per-host document roots.
- Added baseline reverse proxy virtual hosts for upstream `http://` targets.
- Reverse proxy strips hop-by-hop headers, sets upstream `Host`, sets `Connection: close`, forwards `X-Forwarded-Host`, supports buffered upstream `Content-Length` or chunked response bodies, and enforces `reverse_proxy_max_response_bytes`.
- Added `script:runtime:path` virtual host declarations for runtime names such as `php`, `python`, and `perl`.
- Script virtual hosts return `501 Not Implemented` with `x-rimau-runtime-status: planned`; no PHP/Python/Perl runtime is bundled or executed yet.
- Added `tests/test_virtual_host.cpp`.
- Updated Makefile and dev scripts to seed virtual host and reverse proxy SQLite defaults.

Known limits from this update:

- Reverse proxy response handling is buffered, not streaming.
- Reverse proxy normal HTTP upstream readiness uses `poll()` inside the handler and DNS resolution uses `getaddrinfo`; normal HTTP upstream sockets are not integrated into the per-worker `epoll` reactor yet.
- At this phase there was no per-upstream HTTPS CA/pinning/verify-depth policy, health checks, circuit breaker, upstream connection pooling, or normal HTTP proxy streaming yet. Passive circuit breaker was added later.
- Built-in server-side runtimes remain planned. Bundling PHP, Python, Perl, or another runtime needs separate source/version pinning, build integration, security design, and tests. Needs verification.

Validation on 2026-07-19 after virtual host and reverse proxy update:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make check
make test
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server | rg 'ssl|crypto|z' || true
make dev-db && make https-db
make status
make status-https
```

Result:

- Build passed.
- CTest passed, 6/6 tests.
- `make check` passed and printed the new `virtual_hosts_*` and `reverse_proxy_*` SQLite keys.
- Manual smoke with temporary SQLite databases passed for static virtual host, HTTP reverse proxy virtual host, and `script:php` placeholder returning `501` with `x-rimau-runtime-status: planned`.
- `make test` passed, 6/6 tests.
- `--protocols` still reports HTTP/2 and HTTP/3 as not implemented.
- `ldd` showed dynamic `libz.so.1` for gzip only; no dynamic `libssl` or `libcrypto` links were reported.
- `make dev-db && make https-db` passed after seeding the new SQLite keys.
- `make status` and `make status-https` reported no background dev server running.

Reverse proxy upstream pool and HTTPS update:

- Added `reverse_proxy_retry_count` SQLite key.
- Added `reverse_proxy_tls_verify_upstream` SQLite key.
- `proxy:` virtual host rules now accept comma-separated upstream URLs.
- Reverse proxy upstream URLs now support `http://` and `https://`.
- Upstream HTTPS transport uses bundled OpenSSL.
- Added basic process-local round-robin upstream selection.
- Added retry/failover across upstream targets for connection, TLS handshake, write, read, and invalid upstream response failures.
- Updated `tests/test_virtual_host.cpp` and `tests/test_config_database.cpp` for HTTPS upstream parsing, multiple upstream parsing, and retry config.
- Updated Makefile and dev scripts to seed `reverse_proxy_retry_count=1`.

Known limits from this update:

- HTTPS upstream can verify certificates through default trust paths when `reverse_proxy_tls_verify_upstream=true`, but per-upstream CA/pinning/verify-depth policy is not complete. Needs verification.
- Reverse proxy normal HTTP response handling is still buffered and handler-level; normal HTTP upstream sockets are not integrated into the per-worker `epoll` reactor.
- No active health-check scheduler, upstream connection pooling, or normal HTTP proxy streaming yet.

Validation on 2026-07-19 after reverse proxy upstream pool and HTTPS update:

```bash
make test
make check
ldd build/rimau-server | rg 'ssl|crypto|z' || true
make status
make status-https
make dev-db && make https-db
```

Manual smoke with temporary SQLite databases:

- HTTP reverse proxy failover from a dead upstream to a live upstream returned `http failover ok`.
- HTTPS reverse proxy upstream through bundled OpenSSL returned `https upstream ok`.

Result:

- `make test`: passed, 6/6 tests.
- `make check`: passed and printed `reverse_proxy_retry_count` plus `reverse_proxy_tls_verify_upstream`.
- `ldd` showed dynamic `libz.so.1` only; no dynamic `libssl` or `libcrypto` links were reported.
- `make dev-db && make https-db`: passed.
- `make status` and `make status-https`: no background dev server running.

WebSocket reverse proxy tunnel update:

- Added `rimau::http::reverse_proxy_target_path` so normal HTTP proxying and WebSocket proxying share upstream path joining behavior.
- Added reverse proxy virtual host lookup before the local WebSocket echo path.
- WebSocket Upgrade requests for reverse proxy vhosts now connect to selected upstreams from the configured upstream pool.
- Upstream WebSocket handshakes support `http://` and `https://`; HTTPS upstream transport uses bundled OpenSSL.
- Upstream `101 Switching Protocols` headers are parsed, validated, rebuilt, and serialized before sending to the client.
- Added an `upstream` epoll token so each `ClientConnection` can own the client fd and one WebSocket upstream fd.
- After handshake, client-to-upstream and upstream-to-client bytes are tunneled through the worker `epoll` reactor.
- Client TLS and upstream TLS tunnel writes/reads use OpenSSL non-blocking state handling.
- WebSocket proxy handshakes are counted against the in-memory per-IP request rate limiter.
- Non-proxy WebSocket requests still use the existing basic local echo implementation.
- Updated protocol capability notes to include WebSocket reverse proxy tunneling.

Known limits from this update:

- DNS resolution, TCP connect, upstream TLS handshake, upstream handshake request write, and upstream `101` header read happen before tunnel mode; they are not yet a fully asynchronous upstream state machine.
- WebSocket proxy tunnels bytes and enforces bounded pending buffers, but it does not yet implement full frame-aware proxy policy for fragmentation, extensions, subprotocol negotiation, or per-frame inspection. Needs verification for high-load backpressure behavior.
- Normal HTTP reverse proxy responses remain buffered.
- Active health checks, upstream connection pooling, advanced load balancing, and per-upstream TLS CA/pinning/verify-depth policy remain planned.

Validation on 2026-07-19 after WebSocket reverse proxy tunnel update:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make test
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server | rg 'ssl|crypto|z' || true
make dev-db
make https-db
make status
make status-https
```

Manual smoke with temporary SQLite databases:

- HTTP WebSocket upstream through proxy vhost returned `proxied:hello`.
- HTTPS WebSocket upstream through proxy vhost returned `tls-proxied:hello`.

Result:

- Build passed.
- CTest passed, 6/6 tests.
- `make test` passed, 6/6 tests.
- Default and `data/rimau.sqlite3` config checks passed.
- `--protocols` reports WebSocket reverse proxy tunneling for HTTP/1.1 and still reports HTTP/2/HTTP/3 as not implemented.
- `ldd` showed dynamic `libz.so.1` only; no dynamic `libssl` or `libcrypto` links were reported.
- `make dev-db` and `make https-db` passed.
- `make status` and `make status-https` reported no background dev server running.
- `git status --short --branch` still fails because this directory is not a Git repository.

Reverse proxy passive circuit breaker update:

- Added SQLite keys `reverse_proxy_circuit_breaker_enabled`, `reverse_proxy_circuit_breaker_failure_threshold`, and `reverse_proxy_circuit_breaker_cooldown_seconds`.
- Added process-local in-memory circuit breaker state keyed by upstream target.
- Normal HTTP reverse proxy now skips upstreams whose circuit is open.
- WebSocket reverse proxy setup now uses the same circuit breaker before upstream connect/handshake.
- Upstream success clears circuit state; upstream failure increments the failure count and opens the circuit when the configured threshold is reached.
- If every selected upstream is skipped because its circuit is open, Rimau returns `503 Service Unavailable` with `Retry-After`.
- Updated Makefile and dev scripts to seed circuit breaker defaults.
- Updated config and virtual host tests for circuit breaker behavior.

Known limits from this update:

- Circuit breaker state is in-memory per process and not distributed.
- There is no active health-check scheduler yet; opened circuits are retried only after cooldown.
- No circuit-breaker metrics or admin inspection API exist yet.

Validation on 2026-07-19 after passive circuit breaker update:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
make dev-db
make https-db
./build/rimau-server --database build/rimau-dev.sqlite3 --check-config
./build/rimau-server --database build/rimau-dev-https.sqlite3 --check-config
ldd build/rimau-server | rg 'ssl|crypto|z' || true
make status
make status-https
```

Manual smoke with a temporary SQLite database:

- First HTTP proxy request to a dead upstream returned `502`.
- Second HTTP proxy request to the same dead upstream returned `503`.
- WebSocket Upgrade to the same proxy vhost while the circuit was open returned `HTTP/1.1 503 Service Unavailable`.

Result:

- Build passed.
- CTest passed, 6/6 tests.
- `make test` passed, 6/6 tests.
- `make check` passed.
- Default and `data/rimau.sqlite3` config checks passed and printed the new circuit breaker keys.
- `--protocols` still reports HTTP/1.1 implemented with WebSocket reverse proxy tunneling and HTTP/2/HTTP/3 not implemented.
- `make dev-db` and `make https-db` passed and seeded the new circuit breaker keys.
- `build/rimau-dev.sqlite3` and `build/rimau-dev-https.sqlite3` config checks passed and printed the new circuit breaker keys.
- `ldd` showed dynamic `libz.so.1` only; no dynamic `libssl` or `libcrypto` links were reported.
- `make status` and `make status-https` reported no background dev server running.
- `git status --short --branch` still fails because this directory is not a Git repository.

Bundled zlib and SQLite dependency update:

- Removed system `find_package(ZLIB)` usage.
- Added CMake target `rimau_bundled_zlib`.
- Pinned bundled zlib to version `1.3.2` with SHA256 in `CMakeLists.txt`.
- Linked Rimau against `build/_deps/zlib/install/lib/libz.a`.
- Removed system `find_package(SQLite3)` usage.
- Added CMake target `rimau_bundled_sqlite`.
- Pinned bundled SQLite to version `3.53.3`, release id `3530300`, with SHA256 in `CMakeLists.txt`.
- Compiled SQLite amalgamation into `build/_deps/sqlite/install/lib/libsqlite3.a`.
- Compiled SQLite with `SQLITE_OMIT_LOAD_EXTENSION=1`.
- Added CMake guard options that reject system SQLite/zlib builds.
- Added `RIMAU_STATIC_CXX_RUNTIME=ON` so `rimau-server` static-links `libstdc++` and `libgcc` on Linux GNU/Clang builds when supported.

Known limits from this update:

- This section is superseded by the bundled glibc source static update below for the current Linux x86_64 deployment binary.
- First build needs network access unless CMake dependency downloads are already cached.

Validation on 2026-07-19 after bundled zlib/SQLite update:

```bash
cmake -S . -B build -DRIMAU_ENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
make dev-db
make https-db
./build/rimau-server --database build/rimau-dev.sqlite3 --check-config
./build/rimau-server --database build/rimau-dev-https.sqlite3 --check-config
ldd build/rimau-server
make status
make status-https
git status --short --branch
```

Result:

- Build passed.
- CTest passed, 6/6 tests.
- `make test` passed, 6/6 tests.
- `make check` passed.
- Default and `data/rimau.sqlite3` config checks passed and printed current SQLite-backed config values.
- Default and `data/rimau.sqlite3` protocol status checks passed; at that phase HTTP/2 and HTTP/3 still reported `not implemented`.
- `make dev-db` and `make https-db` passed.
- `build/rimau-dev.sqlite3` and `build/rimau-dev-https.sqlite3` config checks passed and printed the current bundled-SQLite-backed config values.
- At this phase `ldd build/rimau-server` reported only `libc.so.6` and the Linux dynamic loader; this is superseded by the bundled glibc source static update below.
- No dynamic `libssl`, `libcrypto`, `libz`, `libsqlite3`, `libstdc++`, or `libgcc_s` links were reported at this phase.
- `make status` and `make status-https` reported no background dev server running.
- `git status --short --branch` still fails because this directory is not a Git repository.

Bundled glibc source static update:

- Added CMake option `RIMAU_FULLY_STATIC_SERVER=ON`.
- Added CMake option `RIMAU_USE_BUNDLED_GLIBC=ON`.
- Added CMake target `rimau_bundled_bison` using GNU Bison 3.8.2 source.
- Added CMake target `rimau_bundled_linux_headers` using Linux kernel source 6.18.7 for UAPI headers.
- Added CMake target `rimau_bundled_glibc` using GNU glibc 2.43 source.
- Configured glibc with `--without-selinux` after the first build attempted to use host `libselinux` without matching headers.
- Configured bundled OpenSSL with `no-dso`.
- Linked `rimau-server` with `-static`, `-B build/_deps/glibc/sysroot/usr/lib`, and `--sysroot=build/_deps/glibc/sysroot`.

Known limits from this update:

- Bundled glibc source build is currently wired only for Linux x86_64.
- First build needs network access unless CMake source downloads are cached.
- Build-time still requires host compiler/tooling such as `gcc`/`g++`, `make`, `perl`, `gawk`, `msgfmt`, `python3`, and `m4`; these are not runtime shared-library dependencies for `rimau-server`.
- glibc configure warns that `makeinfo` is missing; some glibc documentation/test features are disabled on this machine. Needs verification if glibc upstream test suites become a release requirement.
- Static glibc DNS/NSS hostname lookup still needs production validation. Linker warnings were emitted for `getaddrinfo` in Rimau reverse proxy code and `gethostbyname` inside bundled OpenSSL. Needs verification.

Validation on 2026-07-19 after bundled glibc source static update:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
make test
make check
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
make status
make status-https
git status --short --branch
```

Result:

- Build passed.
- CTest passed, 6/6 tests.
- `make test` passed, 6/6 tests.
- `make check` passed.
- Default and `data/rimau.sqlite3` config checks passed.
- Default and `data/rimau.sqlite3` protocol status checks passed; at that phase HTTP/2 and HTTP/3 still reported `not implemented`.
- `ldd build/rimau-server` reported `not a dynamic executable`.
- `file build/rimau-server` reported a statically linked Linux x86-64 ELF.
- `readelf` found no dynamic interpreter.
- Link command in `build/CMakeFiles/rimau-server.dir/link.txt` includes `--sysroot=/home/data/tunnelbiz/rimauwebserver/build/_deps/glibc/sysroot` and `-B/home/data/tunnelbiz/rimauwebserver/build/_deps/glibc/sysroot/usr/lib/`.
- Bundled glibc sysroot contains `usr/lib/libc.a`, `crt1.o`, `crti.o`, and `crtn.o`.
- Bundled Bison binary exists at `build/_deps/bison/install/bin/bison`.
- Bundled Linux headers exist under `build/_deps/linux-headers/install/include`.
- `make status` and `make status-https` reported no background dev server running.
- `git status --short --branch` still fails because this directory is not a Git repository.

HTTP/2 and HTTP/3 wire codec update:

- Added `include/rimau/protocol/http2_frame.hpp` and `src/protocol/http2_frame.cpp`.
- Added `include/rimau/protocol/http2_hpack.hpp` and `src/protocol/http2_hpack.cpp`.
- Added `include/rimau/protocol/http3_frame.hpp` and `src/protocol/http3_frame.cpp`.
- Added CTest targets `rimau_http2_wire` and `rimau_http3_wire`.
- HTTP/2 frame parser/serializer now handles the 9-byte frame header, payload length, type, flags, stream id, and payload.
- HTTP/2 SETTINGS payload parser/serializer exists.
- HTTP/2 frame validation exists for SETTINGS, PING, GOAWAY, WINDOW_UPDATE, RST_STREAM, DATA, HEADERS, and CONTINUATION basics.
- HPACK baseline supports static-table indexed fields and literal fields without Huffman or dynamic table indexing.
- `ClientConnection` now detects cleartext HTTP/2 h2c prior-knowledge preface before HTTP/1.1 parsing.
- At that wire-codec phase, when `http2_enabled=true`, Rimau parsed client SETTINGS and replied with HTTP/2 SETTINGS, SETTINGS ACK, and GOAWAY.
- HTTP/3 QUIC varint parser/serializer exists.
- HTTP/3 frame parser/serializer exists.
- HTTP/3 SETTINGS payload parser/serializer exists.
- `--protocols` now reports HTTP/2 and HTTP/3 as `partial` instead of config/status-only.

Known limits from that wire-codec-only update before the h2c request-serving update:

- At that phase, HTTP/2 request serving was not implemented.
- HTTP/2 stream lifecycle, multiplexing, flow control, continuation assembly, priority, HPACK Huffman, and HPACK dynamic table behavior are not implemented.
- HTTP/2 over TLS ALPN `h2` is not advertised yet.
- HTTP/3 live serving is not implemented.
- HTTP/3 UDP listener, QUIC transport, QUIC TLS 1.3 handshake, QPACK, stream lifecycle, request adapter, and ALPN `h3` are not implemented.
- Full HTTP/2/HTTP/3 support may still need mature bundled source dependencies. Needs verification.

Validation on 2026-07-19 after HTTP/2 and HTTP/3 wire codec update:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --protocols
```

Manual h2c smoke:

```text
client sends HTTP/2 connection preface + empty SETTINGS
server responds SETTINGS + SETTINGS ACK + GOAWAY
```

Result:

- Build passed.
- CTest passed, 8/8 tests.
- `--protocols` reports HTTP/2 and HTTP/3 as `partial`.
- h2c smoke parsed three returned HTTP/2 frames: SETTINGS, SETTINGS ACK, and GOAWAY.

HTTP/2 h2c request-serving update:

- Extended cleartext HTTP/2 h2c prior-knowledge handling from handshake-only to partial request serving.
- `ClientConnection` now keeps HTTP/2 connection state after client preface and SETTINGS.
- HTTP/2 server path now handles SETTINGS, SETTINGS ACK, PING, RST_STREAM, WINDOW_UPDATE, HEADERS, and DATA basics.
- HTTP/2 HEADERS payloads are decoded with the project HPACK baseline; literal incremental indexing is accepted without dynamic-table persistence.
- HTTP/2 pseudo-headers and regular headers are translated into `rimau::http::Request`.
- Complete h2c streams are dispatched through the existing `Transaction` and `VirtualHostHandlerFactory` pipeline.
- HTTP/2 responses are serialized as HEADERS and DATA frames.
- h2c-only startup is allowed when `http1_enabled=false`, `http2_enabled=true`, and `tls_enabled=false`.
- Protocol status text now reports HTTP/2 as partial h2c request serving instead of handshake-only behavior.

Known limits from this update:

- At this h2c update phase, HTTP/2 was still cleartext h2c only and ALPN `h2` was not advertised. This is superseded by the 2026-07-20 TLS ALPN `h2` partial request-serving update below.
- HPACK Huffman, dynamic table persistence, dynamic table references, CONTINUATION frame assembly, production stream lifecycle, full multiplexing behavior, priority handling, and flow control are still not implemented.
- HTTP/3 remains wire-codec-only; UDP/QUIC/QPACK/live request serving is not implemented.
- Automated real-client HTTP/2 integration tests are still pending.

Validation on 2026-07-19 after HTTP/2 h2c request-serving update:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Manual h2c smoke:

```text
client sends HTTP/2 connection preface + SETTINGS + HEADERS GET /
server responds SETTINGS + SETTINGS ACK + response HEADERS + response DATA

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
- Default and `data/rimau.sqlite3` config checks passed.
- Default and `data/rimau.sqlite3` protocol status checks passed; HTTP/2 reports `partial` with h2c request serving, HTTP/3 reports `partial` wire codec.
- `ldd build/rimau-server` reported `not a dynamic executable`.
- `file build/rimau-server` reported a statically linked Linux x86-64 ELF.
- `readelf` found no dynamic interpreter.
- h2c basic GET smoke passed.
- h2c GET plus POST DATA smoke passed.
- h2c-only startup smoke passed with `http1_enabled=false`, `http2_enabled=true`, and `tls_enabled=false`.
- `make status` and `make status-https` reported no background server running.
- `git status --short --branch` still fails because this directory is not a Git repository.

GitHub publishing update:

- Target GitHub repository: `https://github.com/asklinux/riwau-webserver`.
- Remote default branch is `main`.
- Existing remote contained a GPL-3.0 `LICENSE`; it was preserved and copied into the local workspace.
- Project source was synced into a temporary clone of the remote so the existing remote history was preserved.
- Pushed commit `903dadc` (`Add Rimau Web Server implementation`) to `main`.
- The original local workspace `/home/data/tunnelbiz/rimauwebserver` still was not initialized as a Git repository after this push. Future work should either initialize it carefully against the remote or use a fresh clone. Needs verification.

HTTP/2 TLS ALPN `h2` request-serving update:

- SQLite config validation now allows `tls_alpn_protocols` to contain `h2` when HTTP/2 is enabled for TLS serving, and still rejects `h3` until HTTP/3 live serving exists.
- TLS connection state records the selected ALPN protocol after `SSL_accept`.
- When TLS ALPN selects `h2`, `ClientConnection` requires the HTTP/2 client connection preface and enters the same partial HTTP/2 request-serving path used by h2c.
- TLS ALPN `h2` request serving handles client preface + SETTINGS, replies SETTINGS + SETTINGS ACK, decodes HEADERS/DATA with the HPACK baseline, dispatches complete streams through `Transaction` and `VirtualHostHandlerFactory`, and writes HTTP/2 HEADERS/DATA response frames.
- If a TLS client negotiates `h2` but sends non-HTTP/2 bytes, Rimau sends an HTTP/2 GOAWAY protocol error instead of parsing it as HTTP/1.1.
- Startup now permits HTTP/2-only TLS serving when `http1_enabled=false`, `http2_enabled=true`, `tls_enabled=true`, and `tls_alpn_protocols` includes `h2`.
- Protocol status text now reports partial TLS ALPN `h2` request serving when configured.

Known limits from this update:

- HTTP/2 remains partial, not production-complete.
- HPACK Huffman, dynamic table persistence, dynamic table references, CONTINUATION frame assembly, production stream lifecycle, broad multiplexing behavior, priority handling, and full flow control are still not implemented.
- Automated real-client HTTP/2 integration tests are still pending; current TLS smoke uses Python `ssl` plus manually built HTTP/2 frames.
- HTTP/3 remains wire-codec-only; UDP/QUIC/QPACK/live request serving and ALPN `h3` are not implemented.

Validation on 2026-07-20 after HTTP/2 TLS ALPN `h2` update:

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
temporary SQLite database with tls_enabled=true, http2_enabled=true, tls_alpn_protocols=h2,http/1.1
Python stdlib SSL client offers ALPN h2
server negotiates h2
client sends HTTP/2 connection preface + SETTINGS + HEADERS GET /
server responds SETTINGS + SETTINGS ACK + response HEADERS + response DATA

temporary SQLite database with http1_enabled=false, tls_enabled=true, http2_enabled=true, tls_alpn_protocols=h2
server starts in TLS HTTP/2-only mode
Python stdlib SSL client offers ALPN h2 and sends GET / as HTTP/2 frames
server responds with HTTP/2 HEADERS + DATA
```

Result:

- Build passed; static glibc DNS/NSS linker warnings for `getaddrinfo`/OpenSSL `gethostbyname` remain.
- CTest passed, 8/8 tests.
- `make test` passed, 8/8 tests.
- `make check` passed.
- TLS ALPN `h2` smoke passed.
- TLS HTTP/2-only startup and GET smoke passed.
- `ldd build/rimau-server` reported `not a dynamic executable`.
- `file build/rimau-server` reported a statically linked Linux x86-64 ELF.
- `readelf` found no dynamic interpreter.
- `make status` and `make status-https` reported no background server running.
- `git status --short --branch` still fails because this directory is not a Git repository.

ModSecurity-compatible WAF update:

- Added `include/rimau/http/waf.hpp` and `src/http/waf.cpp`.
- Added `rimau::http::WafSettings`, `WafMatch`, `WafResult`, `inspect_request`, and `waf_block_response`.
- Added SQLite config keys `modsecurity_enabled`, `modsecurity_owasp_crs_enabled`, `modsecurity_blocking_enabled`, `modsecurity_anomaly_threshold`, `modsecurity_max_inspection_bytes`, and `modsecurity_audit_log_enabled`.
- `modsecurity_enabled` defaults to `false`; all other WAF defaults are ready for blocking once enabled.
- HTTP/1.1 normal requests are inspected before `Transaction` dispatch.
- WebSocket upgrade requests are inspected before local echo or reverse proxy tunnel setup.
- Partial HTTP/2 requests are inspected before shared handler dispatch.
- WAF block responses return `403 Forbidden` with `x-rimau-waf-*` headers.
- Added CTest target `rimau_waf`.
- The WAF rules are compiled into Rimau and currently cover scanner user agents, encoded CRLF/request splitting, path traversal, XSS, SQLi, RCE, PHP wrapper injection, and Java/JNDI exploit patterns.

Known limits from this update:

- This is not full `libmodsecurity`.
- This does not bundle the full OWASP Core Rule Set.
- ModSecurity rule syntax parsing, full transaction phases, per-virtual-host WAF exceptions, structured persistent audit logs, and rule update tooling are not implemented. Needs verification.
- WAF audit logging currently writes a concise stderr log line through the existing logger when enabled; it is not a ModSecurity audit log format.

Validation on 2026-07-20 after ModSecurity-compatible WAF update:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --protocols
```

Manual HTTP WAF smoke:

```text
temporary SQLite database on 127.0.0.1:18180 with modsecurity_enabled=true
curl -i http://127.0.0.1:18180/
curl -i http://127.0.0.1:18180/../../etc/passwd
curl -i -A sqlmap/1.8 http://127.0.0.1:18180/
```

Result:

- Build passed; static glibc DNS/NSS linker warnings for `getaddrinfo`/OpenSSL `gethostbyname` remain.
- CTest passed, 9/9 tests.
- Default config check passed and printed the new `modsecurity_*` keys.
- Protocol status command passed.
- Manual WAF smoke returned `200 OK` for `/`.
- Manual WAF smoke returned `403 Forbidden` with `x-rimau-waf-rule-id: 930100` for path traversal.
- Manual WAF smoke returned `403 Forbidden` with `x-rimau-waf-rule-id: 913100` for scanner user-agent.
- A broad SQLi pattern initially caused a false positive on `Accept: */*`; the pattern was removed and a regression test for a normal curl-style request was added.

Ordered update checklist documentation:

- Added `docs/plans/021-ordered-update-checklist.md`.
- The checklist orders remaining work into phases: repo/automation, HTTP/1.1, security/WAF, TLS, HTTP/2, reverse proxy, HTTP/3, server-side runtimes, observability/admin, deployment, and performance.
- `docs/TODO.md` now points future work to the checklist for one-by-one updates.
- No code changes were made for this checklist update.

Workspace Git alignment update:

- Initialized `/home/data/tunnelbiz/rimauwebserver` as a Git repository.
- Added remote `origin` pointing to `https://github.com/asklinux/riwau-webserver.git`.
- Fetched `origin/main` at commit `a1b57618293676d60a377637690c1918b2b5e497`.
- Set local branch `main` to track the remote source state without deleting the existing working tree.
- `git status --short --branch` now works from the workspace.
- `docs/plans/021-ordered-update-checklist.md` Phase 0 first item is now checked.

CI workflow update:

- Added `.github/workflows/ci.yml`.
- `docs/plans/021-ordered-update-checklist.md` now marks the CI workflow file item as complete, while keeping the first remote GitHub Actions run as pending verification.
- CI triggers on push to `main` and on pull requests.
- The workflow installs build tools on `ubuntu-22.04`, configures CMake with `RIMAU_ENABLE_TESTS=ON`, `RIMAU_FULLY_STATIC_SERVER=OFF`, and `RIMAU_USE_BUNDLED_GLIBC=OFF`, then builds with Ninja.
- CI keeps bundled OpenSSL, SQLite, and zlib enabled, so tests still exercise the required bundled dependency path for those libraries.
- CI runs `ctest --test-dir build-ci --output-on-failure`, `rimau-server --check-config`, `rimau-server --protocols`, and `rimau-waf-tests`.
- Bundled glibc full static deployment checks remain a separate checklist item because building glibc from source is too heavy for fast PR feedback.
- First remote GitHub Actions run after push is still pending. Needs verification.
- Initial workflow run `29711550002` failed with `startup_failure` before jobs were created and without logs; workflow was simplified to remove the cache expression and use a stable hosted-runner label before retrying.
- Retry workflow run `29711700813` also failed with `startup_failure` before jobs were created and without logs. Local `actionlint` validation passed, so the failure still needs GitHub Actions runner/startup investigation. Needs verification.
- Third workflow run `29711874922` used `ubuntu-22.04` and still failed with `startup_failure` before jobs were created and without logs.
- Repository Actions permissions API reports Actions enabled and all actions/workflows allowed; the repo has no self-hosted runners configured. Needs verification whether GitHub-hosted runner startup is unavailable or another repository setting is blocking job creation.

SQLite schema migration metadata update:

- Added `rimau_schema_migrations` with current config schema version `1`.
- Added `rimau::core::config_schema_version`.
- Fresh SQLite config databases now bootstrap both `rimau_config` and `rimau_schema_migrations`.
- Existing SQLite config databases that already have `rimau_config` but no migration metadata are marked as version `1` during bootstrap.
- SQLite config databases with a recorded schema version newer than the binary supports are rejected.
- Updated `tests/test_config_database.cpp` to cover fresh DB bootstrap, legacy metadata bootstrap, and future-version rejection.
- Marked the SQLite schema migration/version checklist item complete in `docs/plans/021-ordered-update-checklist.md`.

Validation on 2026-07-20 after SQLite schema migration metadata update:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R rimau_config_database
./build/rimau-server --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --database data/rimau.sqlite3 --protocols
```

Result:

- Build passed; static glibc DNS/NSS linker warnings for `getaddrinfo`/OpenSSL `gethostbyname` remain.
- CTest passed, 9/9 tests.
- `rimau_config_database` passed.
- Default and `data/rimau.sqlite3` config checks passed.
- Default and `data/rimau.sqlite3` protocol status checks passed.

License status documentation update:

- Verified that the root `LICENSE` file contains GNU GPL version 3 text.
- Added ADR-0028 with status `Needs verification` because the repository file exists, but the project owner has not explicitly confirmed GPL-3.0 as the intended final Rimau Web Server license in project docs.
- Marked the license checklist item complete because the status is now documented as required by `docs/plans/021-ordered-update-checklist.md`.

CLI integration test update:

- Added CTest script `tests/test_cli_config.cmake`.
- Added CTest case `rimau_cli_config`.
- The test creates a temporary SQLite database through `rimau-server --database ... --check-config`.
- The test verifies `--set` persists config values, `--check-config` reads them back, `--protocols` reflects SQLite protocol flags, and invalid `--set port=70000` returns a failure with the expected validation error.
- Marked the CLI integration checklist item complete in `docs/plans/021-ordered-update-checklist.md`.

Validation on 2026-07-20 after CLI integration test update:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure -R rimau_cli_config
ctest --test-dir build --output-on-failure
```

Result:

- CMake configure passed.
- Build passed.
- `rimau_cli_config` passed.
- CTest passed, 10/10 tests.

HTTP/1.1 session extraction update:

- Added `include/rimau/http/http1_session.hpp` and `src/http/http1_session.cpp`.
- Added `rimau::http::next_http1_request_frame` to classify HTTP/1.1 buffered input as incomplete, header-complete/body-waiting, complete, or framing error.
- The new module owns HTTP/1.1 header framing validation, invalid `Content-Length` and `Transfer-Encoding` rejection, `Content-Length` body boundary detection, chunked body decoding, request size enforcement, and pipelining consumption length.
- `ClientConnection::process_buffered_request` now delegates HTTP/1.1 frame detection to this module before applying WebSocket, WAF, rate-limit, transaction dispatch, and keep-alive response behavior.
- Added CTest target `rimau_http1_session` so HTTP/1.1 parsing/framing can be tested without socket or `epoll`.
- Marked the first Phase 1 checklist item complete in `docs/plans/021-ordered-update-checklist.md`.

Validation on 2026-07-20 after HTTP/1.1 session extraction:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Manual Python socket smoke started `rimau-server` with a temporary SQLite database on `127.0.0.1:18182`, sent two pipelined `GET /` requests over one socket, and required two `HTTP/1.1 200 OK` responses.

Result:

- Build passed; static glibc DNS/NSS linker warnings for `getaddrinfo`/OpenSSL `gethostbyname` remain.
- CTest passed, 11/11 tests.
- Pipelined HTTP/1.1 keep-alive smoke passed.

HTTP/1.1 network integration test update:

- Added `tests/test_http1_network.py`.
- Added CTest case `rimau_http1_network`.
- The test starts `rimau-server` with a temporary SQLite database and temporary document root.
- Automated coverage now includes HTTP/1.1 keep-alive, keep-alive max request cap, idle timeout, request pipelining, chunked request body decoding, single range response, gzip static response, local WebSocket echo, and WebSocket reverse proxy tunneling through a Python upstream.
- Marked the Phase 1 HTTP/1.1 integration checklist item complete in `docs/plans/021-ordered-update-checklist.md`.

Validation on 2026-07-20 after HTTP/1.1 network integration test update:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure -R rimau_http1_network
ctest --test-dir build --output-on-failure
```

Result:

- CMake configure passed.
- Build passed.
- `rimau_http1_network` passed.
- CTest passed, 12/12 tests.
