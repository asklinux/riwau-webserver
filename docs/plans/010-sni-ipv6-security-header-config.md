# Plan 010: SNI, IPv6, And Security Header Config

Date: 2026-07-19

## Goal

Close the remaining practical TLS/security configuration gaps from Plan 009 without adding file-based runtime configuration or advertising unfinished HTTP/2/HTTP/3 support.

## Implemented Scope

- Added SQLite key `tls_sni_certificates`.
- `tls_sni_certificates` uses semicolon-separated entries:

```text
hostname-pattern=certificate.pem|private-key.pem
```

- The default `tls_certificate_file` and `tls_private_key_file` remain the fallback certificate.
- Each configured SNI certificate entry creates its own bundled-OpenSSL `SSL_CTX`.
- The OpenSSL SNI callback selects a matching context for exact hostnames or simple `*.domain` wildcard patterns.
- `tls_sni_hosts` remains an optional SNI allowlist. Configured SNI certificate patterns are also accepted.
- SIGHUP reload can publish a new TLS context for new connections when `tls_sni_certificates` changes.
- Listener bind supports IPv4 or IPv6 literal addresses.
- IP allowlist/blocklist matching supports IPv4 and IPv6 exact addresses and CIDR ranges.
- SQLite validation rejects malformed IP list entries during `--set` and `--check-config`.
- Added SQLite-configurable values for the fixed security header set:
  - `security_header_content_security_policy`
  - `security_header_strict_transport_security`
  - `security_header_x_content_type_options`
  - `security_header_x_frame_options`
  - `security_header_referrer_policy`
  - `security_header_cross_origin_opener_policy`
- Empty individual security header values disable that header while `security_headers_enabled=true`.
- Dev Makefile and scripts reset IP lists, SNI certificate map, and security header values for deterministic local runs.

## Deliberate Limits

- ALPN still advertises `http/1.1` only.
- At this phase HTTP/2 and HTTP/3 remained config/status gates only. Plan 017 later adds partial wire codec support.
- Rate limiting remains in-memory and per process.
- Security header names remain a fixed built-in set; arbitrary custom header names are not implemented.
- Bind config accepts IPv4 or IPv6 literals, but not hostname resolution.
- There is no explicit dual IPv4/IPv6 listener mode from one config value yet.
- Certificate reload affects new TLS connections only. Existing TLS sessions keep their original context.

## Validation

Commands used:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Runtime smoke tests with temporary SQLite databases covered:

- IPv6 listener on `::1`.
- IPv6 allowlist `::1/128`.
- Security header override to `x-frame-options: DENY`.
- Empty `security_header_x_content_type_options` suppressing that header.
- SNI multi-certificate selection for default and alternate certificates.
