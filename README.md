# Rimau Web Server

Rimau Web Server ialah projek C++ untuk membina web server berprestasi tinggi dengan sasaran jangka panjang setanding kelas OpenLiteSpeed dan nginx.

Status semasa: scaffold awal yang sudah mempunyai HTTP/1.1 praktikal. HTTP/1.1 menyokong GET/HEAD static serving, OPTIONS, POST/PUT/PATCH/DELETE JSON scaffold, Content-Length, chunked request decoding, keep-alive, basic pipelining, single/multipart range responses, gzip static compression, basic WebSocket echo, WebSocket reverse proxy tunneling untuk proxy vhost, TLS hardening asas, timeout, rate limit, kawalan connection/IP, built-in ModSecurity-compatible WAF subset dengan per-host overrides, virtual host static, dan reverse proxy HTTP/1.1 buffered dengan HTTP/HTTPS upstream I/O melalui worker `epoll`, multi-upstream `round_robin`/`failover`/`stable_hash`, retry/failover asas, dan passive circuit breaker. HTTP/2 kini implemented untuk native cleartext h2c prior-knowledge dan TLS ALPN `h2` request serving melalui shared handler pipeline, dengan HPACK Huffman/dynamic-table decode, CONTINUATION assembly, stream lifecycle asas, inbound flow-control accounting, dan real-client curl tests. HTTP/3 kini partial pada codec primitive: QUIC varint, frame, dan SETTINGS payload parser/serializer sudah ada, tetapi UDP/QUIC/QPACK/live request serving belum siap.

Seni bina request pipeline mengambil inspirasi konsep daripada Proxygen, tetapi kod Proxygen tidak disalin ke projek ini.

Runtime configuration dibaca daripada SQLite, bukan fail config key-value.
SQLite engine dibina terus ke binary melalui bundled static SQLite 3.53.3; runtime tidak link kepada `libsqlite3` sistem.

Runtime server menggunakan Linux `epoll` reactor dengan worker thread berdasarkan CPU core. Setiap worker mempunyai event loop sendiri, listener non-blocking sendiri, dan pengagihan connection melalui `SO_REUSEPORT`. TLS handshake/read/write juga diproses melalui state machine non-blocking.

HTTP/1.1 keep-alive disokong untuk request dengan framing header-only, `Content-Length`, atau `Transfer-Encoding: chunked`. SIGHUP boleh reload nilai SQLite tertentu seperti `document_root`, request size limit, keep-alive settings, timeout, rate limit, IP list, virtual host/proxy settings, TCP keepalive settings untuk connection baru, protocol status flags, TLS certificate/key/settings untuk connection baru, dan graceful shutdown timeout tanpa restart proses.

TLS/SSL dibina terus ke Rimau melalui bundled static OpenSSL. CMake memuat turun dan membina OpenSSL khusus untuk Rimau; runtime tidak link kepada `libssl` atau `libcrypto` sistem. TLS semasa menyokong TLS 1.2/1.3, SNI validation, multi-certificate SNI selection, ALPN `http/1.1` dan `h2`, configurable cipher list/ciphersuites, reload certificate/key untuk sambungan baharu, dan production certificate management guide. OCSP stapling tidak disokong setakat ini.

Gzip response compression menggunakan bundled static zlib 1.3.2; runtime tidak link kepada `libz` sistem.

Pada Linux x86_64 dengan GNU/Clang, CMake membina GNU glibc 2.43 daripada source asal, membina Linux UAPI headers 6.18.7 daripada source kernel.org, membina GNU Bison 3.8.2 sebagai build tool glibc, dan link `rimau-server` sebagai static ELF melalui sysroot bundled tersebut. CTest `rimau_static_elf_checks` mengesahkan `ldd`, `file`, dan tiada `INTERP` untuk build static penuh.

## HTTP/1.1 Notes

- Static files dibaca dari SQLite `document_root`.
- POST, PUT, PATCH, dan DELETE belum memutasi fail; handler semasa pulang JSON metadata/body scaffold.
- Range support menyokong single dan multipart byte ranges dengan `If-Range`.
- Compression semasa ialah gzip sahaja; Brotli belum implemented.
- WebSocket semasa ialah basic RFC 6455 echo untuk host bukan proxy, dan reverse proxy tunnel untuk proxy vhost HTTP/HTTPS upstream. Tiada fragmentation handling penuh, extensions, subprotocol routing tempatan, atau application routing.
- Request body besar untuk HTTP/1.1 boleh discroll ke fail sementara sebelum handler dispatch dan dibaca melalui `RequestBodyReader`; live streaming sebelum dispatch, producer-side response streaming, dan zero-copy static file path belum implemented.
- Request smuggling checks menolak invalid/duplicate `Content-Length`, konflik `Content-Length`/`Transfer-Encoding`, duplicate/unsupported `Transfer-Encoding`, obs-fold, dan bare CR/LF.
- Security controls termasuk request/header/body/idle timeout, global/per-IP connection limit, fixed-window per-IP rate limit, built-in ModSecurity-compatible WAF subset, IPv4/IPv6 exact/CIDR allowlist/blocklist, SQLite-configurable security header values, dan `server_header_enabled=false`.
- WAF semasa ialah subset OWASP CRS-inspired yang dikompil dalam kod Rimau untuk scanner user-agent, request splitting/CRLF, path traversal, XSS, SQLi, RCE, PHP wrapper injection, dan Java/JNDI exploit patterns. Ini bukan full `libmodsecurity` dan bukan full OWASP Core Rule Set.
- Virtual host memilih handler melalui HTTP `Host` header dengan exact host dan wildcard ringkas `*.domain`.
- Reverse proxy semasa menyokong upstream `http://` dan `https://`, membaca response HTTP biasa secara buffered, menjalankan HTTP/1.1 normal upstream I/O melalui worker `epoll`, WebSocket proxy tunnel dua hala, dan mempunyai multi-upstream `round_robin`, `failover`, atau `stable_hash`, retry/failover asas, serta passive circuit breaker. Ia belum mempunyai HTTP/2 async proxy adapter, certificate verification policy production-ready, active health check scheduler, streaming request/response HTTP biasa, atau connection pooling.
- Route server-side script boleh dideklarasi seperti `app.test=script:php:public/app`, tetapi runtime PHP/Python/Perl belum dibundle dan belum dieksekusi. Handler semasa pulang `501 Not Implemented` dengan `x-rimau-runtime-status: planned`, dan Rimau tidak shell out kepada interpreter sistem.

## HTTP/2 And HTTP/3 Notes

- HTTP/2 frame parser/serializer menyokong header 9-byte, frame length, type, flags, stream id, SETTINGS payload, dan validasi untuk SETTINGS, PING, GOAWAY, WINDOW_UPDATE, RST_STREAM, DATA, HEADERS, dan CONTINUATION.
- HPACK HTTP/2 menyokong static table, Huffman string decode, dynamic table insert/reference, dan dynamic table size update.
- Jika `http2_enabled=true`, cleartext h2c prior-knowledge dan TLS ALPN `h2` client preface diproses melalui native `ServerSession`: Rimau parse client SETTINGS, balas SETTINGS/ACK, parse HEADERS/CONTINUATION/DATA, dispatch ke pipeline `Transaction`, dan serialize response sebagai HTTP/2 HEADERS/DATA frame.
- CTest `rimau_http2_h2c_curl` dan `rimau_tls_alpn_h2_curl` menggunakan real `curl` HTTP/2 client untuk mengesahkan h2c dan TLS ALPN `h2` boleh fetch response sebenar.
- HTTP/3 codec primitive menyokong QUIC varint, frame parser/serializer, dan SETTINGS payload. Ini belum termasuk UDP listener, QUIC transport, TLS 1.3 QUIC handshake, QPACK, stream lifecycle, atau request adapter.
- ALPN boleh mengiklankan `http/1.1` dan `h2` apabila `http2_enabled=true` serta `tls_alpn_protocols` mengandungi `h2`. `h3` masih ditolak dalam config sehingga HTTP/3 request serving siap dan diuji.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R rimau_http_fuzz
ctest --test-dir build --output-on-failure -R rimau_static_elf_checks
ctest --test-dir build --output-on-failure -R rimau_http2_h2c_curl
ctest --test-dir build --output-on-failure -R rimau_tls_alpn_h2_curl
ctest --test-dir build --output-on-failure -R rimau_tls_sni_cert_selection
```

Build pertama memerlukan network untuk source archive yang dipin dalam `CMakeLists.txt` kecuali cache CMake sudah ada. Build-time masih memerlukan compiler dan tool asas host seperti `gcc`/`g++`, `make`, `perl`, `gawk`, `msgfmt`, `python3`, dan `m4`; sasaran yang dibuang ialah runtime shared-library dependency untuk binary deployable. Bundled glibc semasa hanya wired untuk Linux x86_64.

Shortcut dari root projek:

```bash
make test
./scripts/test.sh
```

Nota: `ctest` sahaja dari root source directory akan papar `No tests were found!!!` kerana CTest test files dijana dalam `build/`.

## Run

```bash
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --database data/rimau.sqlite3
```

Jika port 8080 sedang digunakan, guna dev server default pada port 18080:

```bash
make serve
curl -i http://127.0.0.1:18080/
```

`make serve` menjalankan server di foreground. Biarkan terminal itu terbuka semasa test dengan browser atau `curl`.

Untuk run sebagai background process:

```bash
make start
make status
curl -i http://127.0.0.1:18080/
make stop
```

## HTTPS

Dev HTTPS menggunakan sijil self-signed yang dijana oleh OpenSSL bundled:

```bash
make start-https
curl -k -I https://127.0.0.1:18443/
make stop-https
```

Production certificate path, permission, reload, rotation, dan rollback guidance ada di `docs/plans/022-production-certificate-management.md`. OCSP stapling tidak disokong; ADR-0039 menangguhkan fungsi itu sehingga ada design penuh.

OpenSSL bundled dipin kepada `openssl-4.0.1`, zlib kepada `1.3.2`, SQLite kepada `3.53.3`, glibc kepada `2.43`, GNU Bison kepada `3.8.2`, dan Linux UAPI headers kepada source kernel `6.18.7` dalam `CMakeLists.txt`. Untuk update dependency khusus Rimau, ubah versi/URL/SHA256 di CMake, kemudian rebuild.

Nota deployment: static glibc masih memberi caveat DNS/NSS. Linker memberi amaran bahawa penggunaan hostname melalui `getaddrinfo`/`gethostbyname` dalam binary statik boleh memerlukan shared NSS modules daripada versi glibc yang sama pada runtime. Reverse proxy kepada alamat IP literal ialah path paling jelas setakat ini; hostname upstream production memerlukan ujian tambahan. Needs verification.

Atau:

```bash
./scripts/serve-dev.sh
```

## Configure

SQLite database default:

```text
data/rimau.sqlite3
```

Server akan bootstrap jadual `rimau_config` dan nilai default jika database belum wujud.

Contoh ubah config:

```bash
./build/rimau-server --database data/rimau.sqlite3 --set host=127.0.0.1
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
# Untuk TLS HTTP/2: set http2_enabled=true dan tls_alpn_protocols=h2,http/1.1
./build/rimau-server --database data/rimau.sqlite3 --set "tls_sni_certificates=api.example.test=certs/api.crt|certs/api.key;*.tenant.test=certs/tenant.crt|certs/tenant.key"
./build/rimau-server --database data/rimau.sqlite3 --set request_timeout_seconds=30
./build/rimau-server --database data/rimau.sqlite3 --set header_timeout_seconds=10
./build/rimau-server --database data/rimau.sqlite3 --set body_timeout_seconds=30
./build/rimau-server --database data/rimau.sqlite3 --set idle_timeout_seconds=15
./build/rimau-server --database data/rimau.sqlite3 --set global_connection_limit=10000
./build/rimau-server --database data/rimau.sqlite3 --set per_ip_connection_limit=100
./build/rimau-server --database data/rimau.sqlite3 --set rate_limit_requests_per_minute=600
./build/rimau-server --database data/rimau.sqlite3 --set ip_allowlist=127.0.0.1,::1,10.0.0.0/8,2001:db8::/32
./build/rimau-server --database data/rimau.sqlite3 --set websocket_max_frame_bytes=65536
./build/rimau-server --database data/rimau.sqlite3 --set security_headers_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set "security_header_content_security_policy=default-src 'self'; frame-ancestors 'self'; base-uri 'self'"
./build/rimau-server --database data/rimau.sqlite3 --set security_header_x_frame_options=SAMEORIGIN
./build/rimau-server --database data/rimau.sqlite3 --set server_header_enabled=false
./build/rimau-server --database data/rimau.sqlite3 --set virtual_hosts_enabled=true
./build/rimau-server --database data/rimau.sqlite3 --set "virtual_hosts=site.test=static:public/site;api.test=proxy:http://127.0.0.1:19090,https://127.0.0.1:19443;app.test=script:php:public/app"
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_connect_timeout_seconds=5
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_read_timeout_seconds=30
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_max_response_bytes=1048576
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_retry_count=1
./build/rimau-server --database data/rimau.sqlite3 --set reverse_proxy_load_balancing_policy=round_robin
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
./build/rimau-server --database data/rimau.sqlite3 --protocols
```

## Virtual Hosts And Reverse Proxy

`virtual_hosts` ialah senarai dipisahkan `;`:

```text
host=static:document-root;host=proxy:http://upstream:port/base,https://backup:port/base;host=script:runtime:script-root
```

Contoh:

```bash
./build/rimau-server --database data/rimau.sqlite3 --set "virtual_hosts=site.test=static:public/site;*.tenant.test=static:public/tenant;api.test=proxy:http://127.0.0.1:9000/api,https://127.0.0.1:9443/api;app.test=script:php:public/app"
```

`virtual_host_waf_overrides` juga dipisahkan `;` dan guna option dipisahkan koma:

```bash
./build/rimau-server --database data/rimau.sqlite3 --set "virtual_host_waf_overrides=site.test=enabled:false;api.test=threshold:10,rule_exceptions:930100|942100;*.tenant.test=blocking:false"
```

Jika `modsecurity_audit_log_enabled=true`, setiap WAF match menghasilkan event `rimau_waf_audit` berstruktur melalui stderr logger. Event ini tidak menulis raw body, query string, header values, cookie, authorization, atau WAF evidence. Rotation/retention perlu dibuat oleh systemd-journald, container log driver, atau logrotate sehingga audit sink khusus ditambah.

Static vhost melayan fail dari document root host tersebut. Proxy vhost untuk HTTP/1.1 menghantar request kepada upstream HTTP/1.1 melalui worker `epoll` dan menyalin response buffered kembali kepada client. Jika request ialah WebSocket Upgrade, Rimau membuat upstream WebSocket handshake dan kemudian tunnel data dua hala melalui worker `epoll` yang sama. Jika beberapa upstream diberi, Rimau memilih mengikut `reverse_proxy_load_balancing_policy`: `round_robin`, `failover`, atau `stable_hash`, dan boleh retry ke upstream seterusnya mengikut `reverse_proxy_retry_count`. Upstream yang gagal berulang kali boleh dipintas sementara oleh passive circuit breaker dan akan menghasilkan `503 Service Unavailable` jika semua target terbuka circuit-nya. Script vhost hanya declaration buat masa ini dan akan pulang `501` sehingga runtime model sebenar diterima; Rimau tidak memanggil system `php`, `python`, `perl`, atau interpreter lain.

Nota keselamatan: HTTPS upstream menggunakan bundled OpenSSL untuk transport TLS. `reverse_proxy_tls_verify_upstream=true` menghidupkan verification menggunakan default trust paths; policy CA/per-upstream yang lebih lengkap masih kerja susulan.

`http2_enabled=true` mengaktifkan native cleartext h2c prior-knowledge request serving dan TLS ALPN `h2` serving apabila `tls_alpn_protocols=h2,http/1.1` atau senarai ALPN lain yang mengandungi `h2`. HTTP/2 P4 meliputi HPACK Huffman/dynamic-table decode, CONTINUATION assembly, stream lifecycle asas, inbound flow-control accounting, dan real-client h2c/TLS tests; broad multiplexing stress, outbound response flow-control scheduling, dan producer-side streaming masih kerja susulan. `http3_enabled=true` masih status/config untuk HTTP/3 live serving. Server belum mengiklankan ALPN `h3` dan belum mempunyai HTTP/3 UDP/QUIC listener.

## Documentation

Setiap sesi pembangunan wajib membaca `AGENTS.md` dan dokumen dalam `docs/` sebelum membuat perubahan. Dokumen tersebut ialah sistem memori projek dan perlu dikemas kini selepas perubahan sistem.
