# Plan 014: Reverse Proxy Passive Circuit Breaker

Date: 2026-07-19

Status: Implemented for process-local passive circuit breaker state shared by normal HTTP reverse proxy and WebSocket reverse proxy setup.

## Goal

Avoid repeatedly selecting upstream targets that have just failed, while keeping reverse proxy configuration in SQLite and avoiding new external dependencies.

## Scope For This Phase

- Implemented: SQLite keys for enabling/disabling passive circuit breaker behavior.
- Implemented: Failure threshold before opening an upstream circuit.
- Implemented: Cooldown duration before an opened upstream is retried.
- Implemented: Process-local in-memory circuit breaker state keyed by upstream target.
- Implemented: Normal HTTP reverse proxy records upstream success/failure and skips opened circuits.
- Implemented: WebSocket reverse proxy setup uses the same state before connect/handshake.
- Implemented: Return `503 Service Unavailable` with `Retry-After` when every selected upstream is skipped by an opened circuit.

## Config Keys

```text
reverse_proxy_circuit_breaker_enabled=true
reverse_proxy_circuit_breaker_failure_threshold=3
reverse_proxy_circuit_breaker_cooldown_seconds=10
```

## Deliberate Limits

- Circuit breaker state is in-memory per process.
- There is no active health-check scheduler yet.
- There is no shared/distributed state across workers in different processes or nodes.
- Circuit state is keyed by upstream target, not by route plus method or by detailed failure type.
- Upstream connection pooling is still not implemented.

## Validation

Latest validation is recorded in `docs/PROGRESS.md`.
