# Plan 011: Virtual Hosts, Reverse Proxy, And Server-Side Runtimes

Date: 2026-07-19

Status: Baseline implemented for virtual host routing, static vhosts, HTTP reverse proxy vhosts, and script-runtime declarations. Real bundled runtime execution remains planned.

## Goal

Add SQLite-configured virtual host routing and baseline reverse proxy support while preserving the rule that runtime configuration is read from SQLite.

## Scope For This Phase

- Implemented: Add virtual host routing from the HTTP `Host` header.
- Implemented: Support exact hostnames and simple `*.domain` wildcard host patterns.
- Implemented: Keep the global `document_root` as the fallback/default host.
- Implemented: Add static virtual hosts with host-specific document roots.
- Implemented: Add HTTP reverse proxy virtual hosts for upstream HTTP services.
- Implemented: Add script virtual host routing syntax so a vhost can declare a desired runtime name such as `php`, `python`, or `perl`.
- Implemented: Return an explicit `501 Not Implemented` for script virtual hosts until a bundled runtime engine is actually integrated.

## Runtime Language Constraint

PHP, Python, Perl, and arbitrary server-side languages are full runtimes, not small libraries. Calling `/usr/bin/php`, `/usr/bin/python`, or `/usr/bin/perl` would add external runtime dependencies and would not satisfy the project rule that dependencies are bundled. Therefore this phase must not claim PHP/Python/Perl execution is implemented.

Future work needs a separate decision for each runtime:

- Bundle and statically build a specific upstream runtime. Needs verification.
- Embed one approved scripting VM. Needs verification.
- Support CGI/FastCGI using external interpreters as an optional mode, explicitly documented as external dependency. Needs verification.

## Virtual Host Config Format

The SQLite key is `virtual_hosts`, using semicolon-separated entries:

```text
host=static:document-root;host=proxy:http://127.0.0.1:9000;host=script:runtime:script-root
```

Examples:

```text
site.test=static:public/site
api.test=proxy:http://127.0.0.1:9000
*.app.test=script:php:public/app
```

## Deliberate Limits

- Reverse proxy upstream is HTTP only in this phase.
- Reverse proxy body/response handling is buffered and not streaming.
- Reverse proxy work is still handler-level and not a full async upstream connection state machine.
- At this phase there was no load balancing, retry policy, circuit breaker, health checks, WebSocket proxying, or HTTPS upstream. Plan 012 later added HTTPS upstream pools/retry, and Plan 013 later added WebSocket proxy tunneling.
- Script virtual hosts return `501` until bundled runtimes are integrated and tested.

## Implementation Result

- Added `include/rimau/http/virtual_host.hpp`.
- Added `src/http/virtual_host.cpp`.
- Added SQLite keys `virtual_hosts_enabled`, `virtual_hosts`, `reverse_proxy_connect_timeout_seconds`, `reverse_proxy_read_timeout_seconds`, and `reverse_proxy_max_response_bytes`.
- `ClientConnection` now dispatches HTTP/1.1 requests through `VirtualHostHandlerFactory` instead of direct static-only factory selection.
- Added unit coverage in `tests/test_virtual_host.cpp`.
- Updated dev SQLite seed commands in `Makefile`, `scripts/serve-dev.sh`, and `scripts/serve-dev-https.sh`.

## Validation

Latest validation for this plan is recorded in `docs/PROGRESS.md`.
