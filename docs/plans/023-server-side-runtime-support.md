# Plan 023: Server-Side Runtime Support

Date: 2026-07-20

Status: Partial. P2 safety contract accepted; real runtime execution is still planned.

## Goal

Define the server-side runtime direction without accidentally depending on system interpreters or claiming PHP, Python, Perl, CGI, or FastCGI execution before a real runtime path is bundled, integrated, and tested.

## Accepted P2 Baseline

- `script:runtime:path` remains a virtual host declaration only.
- Script vhost requests return `501 Not Implemented`.
- The response includes `x-rimau-runtime-status: planned`.
- Rimau must not search `PATH`, shell out, fork/exec system `php`, `python`, `perl`, or run any external interpreter for `script:` vhosts.
- PHP, Python, Perl, CGI, FastCGI, embedded VM, or any other runtime execution needs a separate ADR before implementation.

## Current Script Vhost Contract

Config format:

```text
host=script:runtime:script-root
```

Current request behavior:

- Entry point discovery: not implemented.
- Environment variables: not emitted.
- Request body streaming to runtime: not implemented.
- Runtime timeout: not implemented.
- Runtime memory limit: not implemented.
- Process/thread isolation: not implemented.
- Filesystem sandboxing: not implemented.
- Response mapping from runtime stdout/protocol: not implemented.

The only stable behavior today is explicit non-execution:

```text
HTTP status: 501 Not Implemented
Header: x-rimau-runtime-status: planned
Body: JSON with runtime, script_root, implemented=false, and message
```

## Future Options Requiring ADR

- Bundle a specific PHP release from source with pinned version, SHA256, license notes, build flags, static-link feasibility, extension policy, request adapter, isolation model, update process, and tests.
- Bundle a specific Python release with pinned version, embedded interpreter model, module path policy, package isolation, GIL/threading policy, security model, update process, and tests.
- Bundle a specific Perl release with pinned version, embedding API plan, module path policy, security model, update process, and tests.
- Add optional external CGI/FastCGI mode only if it is explicitly documented as an external dependency and gated by SQLite config that cannot be confused with bundled runtime support.
- Embed a smaller VM only after source/version/license/build/security/update planning is accepted.

## Implemented Coverage

- `tests/test_virtual_host.cpp` verifies script vhost parsing and `501` response behavior.
- `tests/test_http1_network.py` verifies fake `php`, `python`, and `perl` executables placed first in `PATH` are not invoked by script vhost requests.

## Remaining Work

- Choose one real runtime path after an accepted ADR.
- Add runtime-specific SQLite config only when implementation is accepted.
- Define entry point discovery, runtime environment, request body streaming, timeout, memory limits, isolation, response protocol, logging, and error behavior for the chosen path.
- Add end-to-end tests proving the chosen runtime works without hidden system dependencies.
