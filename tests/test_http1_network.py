#!/usr/bin/env python3
import base64
import gzip
import hashlib
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


HTTP2_PREFACE = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
HTTP2_FRAME_DATA = 0x0
HTTP2_FRAME_HEADERS = 0x1
HTTP2_FRAME_SETTINGS = 0x4
HTTP2_FLAG_END_STREAM = 0x1
HTTP2_FLAG_ACK = 0x1
HTTP2_FLAG_END_HEADERS = 0x4

HPACK_STATIC_TABLE = [
    (":authority", ""),
    (":method", "GET"),
    (":method", "POST"),
    (":path", "/"),
    (":path", "/index.html"),
    (":scheme", "http"),
    (":scheme", "https"),
    (":status", "200"),
    (":status", "204"),
    (":status", "206"),
    (":status", "304"),
    (":status", "400"),
    (":status", "404"),
    (":status", "500"),
    ("accept-charset", ""),
    ("accept-encoding", "gzip, deflate"),
    ("accept-language", ""),
    ("accept-ranges", ""),
    ("accept", ""),
    ("access-control-allow-origin", ""),
    ("age", ""),
    ("allow", ""),
    ("authorization", ""),
    ("cache-control", ""),
    ("content-disposition", ""),
    ("content-encoding", ""),
    ("content-language", ""),
    ("content-length", ""),
    ("content-location", ""),
    ("content-range", ""),
    ("content-type", ""),
    ("cookie", ""),
    ("date", ""),
    ("etag", ""),
    ("expect", ""),
    ("expires", ""),
    ("from", ""),
    ("host", ""),
    ("if-match", ""),
    ("if-modified-since", ""),
    ("if-none-match", ""),
    ("if-range", ""),
    ("if-unmodified-since", ""),
    ("last-modified", ""),
    ("link", ""),
    ("location", ""),
    ("max-forwards", ""),
    ("proxy-authenticate", ""),
    ("proxy-authorization", ""),
    ("range", ""),
    ("referer", ""),
    ("refresh", ""),
    ("retry-after", ""),
    ("server", ""),
    ("set-cookie", ""),
    ("strict-transport-security", ""),
    ("transfer-encoding", ""),
    ("user-agent", ""),
    ("vary", ""),
    ("via", ""),
    ("www-authenticate", ""),
]
HPACK_EXACT_INDEX = {entry: index + 1 for index, entry in enumerate(HPACK_STATIC_TABLE)}
HPACK_NAME_INDEX = {}
for index, (name, _) in enumerate(HPACK_STATIC_TABLE, start=1):
    HPACK_NAME_INDEX.setdefault(name, index)


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_port(port: int, process: subprocess.Popen, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"rimau-server exited early with code {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError(f"rimau-server did not accept connections on port {port}")


class HttpConnection:
    def __init__(self, port: int):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(3)
        self.buffer = b""

    def close(self) -> None:
        self.sock.close()

    def send(self, payload: bytes) -> None:
        self.sock.sendall(payload)

    def read_response(self):
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("connection closed before response headers")
            self.buffer += chunk

        raw_headers, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        lines = raw_headers.decode("iso-8859-1").split("\r\n")
        version, status_text, reason = lines[0].split(" ", 2)
        headers = {}
        for line in lines[1:]:
            name, value = line.split(":", 1)
            headers[name.lower()] = value.strip()

        length = int(headers.get("content-length", "0"))
        while len(self.buffer) < length:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("connection closed before response body")
            self.buffer += chunk

        body = self.buffer[:length]
        self.buffer = self.buffer[length:]
        return int(status_text), reason, headers, body


def http_request(port: int, request: bytes):
    connection = HttpConnection(port)
    try:
        connection.send(request)
        return connection.read_response()
    finally:
        connection.close()


def wait_for_log_contains(path: Path, needle: str, timeout: float = 3.0) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            content = path.read_text(encoding="utf-8", errors="replace")
            if needle in content:
                return content
        time.sleep(0.05)
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def assert_rejected_request(port: int, name: str, request: bytes, expected_status: int = 400) -> None:
    connection = HttpConnection(port)
    try:
        connection.send(request)
        status, _, headers, body = connection.read_response()
        assert status == expected_status, (name, status, headers, body[:200])
        assert headers.get("connection", "").lower() == "close", (name, headers)
        assert body, (name, body)

        connection.sock.settimeout(1)
        trailing = connection.sock.recv(4096)
        assert b"HTTP/1.1 200 OK" not in trailing, (name, trailing)
    finally:
        connection.close()


def assert_new_connection_closed(port: int, name: str) -> None:
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(2)
    try:
        try:
            sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            data = sock.recv(4096)
        except (BrokenPipeError, ConnectionResetError):
            data = b""
        assert data == b"", (name, data[:200])
    finally:
        sock.close()


def websocket_accept(key: str) -> str:
    guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    digest = hashlib.sha1((key + guid).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def websocket_client_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    mask = b"\x11\x22\x33\x44"
    header = bytearray([0x80 | opcode])
    if len(payload) < 126:
        header.append(0x80 | len(payload))
    elif len(payload) <= 0xFFFF:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", len(payload)))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack("!Q", len(payload)))
    header.extend(mask)
    header.extend(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return bytes(header)


def websocket_server_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    header = bytearray([0x80 | opcode])
    if len(payload) < 126:
        header.append(len(payload))
    elif len(payload) <= 0xFFFF:
        header.append(126)
        header.extend(struct.pack("!H", len(payload)))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", len(payload)))
    header.extend(payload)
    return bytes(header)


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise RuntimeError("socket closed while reading frame")
        data += chunk
    return data


def read_websocket_frame(sock: socket.socket):
    first, second = read_exact(sock, 2)
    opcode = first & 0x0F
    masked = (second & 0x80) != 0
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", read_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(sock, 8))[0]
    mask = read_exact(sock, 4) if masked else b""
    payload = read_exact(sock, length)
    if masked:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


def websocket_upgrade_request(host: str, path: str = "/ws", key: str | None = None) -> bytes:
    if key is None:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
    return (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).encode("ascii")


def websocket_handshake(port: int, host: str):
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(3)
    request = websocket_upgrade_request(host, key=key)
    sock.sendall(request)
    response = b""
    while b"\r\n\r\n" not in response:
        response += sock.recv(4096)
    if b"HTTP/1.1 101 Switching Protocols" not in response:
        raise RuntimeError(response.decode("latin1", errors="replace"))
    expected = websocket_accept(key).encode("ascii")
    if expected not in response:
        raise RuntimeError("websocket accept key mismatch")
    return sock


def hpack_encode_integer(value: int, prefix_bits: int, prefix_mask: int) -> bytes:
    max_prefix = (1 << prefix_bits) - 1
    if value < max_prefix:
        return bytes([prefix_mask | value])

    output = bytearray([prefix_mask | max_prefix])
    value -= max_prefix
    while value >= 128:
        output.append((value % 128) + 128)
        value //= 128
    output.append(value)
    return bytes(output)


def hpack_decode_integer(data: bytes, offset: int, prefix_bits: int):
    max_prefix = (1 << prefix_bits) - 1
    value = data[offset] & max_prefix
    offset += 1
    if value < max_prefix:
        return value, offset

    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value += (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return value, offset
        shift += 7


def hpack_encode_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return hpack_encode_integer(len(encoded), 7, 0x00) + encoded


def hpack_decode_string(data: bytes, offset: int):
    if data[offset] & 0x80:
        raise RuntimeError("HPACK Huffman strings are not supported by this test helper")
    size, offset = hpack_decode_integer(data, offset, 7)
    value = data[offset:offset + size].decode("utf-8")
    return value, offset + size


def hpack_static_entry(index: int):
    if index <= 0 or index > len(HPACK_STATIC_TABLE):
        raise RuntimeError(f"HPACK static index out of range: {index}")
    return HPACK_STATIC_TABLE[index - 1]


def hpack_encode_header_block(headers) -> bytes:
    output = bytearray()
    for name, value in headers:
        exact = HPACK_EXACT_INDEX.get((name, value))
        if exact:
            output.extend(hpack_encode_integer(exact, 7, 0x80))
            continue

        name_index = HPACK_NAME_INDEX.get(name, 0)
        output.extend(hpack_encode_integer(name_index, 4, 0x00))
        if name_index == 0:
            output.extend(hpack_encode_string(name))
        output.extend(hpack_encode_string(value))
    return bytes(output)


def hpack_decode_header_block(block: bytes):
    headers = []
    offset = 0
    while offset < len(block):
        byte = block[offset]
        if byte & 0x80:
            index, offset = hpack_decode_integer(block, offset, 7)
            headers.append(hpack_static_entry(index))
            continue

        if byte & 0x40:
            name_index, offset = hpack_decode_integer(block, offset, 6)
        else:
            name_index, offset = hpack_decode_integer(block, offset, 4)

        if name_index == 0:
            name, offset = hpack_decode_string(block, offset)
        else:
            name, _ = hpack_static_entry(name_index)
        value, offset = hpack_decode_string(block, offset)
        headers.append((name, value))
    return headers


def http2_frame(frame_type: int, flags: int = 0, stream_id: int = 0, payload: bytes = b"") -> bytes:
    return (
        len(payload).to_bytes(3, "big")
        + bytes([frame_type, flags])
        + (stream_id & 0x7FFFFFFF).to_bytes(4, "big")
        + payload
    )


def read_http2_frame(sock: socket.socket):
    header = read_exact(sock, 9)
    length = int.from_bytes(header[:3], "big")
    frame_type = header[3]
    flags = header[4]
    stream_id = int.from_bytes(header[5:9], "big") & 0x7FFFFFFF
    return frame_type, flags, stream_id, read_exact(sock, length)


def http2_get(port: int, path: str, authority: str = "localhost"):
    headers = [
        (":method", "GET"),
        (":scheme", "http"),
        (":path", path),
        (":authority", authority),
    ]
    request = (
        HTTP2_PREFACE
        + http2_frame(HTTP2_FRAME_SETTINGS)
        + http2_frame(
            HTTP2_FRAME_HEADERS,
            HTTP2_FLAG_END_HEADERS | HTTP2_FLAG_END_STREAM,
            1,
            hpack_encode_header_block(headers),
        )
    )

    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(5)
    response_headers = {}
    response_body = b""
    try:
        sock.sendall(request)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            frame_type, flags, stream_id, payload = read_http2_frame(sock)
            if frame_type == HTTP2_FRAME_SETTINGS and (flags & HTTP2_FLAG_ACK) == 0:
                sock.sendall(http2_frame(HTTP2_FRAME_SETTINGS, HTTP2_FLAG_ACK))
                continue
            if stream_id != 1:
                continue
            if frame_type == HTTP2_FRAME_HEADERS:
                for name, value in hpack_decode_header_block(payload):
                    response_headers[name] = value
                if flags & HTTP2_FLAG_END_STREAM:
                    break
            elif frame_type == HTTP2_FRAME_DATA:
                response_body += payload
                if flags & HTTP2_FLAG_END_STREAM:
                    break
        return response_headers, response_body
    finally:
        sock.close()


def assert_waf_http_response(name: str, response) -> None:
    status, _, headers, body = response
    assert status == 403, (name, response)
    assert headers.get("x-rimau-waf") == "blocked", (name, headers)
    assert headers.get("x-rimau-waf-rule-id") == "930100", (name, headers)
    assert b"Forbidden by Rimau ModSecurity compatible WAF" in body, (name, body)


class WebSocketUpstream:
    def __init__(self):
        self.port = free_port()
        self.ready = threading.Event()
        self.failed = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()
        if not self.ready.wait(timeout=3):
            raise RuntimeError("websocket upstream did not start")

    def join(self) -> None:
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            raise RuntimeError("websocket upstream did not finish")
        if self.failed is not None:
            raise self.failed

    def _run(self) -> None:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(("127.0.0.1", self.port))
                listener.listen(1)
                self.ready.set()
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(5)
                    data = b""
                    while b"\r\n\r\n" not in data:
                        data += conn.recv(4096)
                    headers = data.decode("iso-8859-1").split("\r\n")
                    key = ""
                    for line in headers:
                        if line.lower().startswith("sec-websocket-key:"):
                            key = line.split(":", 1)[1].strip()
                    if not key:
                        raise RuntimeError("upstream did not receive websocket key")
                    response = (
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        f"Sec-WebSocket-Accept: {websocket_accept(key)}\r\n"
                        "\r\n"
                    ).encode("ascii")
                    conn.sendall(response)
                    opcode, payload = read_websocket_frame(conn)
                    if opcode != 0x1:
                        raise RuntimeError(f"unexpected upstream opcode {opcode}")
                    conn.sendall(websocket_server_frame(b"up:" + payload))
        except BaseException as error:
            self.failed = error
            self.ready.set()


class HttpUpstream:
    def __init__(self, name: str, max_requests: int = 1):
        self.name = name
        self.max_requests = max_requests
        self.port = free_port()
        self.ready = threading.Event()
        self.failed = None
        self.requests = []
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()
        if not self.ready.wait(timeout=3):
            raise RuntimeError(f"http upstream {self.name} did not start")

    def join(self) -> None:
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            raise RuntimeError(f"http upstream {self.name} did not finish")
        if self.failed is not None:
            raise self.failed

    def _run(self) -> None:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(("127.0.0.1", self.port))
                listener.listen(self.max_requests)
                self.ready.set()
                for _ in range(self.max_requests):
                    conn, _ = listener.accept()
                    with conn:
                        conn.settimeout(5)
                        data = b""
                        while b"\r\n\r\n" not in data:
                            chunk = conn.recv(4096)
                            if not chunk:
                                break
                            data += chunk
                        self.requests.append(data)
                        body = f"upstream:{self.name}\n".encode("ascii")
                        response = (
                            b"HTTP/1.1 200 OK\r\n"
                            b"Content-Type: text/plain\r\n"
                            + b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n"
                            b"Connection: close\r\n"
                            b"\r\n"
                            + body
                        )
                        conn.sendall(response)
        except BaseException as error:
            self.failed = error
            self.ready.set()


class SlowHttpUpstream:
    def __init__(self, delay_seconds: float = 1.2):
        self.delay_seconds = delay_seconds
        self.port = free_port()
        self.ready = threading.Event()
        self.request_received = threading.Event()
        self.failed = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()
        if not self.ready.wait(timeout=3):
            raise RuntimeError("slow http upstream did not start")

    def join(self) -> None:
        self.thread.join(timeout=4)
        if self.thread.is_alive():
            raise RuntimeError("slow http upstream did not finish")
        if self.failed is not None:
            raise self.failed

    def _run(self) -> None:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(("127.0.0.1", self.port))
                listener.listen(1)
                self.ready.set()
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(5)
                    data = b""
                    while b"\r\n\r\n" not in data:
                        chunk = conn.recv(4096)
                        if not chunk:
                            break
                        data += chunk
                    self.request_received.set()
                    time.sleep(self.delay_seconds)
                    body = b"upstream:slow\n"
                    response = (
                        b"HTTP/1.1 200 OK\r\n"
                        b"Content-Type: text/plain\r\n"
                        + b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n"
                        b"Connection: close\r\n"
                        b"\r\n"
                        + body
                    )
                    conn.sendall(response)
        except BaseException as error:
            self.failed = error
            self.ready.set()
            self.request_received.set()


class RimauServer:
    def __init__(
        self,
        server_path: Path,
        runtime_root: Path,
        upstream_port: int | None = None,
        extra_updates=None,
        env_updates=None,
    ):
        self.server_path = server_path
        self.root = Path(tempfile.mkdtemp(prefix="rimau-http1-network-", dir=runtime_root))
        self.document_root = self.root / "public"
        self.document_root.mkdir()
        self.database = self.root / "rimau.sqlite3"
        self.log = self.root / "rimau.log"
        self.port = free_port()
        self.process = None
        self.upstream_port = upstream_port
        self.extra_updates = list(extra_updates or [])
        self.env_updates = dict(env_updates or {})

    def __enter__(self):
        (self.document_root / "index.html").write_text("Rimau HTTP/1 integration\n", encoding="utf-8")
        (self.document_root / "home.html").write_text("Rimau custom index\n", encoding="utf-8")
        (self.document_root / "error.html").write_text("Rimau custom error\n", encoding="utf-8")
        (self.document_root / "range.txt").write_bytes(b"0123456789abcdef")
        (self.document_root / "large.txt").write_text("Rimau gzip integration\n" * 64, encoding="utf-8")

        updates = [
            f"host=127.0.0.1",
            f"port={self.port}",
            f"document_root={self.document_root}",
            "directory_index=home.html",
            "error_page=error.html",
            "max_request_bytes=262144",
            "http_keep_alive_timeout_seconds=1",
            "http_keep_alive_max_requests=2",
            "idle_timeout_seconds=1",
            "virtual_hosts_enabled=true",
        ]
        if self.upstream_port is not None:
            updates.append(f"virtual_hosts=proxy.test=proxy:http://127.0.0.1:{self.upstream_port}")
        updates.extend(self.extra_updates)
        command = [str(self.server_path), "--database", str(self.database)]
        for update in updates:
            command.extend(["--set", update])
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

        log_handle = self.log.open("wb")
        self.process = subprocess.Popen(
            [str(self.server_path), "--database", str(self.database)],
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            env={**os.environ, **self.env_updates},
        )
        log_handle.close()
        wait_for_port(self.port, self.process)
        time.sleep(0.2)
        return self

    def __exit__(self, exc_type, exc, tb):
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        shutil.rmtree(self.root, ignore_errors=True)


def test_keep_alive_pipelining_and_max_requests(port: int) -> None:
    connection = HttpConnection(port)
    try:
        request = (
            b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
            b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n"
            b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
        )
        connection.send(request)
        first = connection.read_response()
        second = connection.read_response()
        assert first[0] == 200, first
        assert second[0] == 200, second
        assert first[2].get("connection", "").lower() == "keep-alive", first[2]
        assert second[2].get("connection", "").lower() == "close", second[2]
        connection.sock.settimeout(1)
        trailing = connection.sock.recv(4096)
        assert b"HTTP/1.1 200 OK" not in trailing, trailing
    finally:
        connection.close()


def test_idle_timeout(port: int) -> None:
    connection = HttpConnection(port)
    try:
        connection.send(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n")
        response = connection.read_response()
        assert response[0] == 200, response
        time.sleep(2.5)
        connection.sock.settimeout(2)
        closed = connection.sock.recv(1)
        assert closed == b"", closed
    finally:
        connection.close()


def test_chunked_body(port: int) -> None:
    request = (
        b"POST /submit?kind=chunked HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Content-Type: application/json\r\n"
        b"Transfer-Encoding: chunked\r\n"
        b"Connection: close\r\n"
        b"\r\n"
        b"5\r\nhello\r\n"
        b"6\r\n world\r\n"
        b"0\r\n\r\n"
    )
    status, _, headers, body = http_request(port, request)
    assert status == 200, (status, body)
    assert headers.get("content-type", "").startswith("application/json"), headers
    assert b'"body_size":11' in body, body
    assert b'"body":"hello world"' in body, body


def test_large_file_backed_body(port: int) -> None:
    payload = b"a" * 70000
    request = (
        b"POST /large-body HTTP/1.1\r\n"
        b"Host: localhost\r\n"
        b"Content-Type: application/octet-stream\r\n"
        b"Content-Length: " + str(len(payload)).encode("ascii") + b"\r\n"
        b"Connection: close\r\n"
        b"\r\n"
        + payload
    )
    status, _, headers, body = http_request(port, request)
    assert status == 200, (status, body[:200])
    assert headers.get("content-type", "").startswith("application/json"), headers
    assert b'"body_size":70000' in body, body[:200]
    assert b'"body_spooled":true' in body, body[-200:]
    assert b'"body_truncated":true' in body, body[-200:]


def test_range_and_gzip(port: int) -> None:
    status, _, headers, body = http_request(
        port,
        b"GET /range.txt HTTP/1.1\r\nHost: localhost\r\nRange: bytes=2-5\r\nConnection: close\r\n\r\n",
    )
    assert status == 206, (status, headers, body)
    assert headers.get("content-range") == "bytes 2-5/16", headers
    assert body == b"2345", body

    status, _, headers, body = http_request(
        port,
        b"GET /large.txt HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\nConnection: close\r\n\r\n",
    )
    assert status == 200, (status, headers)
    assert headers.get("content-encoding") == "gzip", headers
    assert gzip.decompress(body) == b"Rimau gzip integration\n" * 64


def test_directory_index_and_error_page(port: int) -> None:
    status, _, _, body = http_request(
        port,
        b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
    )
    assert status == 200, (status, body)
    assert body == b"Rimau custom index\n", body

    status, _, _, body = http_request(
        port,
        b"GET /missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
    )
    assert status == 404, (status, body)
    assert body == b"Rimau custom error\n", body


def test_request_smuggling_rejections(port: int) -> None:
    smuggled = b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    cases = [
        (
            "duplicate content-length",
            b"POST /bad HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Content-Length: 1\r\n"
            b"Content-Length: 1\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            b"x"
            + smuggled,
        ),
        (
            "invalid content-length",
            b"POST /bad HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Content-Length: nope\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            + smuggled,
        ),
        (
            "transfer-encoding content-length conflict",
            b"POST /bad HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Content-Length: 4\r\n"
            b"Transfer-Encoding: chunked\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            b"0\r\n\r\n"
            + smuggled,
        ),
        (
            "unsupported transfer-encoding",
            b"POST /bad HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Transfer-Encoding: gzip\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            + smuggled,
        ),
        (
            "obs-fold header",
            b"GET /bad HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b" X-Injected: yes\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            + smuggled,
        ),
        (
            "bare line feed",
            b"GET /bad HTTP/1.1\n"
            b"Host: localhost\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            + smuggled,
        ),
        (
            "bare carriage return",
            b"GET /bad HTTP/1.1\r\n"
            b"Host: localhost\rBad: yes\r\n"
            b"Connection: keep-alive\r\n"
            b"\r\n"
            + smuggled,
        ),
    ]

    for name, request in cases:
        assert_rejected_request(port, name, request)


def test_websocket_echo(port: int) -> None:
    sock = websocket_handshake(port, "localhost")
    try:
        sock.sendall(websocket_client_frame(b"rimau"))
        opcode, payload = read_websocket_frame(sock)
        assert opcode == 0x1, opcode
        assert payload == b"rimau", payload
    finally:
        sock.close()


def test_websocket_proxy(port: int, upstream: WebSocketUpstream) -> None:
    sock = websocket_handshake(port, "proxy.test")
    try:
        sock.sendall(websocket_client_frame(b"proxy"))
        opcode, payload = read_websocket_frame(sock)
        assert opcode == 0x1, opcode
        assert payload == b"up:proxy", payload
    finally:
        sock.close()
    upstream.join()


def test_reverse_proxy_failover_policy(server_path: Path, runtime_root: Path) -> None:
    primary = HttpUpstream("primary", max_requests=2)
    secondary_port = free_port()
    primary.start()
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=false",
        "reverse_proxy_retry_count=1",
        "reverse_proxy_load_balancing_policy=failover",
        f"virtual_hosts=proxy.test=proxy:http://127.0.0.1:{primary.port},http://127.0.0.1:{secondary_port}",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        for _ in range(2):
            status, _, _, body = http_request(
                server.port,
                b"GET /proxy-policy HTTP/1.1\r\nHost: proxy.test\r\nConnection: close\r\n\r\n",
            )
            assert status == 200, (status, body)
            assert body == b"upstream:primary\n", body

    primary.join()
    assert len(primary.requests) == 2, primary.requests


def test_reverse_proxy_upstream_io_does_not_block_worker(server_path: Path, runtime_root: Path) -> None:
    upstream = SlowHttpUpstream(delay_seconds=1.2)
    upstream.start()
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=false",
        "reverse_proxy_read_timeout_seconds=5",
        f"virtual_hosts=proxy.test=proxy:http://127.0.0.1:{upstream.port}",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        proxy = HttpConnection(server.port)
        try:
            proxy.send(b"GET /slow HTTP/1.1\r\nHost: proxy.test\r\nConnection: close\r\n\r\n")
            assert upstream.request_received.wait(timeout=3), "slow upstream did not receive proxy request"

            started = time.monotonic()
            status, _, _, body = http_request(
                server.port,
                b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            )
            elapsed = time.monotonic() - started
            assert status == 200, (status, body)
            assert body == b"Rimau custom index\n", body
            assert elapsed < 0.8, elapsed

            status, _, _, body = proxy.read_response()
            assert status == 200, (status, body)
            assert body == b"upstream:slow\n", body
        finally:
            proxy.close()

    upstream.join()


def test_script_vhost_does_not_shell_out_to_system_runtime(server_path: Path, runtime_root: Path) -> None:
    fake_root = Path(tempfile.mkdtemp(prefix="rimau-fake-runtime-", dir=runtime_root))
    fake_bin = fake_root / "bin"
    fake_bin.mkdir()
    marker = fake_root / "runtime-invoked.txt"
    try:
        for runtime in ("php", "python", "perl"):
            executable = fake_bin / runtime
            executable.write_text(
                "#!/bin/sh\n"
                f"echo {runtime} >> {marker}\n"
                "exit 42\n",
                encoding="utf-8",
            )
            executable.chmod(0o755)

        updates = [
            "worker_threads=1",
            "rate_limit_enabled=false",
            (
                "virtual_hosts="
                "php.test=script:php:public/app;"
                "python.test=script:python:public/app;"
                "perl.test=script:perl:public/app"
            ),
        ]
        env_updates = {
            "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
        }
        with RimauServer(server_path, runtime_root, extra_updates=updates, env_updates=env_updates) as server:
            for host, runtime in (("php.test", "php"), ("python.test", "python"), ("perl.test", "perl")):
                status, _, headers, body = http_request(
                    server.port,
                    f"GET /index HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode("ascii"),
                )
                assert status == 501, (host, status, headers, body)
                assert headers.get("x-rimau-runtime-status") == "planned", (host, headers)
                assert f'"runtime":"{runtime}"'.encode("ascii") in body, (host, body)
                assert b'"implemented":false' in body, (host, body)

        assert not marker.exists(), marker.read_text(encoding="utf-8") if marker.exists() else marker
    finally:
        shutil.rmtree(fake_root, ignore_errors=True)


def test_rate_limit(server_path: Path, runtime_root: Path) -> None:
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=true",
        "rate_limit_requests_per_minute=1",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        first = http_request(
            server.port,
            b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        )
        assert first[0] == 200, first

        second = http_request(
            server.port,
            b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        )
        assert second[0] == 429, second
        assert b"Too Many Requests" in second[3], second


def test_connection_limits(server_path: Path, runtime_root: Path) -> None:
    with RimauServer(
        server_path,
        runtime_root,
        extra_updates=[
            "worker_threads=1",
            "rate_limit_enabled=false",
            "per_ip_connection_limit=1",
            "global_connection_limit=100",
            "header_timeout_seconds=5",
        ],
    ) as server:
        holder = socket.create_connection(("127.0.0.1", server.port), timeout=3)
        try:
            time.sleep(0.2)
            assert_new_connection_closed(server.port, "per-ip connection limit")
        finally:
            holder.close()

    with RimauServer(
        server_path,
        runtime_root,
        extra_updates=[
            "worker_threads=1",
            "rate_limit_enabled=false",
            "per_ip_connection_limit=100",
            "global_connection_limit=1",
            "header_timeout_seconds=5",
        ],
    ) as server:
        holder = socket.create_connection(("127.0.0.1", server.port), timeout=3)
        try:
            time.sleep(0.2)
            assert_new_connection_closed(server.port, "global connection limit")
        finally:
            holder.close()


def test_timeout_and_slow_client_behavior(server_path: Path, runtime_root: Path) -> None:
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=false",
        "request_timeout_seconds=5",
        "header_timeout_seconds=1",
        "body_timeout_seconds=1",
        "idle_timeout_seconds=1",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        header = HttpConnection(server.port)
        try:
            header.send(b"GET /slow HTTP/1.1\r\nHost: localhost")
            time.sleep(2.5)
            response = header.read_response()
            assert response[0] == 408, response
        finally:
            header.close()

        body = HttpConnection(server.port)
        try:
            body.send(
                b"POST /slow-body HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                b"Content-Length: 10\r\n"
                b"Connection: close\r\n"
                b"\r\n"
                b"hi"
            )
            time.sleep(2.5)
            response = body.read_response()
            assert response[0] == 408, response
        finally:
            body.close()


def test_request_timeout(server_path: Path, runtime_root: Path) -> None:
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=false",
        "request_timeout_seconds=1",
        "header_timeout_seconds=5",
        "body_timeout_seconds=5",
        "idle_timeout_seconds=5",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        connection = HttpConnection(server.port)
        try:
            connection.send(b"GET /request-timeout HTTP/1.1\r\nHost: localhost")
            time.sleep(2.5)
            response = connection.read_response()
            assert response[0] == 408, response
        finally:
            connection.close()


def test_waf_entry_points(server_path: Path, runtime_root: Path) -> None:
    proxy_upstream_port = free_port()
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=false",
        "http2_enabled=true",
        "modsecurity_enabled=true",
        "modsecurity_owasp_crs_enabled=true",
        "modsecurity_blocking_enabled=true",
        "modsecurity_anomaly_threshold=5",
        "modsecurity_max_inspection_bytes=131072",
        f"virtual_hosts=proxy.test=proxy:http://127.0.0.1:{proxy_upstream_port}",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        assert_waf_http_response(
            "HTTP/1.1 WAF block",
            http_request(
                server.port,
                b"GET /../../etc/passwd HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            ),
        )

        assert_waf_http_response(
            "WebSocket local WAF block",
            http_request(
                server.port,
                websocket_upgrade_request("localhost", "/../../etc/passwd"),
            ),
        )

        assert_waf_http_response(
            "WebSocket proxy WAF block",
            http_request(
                server.port,
                websocket_upgrade_request("proxy.test", "/../../etc/passwd"),
            ),
        )

        headers, body = http2_get(server.port, "/../../etc/passwd")
        assert headers.get(":status") == "403", (headers, body)
        assert headers.get("x-rimau-waf") == "blocked", (headers, body)
        assert headers.get("x-rimau-waf-rule-id") == "930100", (headers, body)
        assert b"Forbidden by Rimau ModSecurity compatible WAF" in body, (headers, body)


def scanner_request(host: str, target: str = "/") -> bytes:
    return (
        f"GET {target} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: sqlmap/1.8\r\nConnection: close\r\n\r\n"
    ).encode("ascii")


def test_virtual_host_waf_overrides(server_path: Path, runtime_root: Path) -> None:
    updates = [
        "worker_threads=1",
        "rate_limit_enabled=false",
        "modsecurity_enabled=true",
        "modsecurity_owasp_crs_enabled=true",
        "modsecurity_blocking_enabled=true",
        "modsecurity_anomaly_threshold=5",
        "virtual_host_waf_overrides=disabled.test=enabled:false;exceptions.test=rule_exceptions:913100;threshold.test=threshold:10",
    ]
    with RimauServer(server_path, runtime_root, extra_updates=updates) as server:
        status, _, headers, body = http_request(server.port, scanner_request("localhost", "/?token=secret"))
        assert status == 403, (status, headers, body)
        assert headers.get("x-rimau-waf") == "blocked", headers
        assert headers.get("x-rimau-waf-rule-id") == "913100", headers
        log_content = wait_for_log_contains(server.log, '"event":"rimau_waf_audit"')
        assert '"outcome":"blocked"' in log_content, log_content
        assert '"rule_id":913100' in log_content, log_content
        assert '"path":"/"' in log_content, log_content
        assert "token=secret" not in log_content, log_content

        for host in ("disabled.test", "exceptions.test", "threshold.test"):
            status, _, headers, body = http_request(server.port, scanner_request(host))
            assert status == 200, (host, status, headers, body)
            assert headers.get("x-rimau-waf") != "blocked", (host, headers)
            assert b"Rimau custom index" in body, (host, body)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_http1_network.py /path/to/rimau-server /path/to/runtime-root", file=sys.stderr)
        return 2

    server_path = Path(sys.argv[1]).resolve()
    runtime_root = Path(sys.argv[2]).resolve()
    runtime_root.mkdir(parents=True, exist_ok=True)

    upstream = WebSocketUpstream()
    upstream.start()
    with RimauServer(server_path, runtime_root, upstream.port) as server:
        test_keep_alive_pipelining_and_max_requests(server.port)
        test_idle_timeout(server.port)
        test_chunked_body(server.port)
        test_large_file_backed_body(server.port)
        test_range_and_gzip(server.port)
        test_directory_index_and_error_page(server.port)
        test_request_smuggling_rejections(server.port)
        test_websocket_echo(server.port)
        test_websocket_proxy(server.port, upstream)

    test_rate_limit(server_path, runtime_root)
    test_connection_limits(server_path, runtime_root)
    test_timeout_and_slow_client_behavior(server_path, runtime_root)
    test_request_timeout(server_path, runtime_root)
    test_reverse_proxy_failover_policy(server_path, runtime_root)
    test_reverse_proxy_upstream_io_does_not_block_worker(server_path, runtime_root)
    test_script_vhost_does_not_shell_out_to_system_runtime(server_path, runtime_root)
    test_waf_entry_points(server_path, runtime_root)
    test_virtual_host_waf_overrides(server_path, runtime_root)

    print("http1 network integration tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
