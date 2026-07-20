# Plan 020: ModSecurity-Compatible Built-In WAF

Date: 2026-07-20

## Goal

Add ModSecurity-style request protection with OWASP CRS-inspired rules while preserving the project requirement that runtime configuration comes from SQLite and the deployable server should not depend on external runtime libraries.

## Implemented In This Phase

- Add a Rimau-native WAF module in `include/rimau/http/waf.hpp` and `src/http/waf.cpp`.
- Add SQLite keys:
  - `modsecurity_enabled`
  - `modsecurity_owasp_crs_enabled`
  - `modsecurity_blocking_enabled`
  - `modsecurity_anomaly_threshold`
  - `modsecurity_max_inspection_bytes`
  - `modsecurity_audit_log_enabled`
- Default `modsecurity_enabled=false` so existing installations do not unexpectedly block requests.
- Inspect HTTP/1.1 requests before `Transaction` dispatch.
- Inspect WebSocket upgrade requests before local echo or reverse proxy tunnel setup.
- Inspect partial HTTP/2 requests before shared handler dispatch.
- Return `403 Forbidden` with `x-rimau-waf-*` headers when blocking mode is enabled and the request score reaches the configured threshold.
- Add `rimau_waf` CTest coverage for disabled mode, blocking mode, detection-only mode, scanner user agents, path traversal, SQLi, XSS, and a normal curl-style request.

## Built-In Rule Coverage

Current rules are a small OWASP CRS-inspired subset:

- Scanner and vulnerability tool user agents.
- Encoded CRLF/request splitting patterns.
- Path traversal and local file probing patterns.
- Cross-site scripting patterns.
- SQL injection patterns.
- Remote command execution patterns.
- PHP wrapper/injection patterns.
- Java/JNDI exploitation patterns.

## Explicit Non-Goals For This Phase

- Do not claim full `libmodsecurity` integration.
- Do not claim the full OWASP Core Rule Set is bundled.
- Do not parse ModSecurity rule syntax.
- Do not implement the full ModSecurity phase engine.
- Per-virtual-host WAF overrides were out of scope for the initial WAF subset, but ADR-0035 later added SQLite `virtual_host_waf_overrides`.
- Do not add persistent structured WAF audit logs.

These remain `Needs verification` because they require dependency selection, source/version pinning, licensing review, update tooling, broader tests, and performance validation.

## Validation

Commands run:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --check-config
./build/rimau-server --protocols
```

Manual smoke:

```text
temporary SQLite database on 127.0.0.1:18180 with modsecurity_enabled=true
GET / returned 200 OK
GET /../../etc/passwd returned 403 with x-rimau-waf-rule-id: 930100
GET / with User-Agent sqlmap/1.8 returned 403 with x-rimau-waf-rule-id: 913100
```

Known validation note:

- Initial broad SQLi pattern matching `/*` caused a false positive for `Accept: */*`; it was removed and covered by a regression test.
