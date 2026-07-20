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


def websocket_handshake(port: int, host: str):
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(3)
    request = (
        f"GET /ws HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).encode("ascii")
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


class RimauServer:
    def __init__(self, server_path: Path, runtime_root: Path, upstream_port: int):
        self.server_path = server_path
        self.root = Path(tempfile.mkdtemp(prefix="rimau-http1-network-", dir=runtime_root))
        self.document_root = self.root / "public"
        self.document_root.mkdir()
        self.database = self.root / "rimau.sqlite3"
        self.log = self.root / "rimau.log"
        self.port = free_port()
        self.process = None
        self.upstream_port = upstream_port

    def __enter__(self):
        (self.document_root / "index.html").write_text("Rimau HTTP/1 integration\n", encoding="utf-8")
        (self.document_root / "range.txt").write_bytes(b"0123456789abcdef")
        (self.document_root / "large.txt").write_text("Rimau gzip integration\n" * 64, encoding="utf-8")

        updates = [
            f"host=127.0.0.1",
            f"port={self.port}",
            f"document_root={self.document_root}",
            "http_keep_alive_timeout_seconds=1",
            "http_keep_alive_max_requests=2",
            "idle_timeout_seconds=1",
            "virtual_hosts_enabled=true",
            f"virtual_hosts=proxy.test=proxy:http://127.0.0.1:{self.upstream_port}",
        ]
        command = [str(self.server_path), "--database", str(self.database)]
        for update in updates:
            command.extend(["--set", update])
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

        log_handle = self.log.open("wb")
        self.process = subprocess.Popen(
            [str(self.server_path), "--database", str(self.database)],
            stdout=log_handle,
            stderr=subprocess.STDOUT,
        )
        log_handle.close()
        wait_for_port(self.port, self.process)
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
        test_range_and_gzip(server.port)
        test_websocket_echo(server.port)
        test_websocket_proxy(server.port, upstream)

    print("http1 network integration tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
