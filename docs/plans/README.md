# Plans

This directory stores implementation plans and design notes for work that is larger than a small patch.

## How To Create The Project Memory Files

The required memory files are:

```text
AGENTS.md
docs/PROJECT_CONTEXT.md
docs/ARCHITECTURE.md
docs/DECISIONS.md
docs/PROGRESS.md
docs/TODO.md
docs/plans/
```

Command shape for a fresh repo:

```bash
mkdir -p docs/plans
touch AGENTS.md docs/PROJECT_CONTEXT.md docs/ARCHITECTURE.md docs/DECISIONS.md docs/PROGRESS.md docs/TODO.md docs/plans/README.md
```

In this repo the files already exist. Do not recreate them from scratch unless the project owner explicitly requests it. Update them in place.

## Plan File Naming

Use:

```text
NNN-short-title.md
```

Examples:

```text
001-project-structure.md
002-http2-http3-roadmap.md
003-proxygen-inspired-pipeline.md
004-sqlite-configuration.md
005-bundled-openssl-tls.md
006-http2-http3-support-system.md
007-async-nonblocking-io.md
008-http1-feature-expansion.md
009-security-tls-hardening.md
010-sni-ipv6-security-header-config.md
011-virtual-host-reverse-proxy-server-side-runtimes.md
012-reverse-proxy-upstream-pools-https-retry.md
013-websocket-reverse-proxy-tunnel.md
014-reverse-proxy-passive-circuit-breaker.md
015-bundled-zlib-sqlite.md
016-bundled-glibc-source-static.md
017-http2-http3-wire-codecs.md
018-http2-h2c-request-serving.md
019-http2-tls-alpn-h2.md
020-modsecurity-owasp-crs-built-in-waf.md
021-ordered-update-checklist.md
022-production-certificate-management.md
023-server-side-runtime-support.md
```

## Update Rules

- Add a plan before large protocol, architecture, deployment, or security changes.
- Move completed work into `docs/PROGRESS.md`.
- Move pending work into `docs/TODO.md`.
- Capture accepted technical choices in `docs/DECISIONS.md`.
- Mark unknowns as `Needs verification`.
- Never include secrets.
