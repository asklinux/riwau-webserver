# Plan 005: Bundled OpenSSL TLS

Date: 2026-07-18

## Goal

Add SSL/TLS support to Rimau Web Server without requiring a system OpenSSL runtime dependency.

## Implemented

- CMake builds OpenSSL from source through `ExternalProject_Add`.
- Bundled OpenSSL version is pinned to `openssl-4.0.1`.
- OpenSSL source archive SHA256 is pinned in `CMakeLists.txt`.
- OpenSSL is built static with `no-shared`, `no-tests`, and `no-docs`.
- Rimau links to bundled `libssl.a` and `libcrypto.a`.
- `rimau-server` does not dynamically link to `libssl` or `libcrypto`.
- HTTP/1.1 connections can use TLS when SQLite config `tls_enabled=true`.
- Dev certificate generation uses the bundled `openssl` binary.
- Added `make start-https`, `make stop-https`, `make status-https`, and `make serve-https`.

## SQLite Keys

- `tls_enabled`
- `tls_certificate_file`
- `tls_private_key_file`

## Validation

```bash
build/_deps/openssl/install/bin/openssl version
ldd build/rimau-server | rg 'ssl|crypto' || true
make start-https
curl -k -I https://127.0.0.1:18443/
make stop-https
```

Expected:

- Bundled OpenSSL reports `OpenSSL 4.0.1`.
- `ldd` does not list `libssl` or `libcrypto`.
- HTTPS returns `HTTP/1.1 200 OK`.

## Not Implemented Yet

- Production certificate lifecycle.
- Certificate reload.
- ALPN.
- HTTP/2 negotiation over TLS.
- OCSP stapling.
- Session tickets/resumption policy.

## Update Process

To update OpenSSL for Rimau only:

1. Check the latest official OpenSSL tag from `https://github.com/openssl/openssl`.
2. Download the official release archive.
3. Compute SHA256.
4. Update `RIMAU_OPENSSL_TAG` and `RIMAU_OPENSSL_SHA256` in `CMakeLists.txt`.
5. Rebuild Rimau.
6. Verify `ldd build/rimau-server` still does not show `libssl` or `libcrypto`.
