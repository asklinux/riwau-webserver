# Plan 008: HTTP/1.1 Feature Expansion

Date: 2026-07-18

## Goal

Expand Rimau HTTP/1.1 behavior beyond static GET/HEAD so the server can handle common HTTP/1.1 client behavior before HTTP/2 and HTTP/3 wire-level implementation begins. Plan 017 later starts partial HTTP/2 and HTTP/3 wire codec work.

## Implemented Scope

- GET and HEAD static file serving.
- POST, PUT, PATCH, and DELETE handler scaffold responses.
- OPTIONS response with allowed methods.
- Persistent HTTP/1.1 connections and keep-alive.
- Basic request pipelining by buffering later requests until the active response is written.
- `Content-Length` request body framing.
- `Transfer-Encoding: chunked` request body decoding.
- URL decoding.
- Query string and query parameter parsing.
- Header parsing with case-insensitive lookup through lower-case storage.
- Request body storage on `rimau::http::Request`.
- JSON request detection through `Content-Type`.
- JSON response helper and method scaffold JSON output.
- Static file serving from SQLite `document_root`.
- Single byte-range static file responses.
- MIME type detection for common text, image, video, wasm, and pdf extensions.
- Gzip compression for compressible static responses when the client sends `Accept-Encoding: gzip`.
- Basic RFC 6455 WebSocket upgrade with text/binary echo, ping/pong, and close handling.

## Deliberate Limits

- Request bodies are buffered in memory up to `max_request_bytes`; no streaming body API yet.
- Chunked trailers are consumed for message framing but not exposed to handlers.
- JSON is not parsed into a structured object; scaffold handlers only echo escaped body text and metadata.
- PUT, PATCH, and DELETE do not mutate files.
- Request pipelining has basic ordering behavior only; broad stress tests and backpressure policy are still pending.
- Static file range support is single-range only; multipart ranges and `If-Range` are not implemented.
- Compression is gzip only through zlib; Brotli is not implemented. Plan 015 later changes zlib from a system dependency to bundled static zlib.
- Static file reads remain synchronous and memory-buffered.
- WebSocket support is a basic echo path only; fragmentation, extensions, subprotocol negotiation, application routing, and robust backpressure are not implemented.
- At this phase HTTP/2 and HTTP/3 remained config/status gates only. Plan 017 later adds partial wire codec support.

## Validation

Commands used:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Manual smoke tests used a temporary SQLite database and covered:

- OPTIONS response.
- POST, PUT, PATCH, and DELETE JSON scaffold responses.
- Chunked POST decoding.
- Single-range GET response with `206 Partial Content`.
- Gzip response when `Accept-Encoding: gzip` is present.
- Two GET requests sent over one socket for basic pipelining.
- WebSocket upgrade and text echo over a raw socket.
- SIGTERM graceful shutdown.

## Follow-Up Work

See `docs/TODO.md` for current next work. The largest HTTP/1.1 follow-ups are handler-level streaming request bodies beyond the current file-backed body spooling step, producer-side async response streaming/backpressure beyond current basic chunked response serialization, multipart ranges, Brotli, advanced slow-client protection, richer WebSocket routing, zero-copy static file serving, and broader integration/stress tests.
