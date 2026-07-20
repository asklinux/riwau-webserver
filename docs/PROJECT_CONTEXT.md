# Project Context

Tarikh dokumen mula dibuat: 2026-07-18.

## Tujuan Projek

Rimau Web Server ialah projek untuk membina web server menggunakan C++ dengan sasaran jangka panjang:

- Berprestasi tinggi seperti kelas OpenLiteSpeed dan nginx.
- Menyokong HTTP/1.1, HTTP/2, dan HTTP/3.
- Menyediakan struktur modular supaya transport, parser, scheduler, logging, config, static file, TLS, dan plugin boleh dibangunkan secara berperingkat.

Status semasa bukan web server production-ready. Scaffold awal sudah diwujudkan dengan HTTP/1.1 praktikal untuk static serving, body framing asas, file-backed request body spooling untuk upload besar, keep-alive, pipelining asas, range, gzip, WebSocket echo asas, WebSocket reverse proxy tunneling untuk proxy vhost, TLS hardening asas termasuk multi-certificate SNI, kawalan keselamatan asas, WAF terbina dalam yang ModSecurity-compatible dengan subset rules OWASP CRS-inspired, virtual host static, baseline HTTP reverse proxy dengan passive circuit breaker, HTTP/2 wire codec partial, cleartext h2c dan TLS ALPN `h2` request serving asas, dan HTTP/3 wire codec primitives.

Pada 2026-07-18, projek ini mula mengadaptasi konsep seni bina daripada Proxygen (`https://github.com/facebook/proxygen`) secara konseptual sahaja. Kod Proxygen tidak disalin ke repo ini.

## Fakta Repo Semasa

- Direktori kerja: `/home/data/tunnelbiz/rimauwebserver`
- Semasa pemeriksaan awal pada 2026-07-18, repo ini kosong.
- Semasa pemeriksaan awal pada 2026-07-18, direktori ini belum menjadi Git repository.
- Tiada sejarah Git, migration database, package manager, deployment file, atau dokumentasi lama ditemui sebelum scaffold dibuat.

## Technology Stack

Implemented:

- C++20
- CMake
- POSIX TCP sockets untuk HTTP/1.1 scaffold dengan bind IPv4 atau IPv6 literal
- Linux `epoll` reactor per worker thread
- Non-blocking listener/client sockets
- Worker thread count berdasarkan CPU core apabila `worker_threads=0`
- Per-worker `SO_REUSEPORT` listeners
- TCP keepalive untuk accepted client sockets
- HTTP/1.1 keep-alive dengan SQLite-configured idle timeout dan max requests per connection
- HTTP/1.1 request body accumulator dengan 16 KiB in-memory threshold dan `mkstemp`-backed temporary-file spooling untuk body besar sebelum handler dispatch
- Connection object pool yang mengekalkan buffer capacity untuk mengurangkan malloc/free
- SIGHUP live reload terhad untuk SQLite config yang tidak memerlukan listener/worker restart
- SQLite schema metadata table `rimau_schema_migrations` dengan current config schema version `1` dan guard untuk menolak database versi masa depan
- CTest untuk test asas
- Bundled static SQLite 3.53.3 untuk runtime server configuration
- Bundled static OpenSSL 4.0.1 untuk TLS/SSL, TLS 1.2/1.3, SNI validation, multi-certificate SNI selection, ALPN `http/1.1`, dan partial ALPN `h2` apabila HTTP/2 diaktifkan
- Bundled static zlib 1.3.2 untuk gzip response compression
- Bundled GNU Bison 3.8.2 sebagai build tool untuk glibc
- Bundled Linux UAPI headers daripada Linux kernel source 6.18.7 untuk glibc
- Bundled GNU glibc 2.43 source build untuk Linux x86_64 static sysroot
- Fully static `rimau-server` link pada Linux x86_64 GNU/Clang melalui bundled glibc sysroot apabila `RIMAU_FULLY_STATIC_SERVER=ON` dan `RIMAU_USE_BUNDLED_GLIBC=ON`
- In-memory per-process security state untuk global/per-IP connection limit dan fixed-window rate limiting
- Built-in ModSecurity-compatible WAF dengan subset OWASP CRS-inspired rules untuk scanner user-agent, request splitting/CRLF, path traversal, XSS, SQLi, RCE, PHP wrapper injection, dan Java/JNDI exploit patterns
- IPv4/IPv6 exact/CIDR IP allowlist dan blocklist
- SQLite-configurable HTTP security header values
- SQLite-configured virtual host routing melalui HTTP `Host` header dengan exact host dan wildcard ringkas `*.domain`
- Reverse proxy virtual host baseline untuk upstream `http://` dan `https://`, multi-upstream round-robin asas, retry/failover asas, dan WebSocket reverse proxy tunneling
- Process-local passive reverse proxy circuit breaker dengan threshold/cooldown SQLite
- Server-side script virtual host declaration yang pulang `501 Not Implemented` sehingga runtime bundled sebenar diintegrasi
- Static file serving dari direktori `public/`
- Request handler pipeline inspired by Proxygen-style handler/factory/transaction separation
- SQLite-backed protocol enable flags untuk HTTP/1.1, partial h2c/TLS ALPN `h2` HTTP/2 serving, dan HTTP/3 status/control-plane
- HTTP/2 frame parser/serializer, SETTINGS payload parser/serializer, dan HPACK static-table/literal-no-Huffman baseline dengan literal incremental decode tanpa dynamic-table persistence
- Partial HTTP/2 request serving dalam server untuk cleartext h2c dan TLS ALPN `h2`: parse client preface, SETTINGS, PING, HEADERS, DATA; dispatch request ke shared handler pipeline; reply HTTP/2 HEADERS/DATA
- HTTP/3 QUIC varint parser/serializer, frame parser/serializer, dan SETTINGS payload parser/serializer

Planned:

- HTTP/2 production stream/session lifecycle, full HPACK behavior, continuation assembly, real-client automation, and flow control. Candidate library for full session support: `nghttp2`. Needs verification.
- HTTP/3 live UDP/QUIC serving, TLS 1.3 QUIC handshake, QPACK, and stream/session lifecycle. Candidate: `ngtcp2` + `nghttp3` atau `quiche`. Needs verification.
- ALPN `h3` selepas HTTP/3 live serving siap dan diuji. Needs verification.
- Bundled PHP, Python, Perl, atau runtime server-side lain. Needs verification.
- First GitHub Actions CI run for `.github/workflows/ci.yml`. Needs verification.
- Benchmark dan kemungkinan upgrade kepada `io_uring` atau event abstraction sendiri. Needs verification.

Not present:

- ORM
- Web framework
- Container/deployment manifest

## Repository Structure

```text
.
|-- .github/
|   `-- workflows/
|       `-- ci.yml
|-- AGENTS.md
|-- CMakeLists.txt
|-- LICENSE
|-- Makefile
|-- README.md
|-- docs/
|   |-- ARCHITECTURE.md
|   |-- DECISIONS.md
|   |-- PROGRESS.md
|   |-- PROJECT_CONTEXT.md
|   |-- TODO.md
|   `-- plans/
|       |-- 001-project-structure.md
|       |-- 002-http2-http3-roadmap.md
|       |-- 003-proxygen-inspired-pipeline.md
|       |-- 004-sqlite-configuration.md
|       |-- 005-bundled-openssl-tls.md
|       |-- 006-http2-http3-support-system.md
|       |-- 007-async-nonblocking-io.md
|       |-- 008-http1-feature-expansion.md
|       |-- 009-security-tls-hardening.md
|       |-- 010-sni-ipv6-security-header-config.md
|       |-- 011-virtual-host-reverse-proxy-server-side-runtimes.md
|       |-- 012-reverse-proxy-upstream-pools-https-retry.md
|       |-- 013-websocket-reverse-proxy-tunnel.md
|       |-- 014-reverse-proxy-passive-circuit-breaker.md
|       |-- 015-bundled-zlib-sqlite.md
|       |-- 016-bundled-glibc-source-static.md
|       |-- 017-http2-http3-wire-codecs.md
|       |-- 018-http2-h2c-request-serving.md
|       |-- 019-http2-tls-alpn-h2.md
|       |-- 020-modsecurity-owasp-crs-built-in-waf.md
|       |-- 021-ordered-update-checklist.md
|       `-- README.md
|-- include/
|   `-- rimau/
|       |-- core/
|       |-- http/
|       `-- protocol/
|-- public/
|   `-- index.html
|-- scripts/
|   |-- generate-dev-cert.sh
|   |-- serve-dev-https.sh
|   |-- serve-dev.sh
|   `-- test.sh
|-- src/
|   |-- core/
|   |-- http/
|   |-- protocol/
|   `-- main.cpp
`-- tests/
    |-- test_cli_config.cmake
    |-- test_config_database.cpp
    |-- test_handler_pipeline.cpp
    |-- test_http1_session.cpp
    |-- test_http_parser.cpp
    |-- test_http1_network.py
    |-- test_http2_wire.cpp
    |-- test_http3_wire.cpp
    |-- test_http_response.cpp
    |-- test_protocol_capabilities.cpp
    |-- test_virtual_host.cpp
    `-- test_waf.cpp
```

## Main Modules

- `rimau::core::Server`: Mencipta worker pool berasaskan CPU core, memasang signal handler, dan menjalankan Linux `epoll` reactor per worker.
- `rimau::core::ServerConfig`: Konfigurasi host, port, document root, request limit, timeout, security limits, backlog, server name, protocol flags, TLS settings, SNI certificate map, IP lists, dan security header values yang dibaca daripada SQLite.
- `rimau::core::load_config_from_database`: Bootstrap dan baca jadual SQLite `rimau_config`.
- `rimau::core::config_schema_version`: Bootstrap metadata SQLite dan laporkan versi skema config semasa.
- `rimau::core::set_config_value`: Tulis nilai config disokong ke SQLite.
- `rimau::core::Server`: Mencipta dan reload TLS context termasuk SNI certificate contexts apabila `tls_enabled=true`.
- `rimau::http::next_http1_request_frame`: HTTP/1.1 buffered framing untuk headers, `Content-Length`, chunked transfer decoding, request pipelining boundary, dan framing error tanpa socket event loop.
- `rimau::http::parse_request`: Parser asas HTTP/1.0 dan HTTP/1.1 untuk request line, headers, URL-decoded path, query params, dan buffered body.
- `rimau::http::Request`: Request object yang menyimpan body kecil dalam memori dan boleh merujuk body besar dalam fail sementara melalui `RequestBodyFile`, `body_size()`, `body_spooled_to_file()`, dan `body_text()`.
- `rimau::http::file_response`: Static file response untuk GET dan HEAD, termasuk MIME type, single range, dan gzip untuk content compressible.
- `rimau::http::RequestHandler`: Interface untuk logic request per transaction.
- `rimau::http::RequestHandlerFactory`: Factory untuk membina handler bagi setiap request.
- `rimau::http::ResponseSink`: Interface downstream untuk menghantar response tanpa mengikat handler kepada HTTP/1.1 socket.
- `rimau::http::ResponseBuilder`: Helper kecil untuk membina response.
- `rimau::http::Transaction`: Unit request-response yang dispatch request kepada handler.
- `rimau::http::StaticFileHandler`: Handler terminal untuk static file.
- `rimau::http::VirtualHostHandlerFactory`: Memilih handler berdasarkan `Host` header dan SQLite `virtual_hosts`; fallback kepada global `document_root`.
- `rimau::http::parse_virtual_host_rules`: Parser config virtual host `host=static:path`, `host=proxy:http://upstream,https://backup`, dan `host=script:runtime:path`.
- `rimau::http::ReverseProxyHandler` dalam `src/http/virtual_host.cpp`: Baseline buffered reverse proxy untuk upstream `http://` dan `https://`, multi-upstream round-robin asas, dan retry/failover asas.
- `rimau::http::reverse_proxy_upstream_available` dan rekod success/failure dalam `src/http/virtual_host.cpp`: Passive circuit breaker in-memory untuk upstream reverse proxy.
- `rimau::http::inspect_request` dalam `src/http/waf.cpp`: WAF terbina dalam yang memeriksa request HTTP terhadap subset rules ModSecurity/OWASP CRS-inspired dan menghasilkan anomaly score.
- `rimau::http::waf_block_response` dalam `src/http/waf.cpp`: Response `403 Forbidden` apabila WAF blocking mode aktif dan score mencapai threshold.
- `rimau::core::ClientConnection` dalam `src/core/server.cpp`: State machine non-blocking untuk HTTP/1.1, partial cleartext h2c dan TLS ALPN `h2` HTTP/2 request serving, optional TLS, WebSocket echo tempatan, dan WebSocket reverse proxy tunnel untuk proxy vhost.
- `rimau::http::ServerSideScriptHandler` dalam `src/http/virtual_host.cpp`: Placeholder explicit `501 Not Implemented` untuk runtime seperti `php`, `python`, dan `perl`.
- `rimau::core::Worker` dalam `src/core/server.cpp`: Per-worker listener, `epoll` fd, active connection map, dan connection pool.
- `rimau::protocol::protocol_capabilities`: Senarai status protokol berasaskan config SQLite. Ia bezakan `implemented` dan `configured`.
- `rimau::protocol::http2::parse_frame` dan `serialize_frame`: HTTP/2 frame codec asas.
- `rimau::protocol::http2::hpack_encode_header_block` dan `hpack_decode_header_block`: HPACK baseline untuk static table, literal string tanpa Huffman, dan literal incremental decode tanpa dynamic-table persistence.
- `rimau::protocol::http3::decode_varint`, `encode_varint`, `parse_frame`, dan `serialize_frame`: HTTP/3/QUIC wire primitive codec asas.

## Important Commands

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Shortcut:

```bash
make test
./scripts/test.sh
```

`ctest` sahaja dari root source directory tidak discover test CMake kerana test files berada dalam `build/`.

Validate config:

```bash
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
```

Check protocol status:

```bash
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
```

Configure virtual hosts:

```bash
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
```

Check bundled OpenSSL:

```bash
build/_deps/openssl/install/bin/openssl version
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
```

Run server:

```bash
./build/rimau-server --database data/rimau.sqlite3
```

Run dev server pada port 18080:

```bash
make serve
```

atau:

```bash
./scripts/serve-dev.sh
```

`make serve` dan `./scripts/serve-dev.sh` menjalankan server di foreground; terminal perlu dibiarkan terbuka.

Run dev server sebagai background process:

```bash
make start
make status
make stop
```

Run dev HTTPS server:

```bash
make start-https
curl -k -I https://127.0.0.1:18443/
make stop-https
```

Manual HTTP check:

```bash
curl -i http://127.0.0.1:8080/
```

Manual dev server check:

```bash
make start
curl -i http://127.0.0.1:18080/
```

## Development Process

1. Baca `AGENTS.md` dan semua dokumen memori projek yang relevan.
2. Semak struktur repo dengan `rg --files`.
3. Jika Git sudah diinisialisasi, semak `git status --short --branch`.
4. Buat perubahan kecil dan jelas.
5. Build dan test.
6. Kemas kini dokumen memori projek.
7. Rekod limitation dan TODO baru secara jujur.

## Deployment Process

Belum wujud deployment production.

Current local run:

```bash
cmake -S . -B build
cmake --build build
./build/rimau-server --database data/rimau.sqlite3
```

License file exists as GPL-3.0 from the target GitHub repository. Needs verification that GPL-3.0 is the intended project license.

GitHub remote: `https://github.com/asklinux/riwau-webserver`. Current source was pushed to remote `main` on 2026-07-20. The local working directory `/home/data/tunnelbiz/rimauwebserver` is now initialized as a Git repository on branch `main` with `origin` pointing to the GitHub remote.

Production deployment, service manager, packaging, container, TLS certificate handling, log rotation, dan privilege dropping: Needs verification.

## Existing Features

- Binary CLI `rimau-server`.
- SQLite-backed runtime configuration.
- Config update command melalui `--set key=value`.
- HTTPS/TLS untuk HTTP/1.1 melalui bundled static OpenSSL.
- TLS 1.2 dan TLS 1.3 dengan cipher list/ciphersuites yang dikawal SQLite.
- SNI validation dan multi-certificate SNI selection melalui `tls_sni_certificates`.
- ALPN callback yang mengiklankan `http/1.1` dan boleh mengiklankan `h2` hanya apabila `http2_enabled=true` dan `tls_alpn_protocols` mengandungi `h2`.
- SIGHUP TLS context reload untuk certificate/key/TLS settings bagi sambungan baharu.
- Dev certificate generation melalui bundled OpenSSL binary.
- HTTP/1.1 static file serving untuk GET dan HEAD.
- HTTP method support untuk GET, POST, PUT, PATCH, DELETE, OPTIONS, dan HEAD.
- Content-Length request body parsing.
- Chunked transfer decoding untuk request body.
- HTTP/1.1 large request body spooling ke fail sementara dengan threshold memori 16 KiB sebelum request dispatch.
- Request pipelining asas: request seterusnya boleh dibuffer dan diproses selepas response sebelumnya ditulis.
- URL decoding dan query parameter parsing.
- JSON request detection dan JSON response untuk method body scaffold.
- Basic MIME type detection termasuk text, image, video, wasm, dan pdf types umum.
- Range request single-range untuk static files.
- Gzip compression untuk static response yang compressible apabila client menghantar `Accept-Encoding: gzip`.
- Basic WebSocket upgrade dan echo dua hala untuk text/binary frames, ping/pong, dan close untuk host bukan proxy.
- WebSocket reverse proxy tunneling untuk proxy vhost dengan upstream `http://` atau `https://`.
- Basic path traversal protection.
- Static file symlink escape protection melalui canonical path checks.
- Request size limit untuk buffered HTTP message.
- Request smuggling protection untuk duplicate/invalid `Content-Length`, konflik `Content-Length`/`Transfer-Encoding`, duplicate/unsupported `Transfer-Encoding`, obs-fold, dan bare CR/LF.
- Header injection dan HTTP response splitting protection melalui validation/sanitization.
- Request, header, body, dan idle timeout.
- Slowloris protection asas melalui header/body timeout.
- Global connection limit dan per-IP connection limit.
- Per-IP fixed-window rate limiting.
- IPv4/IPv6 exact/CIDR IP allowlist dan blocklist.
- WebSocket max frame size limit.
- SQLite-configurable default security headers dan ability untuk disable `Server` header.
- Virtual host routing dengan exact host dan wildcard ringkas `*.domain`.
- Static virtual hosts dengan document root khusus host.
- Baseline reverse proxy virtual host untuk upstream HTTP dan HTTPS.
- Multi-upstream reverse proxy dengan round-robin asas dan retry/failover asas.
- Passive reverse proxy circuit breaker in-memory per process dengan threshold/cooldown SQLite.
- Built-in ModSecurity-compatible WAF untuk HTTP/1.1, WebSocket upgrade, WebSocket proxy upgrade, dan partial HTTP/2 request path. Rule set semasa ialah subset OWASP CRS-inspired yang dikompil dalam kod Rimau, bukan full `libmodsecurity` atau full OWASP CRS.
- Script virtual host declaration dengan runtime name seperti `php`, `python`, atau `perl`; execution belum implemented dan response semasa ialah `501`.
- HTTP/1.1 keep-alive.
- SIGHUP reload untuk dynamic SQLite config seperti `document_root`, `max_request_bytes`, HTTP keep-alive settings, timeout, security limit, IP list, dan TLS certificate/key/TLS settings untuk sambungan baharu; listener dan worker changes masih memerlukan restart.
- Proxygen-inspired handler/factory/transaction/response-sink pipeline.
- Parser test asas melalui CTest.
- Response serializer test asas melalui CTest.
- Handler pipeline test asas melalui CTest.
- HTTP/1.1 session/framing test melalui CTest tanpa socket event loop.
- HTTP/1.1 network integration test melalui CTest untuk keep-alive, max request cap, idle timeout, pipelining, chunked body, range, gzip, WebSocket echo, dan WebSocket proxy.
- SQLite config database test asas melalui CTest.
- CLI integration test melalui CTest untuk `--database`, `--set`, `--check-config`, dan `--protocols`.
- Protocol capability test asas melalui CTest.
- Protocol status command.
- SQLite protocol flags: `http1_enabled`, `http2_enabled`, dan `http3_enabled`.
- Linux `epoll` reactor architecture untuk HTTP/1.1 TCP dan HTTPS.
- Worker pool berasaskan CPU core melalui `std::jthread`.
- `SO_REUSEADDR`, `SO_REUSEPORT`, TCP keepalive, graceful SIGTERM/SIGINT shutdown, dan SIGHUP live reload terhad.
- Fully static Linux x86_64 `rimau-server` build menggunakan bundled source glibc sysroot.
- GitHub Actions workflow `.github/workflows/ci.yml` for fast Linux build/test with bundled OpenSSL, SQLite, and zlib; bundled glibc is disabled in CI fast path to avoid long PR feedback cycles.

## Current Implementation Status

- HTTP/1.1: Partial; body besar boleh discroll ke fail sementara, tetapi handler-level streaming/backpressure API belum lengkap.
- HTTP/2: Partial h2c dan TLS ALPN `h2` request serving; frame codec, SETTINGS/PING, HEADERS/DATA path, HPACK baseline, and shared handler pipeline dispatch exist. Continuation assembly, full HPACK dynamic/Huffman behavior, and flow control masih Planned.
- HTTP/3: Partial wire codec primitives; UDP/QUIC/QPACK/live request serving masih Planned.
- TLS: Partial for HTTP/1.1 HTTPS and HTTP/2 ALPN `h2` basics with TLS 1.2/1.3, SNI validation, multi-certificate SNI selection, ALPN `http/1.1`/`h2`, safe cipher config, and new-connection certificate reload.
- Event loop performance architecture: Partial with Linux `epoll` backend.
- Virtual hosts: Partial; exact host, simple wildcard, static document root, proxy route, and script declaration are implemented.
- Reverse proxy: Partial; HTTP/HTTPS upstream, buffered response untuk HTTP biasa, WebSocket tunnel untuk proxy vhost, round-robin asas, retry/failover asas, dan passive circuit breaker are implemented.
- WAF/ModSecurity: Partial; SQLite-configurable built-in ModSecurity-compatible WAF with OWASP CRS-inspired subset rules is implemented. Full `libmodsecurity` integration and full OWASP Core Rule Set bundle remain planned. Needs verification.
- Server-side language runtimes: Planned; declarations exist but PHP/Python/Perl execution is not implemented.
- Access log format: Partial through stderr logger only.
- Config reload: Partial for restart-free dynamic SQLite values.
- SQLite config: Partial.
- Bundled dependency build: Partial; OpenSSL, SQLite, zlib, Bison, Linux UAPI headers, dan glibc source builds are implemented for current Linux x86_64 validation.
- Proxygen-inspired request pipeline: Partial.

## Known Bugs And Limitations

- Server menggunakan Linux `epoll` multi-worker, tetapi masih belum ada benchmark prestasi atau zero-copy static file path.
- Bind config menyokong IPv4 atau IPv6 literal, tetapi belum ada hostname resolution atau dual listener eksplisit untuk bind IPv4 dan IPv6 serentak.
- Request body besar untuk HTTP/1.1 boleh discroll ke fail sementara sebelum handler dispatch, tetapi belum ada handler-level streaming API, response chunking, atau reverse proxy request-body streaming/backpressure.
- Chunked trailers dibaca untuk tamat message tetapi belum didedahkan kepada handler.
- JSON request belum diparse kepada DOM/structured object; body JSON hanya dikesan melalui `Content-Type` dan diecho sebagai string selamat.
- PUT/PATCH/DELETE tidak memutasi fail; scaffold semasa pulang JSON metadata/body untuk method tersebut.
- Request pipelining asas sudah ada, tetapi belum ada stress/integration test luas untuk ordering/backpressure.
- Range request hanya single range; multipart byte ranges belum implemented.
- Compression hanya gzip melalui bundled static zlib; Brotli belum implemented.
- `rimau-server` semasa validated sebagai static ELF: `ldd build/rimau-server` melaporkan `not a dynamic executable`, dan `readelf` tidak menunjukkan dynamic interpreter. Caveat: static glibc DNS/NSS untuk `getaddrinfo`/`gethostbyname` masih memberi linker warning dan perlu ujian production tambahan, terutama reverse proxy upstream hostname. Needs verification.
- HTTP/2 support baru melayan request asas melalui cleartext h2c prior knowledge dan TLS ALPN `h2`; full multiplexing semantics, continuation assembly, HPACK Huffman, HPACK dynamic table persistence, dan flow control penuh belum implemented.
- HTTP/3 support belum live di network; codec semasa hanya QUIC varint/frame/SETTINGS primitives tanpa QUIC transport atau QPACK.
- WebSocket support masih basic; local echo belum menyokong fragmentation, extensions, subprotocol negotiation, backpressure policy khusus, atau application routing. WebSocket proxy melakukan tunnel stream, tetapi tidak memeriksa frame WebSocket secara lengkap untuk fragment/subprotocol policy.
- Reverse proxy HTTP biasa menggunakan `getaddrinfo` dan `poll()` dalam handler, membuffer response dalam memori, dan belum ada streaming, advanced load balancing, active health check scheduler, atau upstream connection pooling. WebSocket proxy data path sudah didaftarkan dalam worker `epoll`, tetapi connect/DNS/handshake upstream masih dibuat sebelum tunnel bermula. Circuit breaker semasa pasif, in-memory per process, dan tidak distributed.
- HTTPS upstream menggunakan bundled OpenSSL untuk transport TLS. `reverse_proxy_tls_verify_upstream=true` menghidupkan verification melalui default trust paths, tetapi per-upstream CA/pinning/verify-depth policy belum siap. Needs verification.
- Script virtual host untuk PHP/Python/Perl belum menjalankan interpreter atau VM. Memanggil interpreter sistem akan melanggar syarat no external runtime dependency; runtime bundled perlu keputusan dan integrasi berasingan. Needs verification.
- TLS request serving implemented untuk HTTP/1.1 dan partial HTTP/2 ALPN `h2`; ALPN `h3` belum diiklankan kerana HTTP/3 live request serving belum implemented.
- TLS certificate/key reload hanya untuk sambungan baharu; sambungan TLS sedia ada terus menggunakan context lama.
- Rate limiting dan connection counters adalah in-memory per process, bukan distributed.
- WAF semasa ialah signature/anomaly engine ringkas terbina dalam. Ia bukan full ModSecurity transaction engine, tidak membaca syntax rule ModSecurity sebenar, tidak bundle full OWASP Core Rule Set, tiada phase engine lengkap, tiada persistent audit log berstruktur, dan rule tuning/exception per virtual host belum implemented. Full ModSecurity/libmodsecurity + OWASP CRS integration remains Needs verification.
- Security header names masih fixed set; nilainya boleh dikonfigurasi atau dikosongkan melalui SQLite.
- SIGHUP reload tidak menukar listener bind, worker count, HTTP/1 enablement, TLS enabled mode, atau connection pool sizing; ubah nilai tersebut memerlukan restart.
- Dev certificate self-signed tidak sesuai untuk production.
- HTTP/2 belum production-complete; `http2_enabled` mengaktifkan partial cleartext h2c request serving dan partial TLS ALPN `h2` serving apabila `tls_alpn_protocols` mengandungi `h2`. HTTP/3 belum implemented sebagai live request-serving protocol; `http3_enabled` masih status/config gate buat masa ini.
- MIME mapping masih minimum.
- SQLite config schema kini ada metadata version table v1, tetapi belum ada framework multi-step migration, downgrade policy, backup policy, atau admin UI.
- Tiada admin UI/API untuk ubah config; buat masa ini guna CLI `--set`.
- Tiada benchmark.
- Tiada fuzzing parser.
- Tiada production hardening seperti chroot, user switching, seccomp, distributed rate limiting, atau advanced slow-client scoring.

## Tests And Validation Commands

Primary:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-http2-wire-tests
./build/rimau-http3-wire-tests
./build/rimau-waf-tests
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
```

Manual smoke:

```bash
make serve
curl -i http://127.0.0.1:18080/
```

Manual HTTP/2 smoke uses temporary SQLite databases with `http2_enabled=true`: h2c raw socket clients send HTTP/2 preface, SETTINGS, HEADERS, and optional DATA frames; TLS smoke negotiates ALPN `h2` with a Python SSL client and sends the same HTTP/2 frame sequence. See `docs/plans/018-http2-h2c-request-serving.md` and `docs/plans/019-http2-tls-alpn-h2.md` for validated scenarios.

Documentation check:

```bash
rg "Needs verification|Planned|Partial|Implemented" docs AGENTS.md
```
