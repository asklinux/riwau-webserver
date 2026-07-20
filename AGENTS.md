# AGENTS.md

Arahan ini wajib diikuti oleh setiap sesi Codex yang bekerja dalam repositori ini.

## Wajib Baca Sebelum Mula

Sebelum membuat perubahan, baca fail ini mengikut turutan:

1. `docs/PROJECT_CONTEXT.md`
2. `docs/ARCHITECTURE.md`
3. `docs/DECISIONS.md`
4. `docs/PROGRESS.md`
5. `docs/TODO.md`
6. Fail relevan dalam `docs/plans/`

Selepas itu semak keadaan kerja semasa:

```bash
git status --short --branch
rg --files
```

Jika repositori belum diinisialisasi sebagai Git repository, rekodkan fakta itu dalam `docs/PROGRESS.md` apabila relevan.

## Wajib Kemas Kini Selepas Kerja

Selepas membuat perubahan sistem, kemas kini dokumen yang relevan:

- `docs/PROJECT_CONTEXT.md` jika tujuan projek, stack, command, proses pembangunan, atau struktur repo berubah.
- `docs/ARCHITECTURE.md` jika modul, aliran request, protokol, konfigurasi, deployment, atau reka bentuk berubah.
- `docs/DECISIONS.md` jika ada keputusan teknikal baru atau perubahan keputusan lama.
- `docs/PROGRESS.md` jika feature, status implementasi, validasi, atau tarikh kerja berubah.
- `docs/TODO.md` jika kerja selesai, kerja baru ditemui, bug dikenal pasti, atau priority berubah.
- `docs/plans/` jika kerja besar memerlukan pelan berasingan.

Jangan akhiri kerja tanpa merekod status sebenar. Bezakan dengan jelas antara:

- `Implemented`
- `Partial`
- `Planned`
- `Needs verification`
- `Blocked`

## Larangan Keselamatan

Jangan masukkan perkara berikut dalam dokumen atau kod contoh:

- Password
- Token API
- Private key
- Secret production
- Cookie/session sebenar
- Credential database

Jika fail konfigurasi memerlukan nilai sensitif, gunakan placeholder seperti `CHANGE_ME` dan pastikan fail sebenar diabaikan oleh `.gitignore`.

## Standard Kerja Kod

- Gunakan C++20 dan CMake selagi keputusan teknikal ini belum berubah dalam `docs/DECISIONS.md`.
- Kekalkan struktur modul dalam `include/rimau/`, `src/`, `tests/`, `public/`, `scripts/`, dan `docs/`.
- Semua runtime configuration server wajib dibaca daripada SQLite melalui `--database`; jangan hidupkan semula fail config key-value.
- Perubahan skema SQLite config wajib direkod dalam `rimau_schema_migrations`, ditest dengan DB lama/baru, dan didokumenkan dalam `docs/ARCHITECTURE.md` serta `docs/DECISIONS.md`.
- SQLite engine wajib guna bundled static SQLite yang dibina oleh target `rimau_bundled_sqlite`; jangan tambah semula `find_package(SQLite3)` sistem.
- TLS/SSL wajib guna bundled OpenSSL yang dibina oleh CMake target `rimau_bundled_openssl`; jangan tambah semula `find_package(OpenSSL)` sistem.
- Gzip wajib guna bundled static zlib yang dibina oleh target `rimau_bundled_zlib`; jangan tambah semula `find_package(ZLIB)` sistem.
- Linux fully static deployment binary wajib guna bundled glibc yang dibina daripada source asal melalui target `rimau_bundled_glibc` apabila `RIMAU_USE_BUNDLED_GLIBC=ON`; jangan gantikan dengan link kepada system `libc.a` atau dynamic `glibc`.
- Build glibc bundled menggunakan GNU Bison bundled target `rimau_bundled_bison` dan Linux UAPI headers bundled target `rimau_bundled_linux_headers`; jangan tambah dependency `libselinux` sistem atau header sistem untuk menggantikan source target ini.
- Bundled glibc path semasa wired untuk Linux x86_64 sahaja. Platform lain perlu keputusan dan validasi baru sebelum didakwa supported.
- Virtual host, reverse proxy, dan server-side runtime declarations wajib dikonfigurasi melalui SQLite keys `virtual_hosts_enabled`, `virtual_hosts`, dan `reverse_proxy_*`; jangan tambah fail vhost config berasingan.
- Jangan dakwa PHP, Python, Perl, atau bahasa server-side lain sudah terbina dalam sehingga runtime sebenar dibundle, dibina, diuji, dan direkod dalam `docs/DECISIONS.md`. Route `script:runtime:path` semasa hanya deklarasi dan mesti pulang `501 Not Implemented`.
- ModSecurity support semasa ialah WAF terbina dalam yang ModSecurity-compatible pada tahap konsep dengan subset rules OWASP CRS-inspired dalam `rimau::http::inspect_request`; jangan dakwa `libmodsecurity` penuh atau full OWASP Core Rule Set sudah divendor sehingga source sebenar dibundle, rule penuh dipin, diuji, dan direkod sebagai keputusan baru.
- Runtime I/O wajib kekal asynchronous dan non-blocking. Server utama guna Linux `epoll` reactor per worker thread; jangan hidupkan semula thread-per-connection blocking path atau single `poll()` loop.
- HTTP handler tidak boleh menganggap `Request::body` sentiasa mengandungi body penuh. Untuk body besar, HTTP/1.1 boleh attach temporary `RequestBodyFile`; guna `Request::body_size()`, `Request::body_spooled_to_file()`, dan `Request::body_text(max_bytes)` atau reka streaming API rasmi sebelum baca body besar penuh.
- Untuk baca body dalam handler secara chunk, guna `Request::open_body_reader()` dan `RequestBodyReader::read_chunk(max_bytes)`; jangan load body besar penuh jika hanya preview atau stream pemprosesan diperlukan.
- Untuk response tanpa `Content-Length` awal, guna `ResponseSink::send_chunked` atau `ResponseBuilder::send_chunked`; jangan encode chunked body secara ad hoc dalam handler.
- Static directory index dan custom error page wajib dikonfigurasi melalui SQLite keys `directory_index` dan `error_page`; jangan hard-code fail index/error baru dalam handler.
- Brotli tidak dilaksanakan dalam P1 kerana tiada bundled dependency yang diterima; jangan dakwa Brotli supported sehingga source dependency dipin, dibundle, dibina, diuji, dan ADR baru diterima.
- Config key `http2_enabled` mengaktifkan partial HTTP/2 request serving untuk cleartext h2c dan TLS ALPN `h2` apabila `tls_alpn_protocols` mengandungi `h2`; config key `http3_enabled` masih status/control-plane untuk live server.
- ALPN `h2` hanya boleh diiklankan apabila `http2_enabled=true` dan statusnya mesti kekal partial; jangan iklankan ALPN `h3` sebelum HTTP/3 request serving dan test end-to-end real client siap.
- Jangan dakwa HTTP/2 atau HTTP/3 siap penuh hanya kerana codec/frame parser atau partial request serving wujud. Bezakan `partial wire codec`, `partial h2c request serving`, `partial TLS ALPN h2 request serving`, dan `implemented production protocol support`.
- Mark maklumat tidak pasti sebagai `Needs verification`.
- Tambah test untuk parser, protokol, config, keselamatan path, dan behavior rangkaian apabila modul berkaitan berubah.

## Command Validasi Asas

Jalankan command ini selepas perubahan C++ apabila boleh:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-waf-tests
./build/rimau-server --check-config
./build/rimau-server --database data/rimau.sqlite3 --check-config
./build/rimau-server --protocols
./build/rimau-server --database data/rimau.sqlite3 --protocols
ldd build/rimau-server || true
file build/rimau-server
readelf -l build/rimau-server | rg 'INTERP|Requesting program interpreter' || true
```

Shortcut yang dibenarkan dari root projek:

```bash
make test
./scripts/test.sh
```

Jangan guna `ctest` sahaja dari root source directory untuk mengesahkan test; ia tidak membaca test yang dijana dalam `build/`.

Untuk ujian manual HTTP/1.1:

```bash
make start
curl -i http://127.0.0.1:18080/
make stop
```

Untuk ubah config SQLite:

```bash
./build/rimau-server --database data/rimau.sqlite3 --set port=18080
./build/rimau-server --database data/rimau.sqlite3 --check-config
```

Untuk ujian HTTPS dev:

```bash
make start-https
curl -k -I https://127.0.0.1:18443/
make stop-https
```
