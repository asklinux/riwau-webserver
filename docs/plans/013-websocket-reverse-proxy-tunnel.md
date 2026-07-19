# Plan 013: WebSocket Reverse Proxy Tunnel

Date: 2026-07-19

Status: Implemented for HTTP/1.1 reverse proxy virtual hosts with `http://` and `https://` upstream handshakes, validated upstream `101` response serialization, and epoll-based bidirectional tunnel data after handshake.

## Goal

Allow reverse proxy virtual hosts to proxy WebSocket Upgrade requests instead of always using the local WebSocket echo handler.

## Scope For This Phase

- Implemented: Detect WebSocket Upgrade before local echo handling.
- Implemented: Select reverse proxy vhost rules from SQLite `virtual_hosts`.
- Implemented: Reuse configured upstream pools, retry/failover, timeout, and upstream TLS verification settings.
- Implemented: Support upstream `http://` WebSocket handshakes.
- Implemented: Support upstream `https://` WebSocket handshakes through bundled OpenSSL.
- Implemented: Validate required upstream `101 Switching Protocols` headers.
- Implemented: Rebuild/sanitize the upstream handshake response before sending it to the client.
- Implemented: Register the upstream fd in the same worker `epoll` reactor after handshake.
- Implemented: Tunnel bytes in both directions after handshake.
- Implemented: Keep non-proxy WebSocket requests on the existing basic local echo path.

## Runtime Flow

```text
client WebSocket Upgrade
  -> parse HTTP/1.1 headers
  -> if Host matches proxy vhost
       -> choose upstream with existing round-robin/retry rules
       -> connect TCP, optional TLS through bundled OpenSSL
       -> send upstream WebSocket handshake
       -> validate upstream 101 response
       -> send sanitized 101 response to client
       -> register upstream fd with worker epoll
       -> tunnel client/upstream bytes
  -> else
       -> local basic WebSocket echo
```

## Deliberate Limits

- DNS resolution and connect still happen before tunnel mode, not as a fully asynchronous upstream state machine.
- Upstream TLS handshake and upstream `101` header read still happen before tunnel mode.
- Normal HTTP reverse proxy responses remain buffered.
- WebSocket proxy tunnel is byte-stream oriented; it does not yet enforce full frame-aware policy for fragmentation, extensions, subprotocol negotiation, or per-frame inspection. Needs verification.
- Active health checks, upstream connection pooling, and advanced load balancing remain planned. Passive circuit breaker was added later in Plan 014.

## Validation

Latest validation is recorded in `docs/PROGRESS.md`.
