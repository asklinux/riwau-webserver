# Plan 017: HTTP/2 And HTTP/3 Wire Codecs

## Goal

Move HTTP/2 and HTTP/3 beyond config/status-only placeholders by adding tested wire-level codec primitives without falsely claiming full live protocol support.

## Implemented

- Added HTTP/2 frame parser and serializer for the 9-byte frame header, payload length, type, flags, and 31-bit stream id.
- Added HTTP/2 SETTINGS payload parser/serializer.
- Added HTTP/2 frame validation for SETTINGS, PING, GOAWAY, WINDOW_UPDATE, RST_STREAM, DATA, HEADERS, and CONTINUATION basics.
- Added HPACK baseline support for static-table indexed fields and literal fields without Huffman or dynamic table indexing.
- Added cleartext h2c prior-knowledge handling in the server: client preface plus SETTINGS is parsed, then Rimau replies SETTINGS, SETTINGS ACK, and GOAWAY.
- Added HTTP/3 QUIC variable-length integer parser/serializer.
- Added HTTP/3 frame parser/serializer.
- Added HTTP/3 SETTINGS payload parser/serializer.
- Added CTest coverage for HTTP/2 wire codec, HPACK baseline, HTTP/3 varint/frame/settings codec, and protocol capability reporting.

## Validation

Commands run on 2026-07-19:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/rimau-server --protocols
```

Manual h2c smoke:

```text
client sends HTTP/2 connection preface + empty SETTINGS
server responds SETTINGS + SETTINGS ACK + GOAWAY
```

Result:

- Build passed.
- CTest passed, 8/8 tests.
- `--protocols` reports HTTP/2 and HTTP/3 as `partial`.
- h2c smoke parsed three returned HTTP/2 frames: SETTINGS, SETTINGS ACK, and GOAWAY.

## Known Limits

Note: Plan 018 later extends the HTTP/2 side from this handshake-only state to partial cleartext h2c request serving.

- At this plan's completion point, HTTP/2 request serving was not implemented yet; Plan 018 later adds partial h2c request serving.
- HTTP/2 stream lifecycle, flow control, priority, continuation assembly, broad HPACK behavior, HPACK Huffman, and dynamic header table are not implemented.
- HTTP/2 over TLS ALPN `h2` is not advertised yet.
- HTTP/3 live serving is not implemented yet.
- HTTP/3 UDP listener, QUIC transport, QUIC TLS 1.3 handshake, QPACK, stream lifecycle, request adapter, and ALPN `h3` are not implemented.
- At this plan's completion point, `http2_enabled=true` only enabled the partial h2c preface/SETTINGS/GOAWAY path; Plan 018 later changes that to partial h2c request serving.
- `http3_enabled=true` is still a status/config gate for live HTTP/3.
