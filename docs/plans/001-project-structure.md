# Plan 001: Project Structure

Date: 2026-07-18

## Goal

Create a maintainable C++ web server structure before implementing advanced HTTP/2 and HTTP/3 support.

## Created Structure

```text
include/rimau/core/       Public headers for SQLite config, server, logging, version.
include/rimau/http/       Public headers for HTTP request, parser, response.
include/rimau/protocol/   Public headers for protocol-specific connection modules.
src/core/                 Core runtime implementation.
src/http/                 HTTP parser and response implementation.
src/protocol/             HTTP/1.1 implementation and HTTP/2/HTTP/3 placeholders.
tests/                    CTest-based test binaries.
public/                   Default document root for local static serving.
docs/                     Persistent project-memory system.
docs/plans/               Larger plans and implementation roadmaps.
```

## Initial Binary

Binary:

```text
rimau-server
```

Implemented CLI:

```text
--database path
--set key=value
--check-config
--protocols
--version
--help
```

## Design Notes

- Keep public API headers under `include/rimau/`.
- Keep implementation under matching `src/` folders.
- Keep protocol implementation separate from generic HTTP parsing and response helpers.
- Keep docs updated after each system update through `AGENTS.md`.
- Do not claim nginx/OpenLiteSpeed parity until benchmarks and production features exist.

## Next Steps

- Validate build and tests.
- Add more HTTP parser and static file tests.
- Event loop plan added in `docs/plans/007-async-nonblocking-io.md`.
- Add TLS/ALPN plan.
- Add HTTP/2 and HTTP/3 library selection decision.
