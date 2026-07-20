# Plan 021: Ordered Update Checklist

Date: 2026-07-20

Gunakan checklist ini untuk sambung kerja satu demi satu. Jangan tanda `[x]` sehingga kod, test, dokumentasi, dan status sebenar sudah dikemas kini.

## Cara Guna

Untuk setiap item:

1. Pilih satu item sahaja.
2. Baca fail berkaitan dan plan lama yang relevan.
3. Implement secara kecil dan jelas.
4. Jalankan validasi minimum untuk area itu.
5. Update `docs/PROGRESS.md`, `docs/TODO.md`, `docs/ARCHITECTURE.md`, dan `docs/DECISIONS.md` jika reka bentuk berubah.
6. Jika publish diminta, commit dan push selepas validasi lulus.

## Phase 0: Repo And Automation Foundation

- [x] Initialize atau align workspace tempatan dengan `https://github.com/asklinux/riwau-webserver`.
  - Done criteria: `git status --short --branch` berfungsi dari `/home/data/tunnelbiz/rimauwebserver`.
- [x] Confirm license GPL-3.0 memang license projek.
  - Done criteria: `docs/DECISIONS.md` ada ADR/license note yang accepted atau jelas `Needs verification`.
  - Status: `LICENSE` contains GPL-3.0 text; ADR-0028 records final owner intent as `Needs verification`.
- [x] Tambah CI workflow file.
  - Done criteria: `.github/workflows/ci.yml` wujud dan local CMake configure dengan flag CI lulus.
- [x] Verify first GitHub Actions CI run.
  - Done criteria: GitHub Actions build CMake dan run CTest untuk PR/push selepas workflow dipush.
  - Status: GitHub Actions run `29717795357` passed on 2026-07-20 after fixing fast-CI glibc scheduling and GCC 11 shared_ptr atomic portability.
- [x] Tambah SQLite schema migration/version table.
  - Done criteria: config database mempunyai versioning dan test migration.
- [x] Tambah CLI integration tests untuk `--database`, `--set`, `--check-config`, dan `--protocols`.
  - Done criteria: test gagal jika config SQLite tidak bootstrap/update dengan betul.

## Phase 1: HTTP/1.1 Stabilization

- [x] Pisahkan HTTP/1.1 codec/session daripada `ClientConnection`.
  - Done criteria: parsing/framing HTTP/1.1 boleh diuji tanpa socket event loop.
- [x] Tambah integration tests untuk keep-alive, max request cap, idle timeout, pipelining, chunked body, range, gzip, WebSocket echo, dan WebSocket proxy.
  - Done criteria: semua behavior utama HTTP/1.1 ada coverage automatik.
- [x] Implement streaming request body dengan backpressure.
  - Done criteria: large upload tidak perlu buffer seluruh body dalam memori.
  - Status: File-backed large-body spool keeps large uploads out of full memory buffering, and handlers now have `RequestBodyReader` pull reads from memory or spool files. Live in-flight request body dispatch, reverse proxy request streaming, and explicit network-level backpressure remain future work.
- [x] Implement response chunking.
  - Done criteria: handler boleh hantar response streaming tanpa `Content-Length` awal.
  - Status: Basic HTTP/1.1 chunked response API/serialization implemented through `ResponseSink::send_chunked`; producer-side async streaming/backpressure remains future work.
- [x] Implement multipart range dan `If-Range`.
  - Done criteria: static file video/large file support lebih lengkap.
  - Status: Static file response supports single and multipart byte ranges, generated ETag/Last-Modified validators, and `If-Range` match handling.
- [x] Decide dan implement Brotli jika dependency/bundling diterima.
  - Done criteria: ADR wujud dan compression test lulus, atau keputusan rasmi untuk tidak implement.
  - Status: Decision accepted to defer Brotli in P1 because no bundled dependency/deployment approach has been accepted.
- [x] Tambah configurable directory index dan custom error page.
  - Done criteria: SQLite config dan test wujud.
  - Status: SQLite keys `directory_index` and `error_page` are implemented, validated, wired through static/vhost handlers, and covered by unit/network tests.

## Phase 2: Security And WAF Hardening

- [x] Tambah integration tests request smuggling.
  - Done criteria: duplicate/invalid `Content-Length`, TE/CL conflict, obs-fold, bare CR/LF, dan unsupported TE diuji.
  - Status: HTTP/1.1 network integration now rejects duplicate/invalid `Content-Length`, `Content-Length` plus `Transfer-Encoding`, unsupported `Transfer-Encoding`, obs-fold, bare LF, and bare CR while ensuring an appended smuggled request is not processed.
- [x] Tambah integration tests rate limit, connection limit, timeout, dan slow-client behavior.
  - Done criteria: behavior keselamatan runtime terbukti end-to-end.
  - Status: HTTP/1.1 network integration now covers fixed-window rate limiting, per-IP and global connection limit rejection, and request/header/body/idle timeout slow-client behavior using SQLite-configured limits.
- [x] Tambah WAF integration tests untuk HTTP/1.1, WebSocket upgrade, WebSocket proxy upgrade, dan partial HTTP/2.
  - Done criteria: WAF block path diuji di semua entry point.
  - Status: HTTP/1.1 network integration now enables the built-in WAF and verifies blocking for normal HTTP/1.1, local WebSocket upgrade, WebSocket proxy vhost upgrade, and partial h2c request-serving paths.
- [x] Bina WAF false-positive regression corpus.
  - Done criteria: traffic normal browser/curl tidak diblock oleh rule default.
  - Status: `rimau_waf` now includes a structured false-positive corpus for normal curl, browser navigation/static asset, search, JSON API, form, and WebSocket upgrade traffic; each case must stay allowed with no WAF matches under default blocking WAF settings.
- [x] Decide full `libmodsecurity` + full OWASP CRS atau kekal Rimau-native WAF.
  - Done criteria: ADR accepted dengan source/version/license/build plan atau keputusan untuk tidak bundle.
  - Status: ADR-0034 accepted keeping the Rimau-native ModSecurity-compatible WAF subset for P1; full `libmodsecurity` and full OWASP CRS remain future work that needs pinned source/version/license/build/update/test planning before any claim of support.
- [x] Tambah per-virtual-host WAF controls jika diperlukan.
  - Done criteria: vhost boleh override enable/threshold/rule exception dari SQLite.
  - Status: SQLite `virtual_host_waf_overrides` supports per-host `enabled`, `owasp_crs`, `blocking`, `threshold`, and numeric `rule_exceptions`; HTTP/1.1 network integration verifies default WAF blocking and host override allow paths.
- [x] Tambah structured WAF audit log.
  - Done criteria: format audit jelas, tidak bocor secret, dan ada retention/rotation guidance.
  - Status: WAF matches now emit redacted `rimau_waf_audit` JSON payloads through the existing stderr logger; docs record no raw body/query/header/cookie/authorization/evidence logging and require retention/rotation through deployment logging until a dedicated audit sink is accepted.
- [ ] Tambah parser/framing fuzz tests.
  - Done criteria: fuzz target boleh dijalankan dalam CI atau local documented command.

## Phase 3: TLS And Certificate Lifecycle

- [ ] Tambah automated TLS ALPN `h2` test dengan real HTTP/2 client.
  - Done criteria: bukan hanya Python raw-frame smoke.
- [ ] Tambah automated multi-certificate SNI selection test.
  - Done criteria: cert dipilih mengikut host exact/wildcard.
- [ ] Tambah OCSP stapling decision/implementation.
  - Done criteria: ADR diterima dan test/config wujud, atau dinyatakan tidak disokong.
- [ ] Tambah production certificate management guide.
  - Done criteria: docs jelaskan path, permission, reload, rotation, dan rollback.
- [ ] Tambah automated fully-static ELF checks.
  - Done criteria: CI/local script semak `ldd`, `file`, dan tiada `INTERP`.

## Phase 4: HTTP/2 Production Path

- [ ] Decide native HTTP/2 penuh atau bundled `nghttp2`.
  - Done criteria: ADR accepted dengan source/version/SHA/license/update policy jika bundling.
- [ ] Pindahkan HTTP/2 logic dari `ClientConnection` ke session module khusus.
  - Done criteria: HTTP/2 state machine tidak lagi inline besar dalam server core.
- [ ] Implement complete stream lifecycle.
  - Done criteria: stream open/half-closed/closed dan reset diuji.
- [ ] Implement CONTINUATION assembly.
  - Done criteria: header block multi-frame diterima dengan limit yang selamat.
- [ ] Complete HPACK Huffman dan dynamic table atau gantikan dengan library.
  - Done criteria: real-client HTTP/2 headers umum boleh diproses.
- [ ] Implement flow control.
  - Done criteria: DATA large body/response tidak melanggar window semantics.
- [ ] Tambah real-client h2c dan TLS `h2` integration tests.
  - Done criteria: curl/nghttp/httpx client berjaya dalam test automatik. Needs verification untuk tool pilihan.

## Phase 5: Reverse Proxy Production Path

- [ ] Pindahkan normal HTTP reverse proxy upstream I/O ke worker reactor atau async upstream state machine.
  - Done criteria: tiada `poll()` blocking di handler normal proxy path.
- [ ] Implement reverse proxy request/response streaming dan backpressure.
  - Done criteria: upstream/downstream large body tidak buffer penuh dalam memori.
- [ ] Tambah upstream connection pooling.
  - Done criteria: keep-alive upstream boleh reuse connection dengan limit dan cleanup.
- [ ] Tambah active health checks.
  - Done criteria: upstream health status boleh dilihat/test tanpa tunggu request fail.
- [ ] Tambah advanced load balancing policy.
  - Done criteria: at least round-robin, failover, dan satu policy tambahan jelas diuji.
- [ ] Tambah per-upstream TLS CA/pinning/verify-depth policy.
  - Done criteria: verification boleh dikawal per upstream dari SQLite.
- [ ] Validate static glibc DNS/NSS hostname behavior.
  - Done criteria: reverse proxy hostname path diuji dalam environment deployment sasaran atau direka alternatif resolver.

## Phase 6: HTTP/3 Live Serving

- [ ] Decide HTTP/3 stack: native atau bundled `ngtcp2` + `nghttp3` / `quiche`.
  - Done criteria: ADR accepted dengan source/version/SHA/license/build plan.
- [ ] Tambah UDP listener.
  - Done criteria: server bind UDP port dan lifecycle graceful shutdown diuji.
- [ ] Implement QUIC connection lifecycle.
  - Done criteria: handshake, stream, close, timeout, dan key update policy jelas.
- [ ] Implement QPACK.
  - Done criteria: request headers HTTP/3 real client boleh decode.
- [ ] Implement HTTP/3 request/response adapter ke `Transaction`.
  - Done criteria: GET `/` melalui HTTP/3 live client berjaya.
- [ ] Aktifkan ALPN `h3` hanya selepas live serving lulus.
  - Done criteria: config validation membenarkan `h3` dan test real-client lulus.

## Phase 7: Server-Side Runtime Support

- [ ] Decide runtime model: bundled PHP/Python/Perl, embedded VM kecil, CGI/FastCGI optional, atau kombinasi.
  - Done criteria: ADR accepted dengan security model.
- [ ] Jika bundle PHP, pin source/version/SHA/license/build flags.
  - Done criteria: PHP route tidak shell out ke system `php`.
- [ ] Jika bundle Python, pin source/version/SHA/embed model.
  - Done criteria: Python route tidak bergantung kepada interpreter sistem.
- [ ] Jika bundle Perl, pin source/version/SHA/embed model.
  - Done criteria: Perl route tidak bergantung kepada interpreter sistem.
- [ ] Define script vhost contract.
  - Done criteria: entrypoint, env vars, body streaming, timeout, memory limit, dan isolation documented/tested.
- [ ] Implement script runtime execution.
  - Done criteria: `script:runtime:path` tidak lagi `501` untuk runtime yang benar-benar siap.

## Phase 8: Observability And Admin

- [ ] Tambah structured access log format.
  - Done criteria: status, bytes, latency, vhost, upstream, dan WAF outcome boleh direkod.
- [ ] Tambah metrics endpoint atau metrics exporter.
  - Done criteria: worker load, active connections, requests, errors, rate-limit/WAF/proxy stats tersedia.
- [ ] Tambah admin/control endpoint jika diterima.
  - Done criteria: auth model jelas; tiada secret dalam repo.
- [ ] Tambah config backup/restore guide.
  - Done criteria: SQLite config boleh backup/restore dengan documented command.

## Phase 9: Deployment

- [ ] Tentukan install layout.
  - Done criteria: binary, public root, config DB, certs, logs, dan service files ada path rasmi.
- [ ] Tambah systemd service.
  - Done criteria: service start/stop/reload documented.
- [ ] Tambah package build.
  - Done criteria: artifact installable wujud untuk target OS yang dipilih.
- [ ] Tambah container image jika diperlukan.
  - Done criteria: image build/run documented dan tidak bercanggah dengan static binary goal.
- [ ] Tambah production hardening guide.
  - Done criteria: user/group, permissions, TLS, firewall, logging, backups, limits, dan update process documented.

## Phase 10: Performance

- [ ] Tambah benchmark harness.
  - Done criteria: command repeatable untuk HTTP/1.1, HTTPS, static, proxy, WAF on/off.
- [ ] Benchmark Linux `epoll` reactor.
  - Done criteria: baseline numbers documented; belum dakwa setanding nginx/OpenLiteSpeed tanpa data.
- [ ] Implement zero-copy static path seperti `sendfile`.
  - Done criteria: static large file benchmark menunjukkan improvement dan fallback path selamat.
- [ ] Tambah buffer/memory pool strategy.
  - Done criteria: allocation reduction boleh diukur atau sekurang-kurangnya diuji untuk memory limit.
- [ ] Decide `io_uring` selepas benchmark.
  - Done criteria: ADR diterima dengan sebab dan risiko.

## Validation Minimum Set

Untuk perubahan C++ biasa:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --protocols
```

Untuk perubahan static deployment:

```bash
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
```

Untuk perubahan WAF:

```bash
./build/rimau-waf-tests
```

Untuk dokumentasi sahaja:

```bash
rg "Needs verification|Partial|Planned|Implemented" docs AGENTS.md README.md
```
