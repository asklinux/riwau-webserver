# Plan 003: Proxygen-Inspired Request Pipeline

Date: 2026-07-18

## Goal

Learn from `https://github.com/facebook/proxygen` and adapt the architecture into Rimau Web Server without copying Proxygen code.

## Concepts Adopted

Proxygen separates HTTP serving into layers:

- server/acceptor/session for connection lifecycle
- codec for protocol wire parsing and serialization
- transaction for one request-response pair
- request handler for application logic
- request handler factory for creating per-request handlers
- downstream response handler/writer for sending responses

Rimau now has a smaller native version:

- `rimau::http::RequestHandler`
- `rimau::http::RequestHandlerFactory`
- `rimau::http::Transaction`
- `rimau::http::ResponseSink`
- `rimau::http::ResponseBuilder`
- `rimau::http::StaticFileHandler`

## Implemented In This Phase

- Refactored HTTP/1.1 static file path to use a transaction and handler.
- Added `SocketResponseSink` inside the HTTP/1.1 connection implementation.
- Added tests for GET, HEAD, and unsupported method through the handler pipeline.
- Renamed visible project identity to `Rimau Web Server` in CLI/version output and HTTP `server` header.

## Not Implemented Yet

- Filter chain.
- Async request body streaming.
- HTTP/1.1 codec object separate from connection.
- HTTP/2 codec/session integration.
- HTTP/3 codec/session integration.
- Flow control.
- Backpressure callbacks.
- Event loop worker lifecycle.

## Next Steps

1. Add HTTP/1.1 codec/session abstraction.
2. Add handler filter chain.
3. Add access log filter.
4. Add request body callbacks.
5. Add event-loop worker plan.
6. Update HTTP/2 and HTTP/3 plans to map each protocol into `Transaction` and `ResponseSink`.

## Safety Notes

- Proxygen was used as a design reference only.
- No Proxygen source code was copied into Rimau.
- Keep future references as conceptual unless a license/dependency decision is explicitly recorded in `docs/DECISIONS.md`.
