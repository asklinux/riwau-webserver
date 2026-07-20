#!/usr/bin/env python3
import os
import shutil
import socket
import subprocess
import sys
import tempfile
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


def curl_supports_http2(curl: str) -> bool:
    result = subprocess.run([curl, "-V"], check=False, capture_output=True, text=True)
    return result.returncode == 0 and "HTTP2" in result.stdout


def has_http2_usage(output: str) -> bool:
    patterns = [
        "using HTTP/2",
        "Using HTTP2",
        "Connection state changed (HTTP/2 confirmed)",
        "[HTTP/2]",
    ]
    return any(pattern in output for pattern in patterns)


def configure_server(server: Path, database: Path, document_root: Path, port: int) -> None:
    updates = [
        "host=127.0.0.1",
        f"port={port}",
        f"document_root={document_root}",
        "worker_threads=1",
        "tls_enabled=false",
        "http2_enabled=true",
        "rate_limit_enabled=false",
    ]
    command = [str(server), "--database", str(database)]
    for update in updates:
        command.extend(["--set", update])
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_http2_h2c_curl.py /path/to/rimau-server /path/to/runtime-root", file=sys.stderr)
        return 2

    curl = os.environ.get("RIMAU_CURL") or shutil.which("curl")
    if not curl:
        print("SKIP: curl not found; cannot run real h2c client test")
        return 0
    if not curl_supports_http2(curl):
        print("SKIP: curl does not report HTTP2 support; cannot run real h2c client test")
        return 0

    server = Path(sys.argv[1]).resolve()
    runtime_root = Path(sys.argv[2]).resolve()
    runtime_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="rimau-h2c-curl-", dir=runtime_root))
    process = None
    try:
        document_root = root / "public"
        document_root.mkdir()
        (document_root / "index.html").write_text("Rimau real curl h2c test\n", encoding="utf-8")
        database = root / "rimau.sqlite3"
        log = root / "rimau.log"
        port = free_port()

        configure_server(server, database, document_root, port)
        log_handle = log.open("wb")
        process = subprocess.Popen(
            [str(server), "--database", str(database)],
            stdout=log_handle,
            stderr=subprocess.STDOUT,
        )
        log_handle.close()
        wait_for_port(port, process)

        result = subprocess.run(
            [
                curl,
                "--http2-prior-knowledge",
                "--max-time",
                "5",
                "--verbose",
                "--output",
                str(root / "curl-body.txt"),
                f"http://127.0.0.1:{port}/",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        output = result.stdout + result.stderr
        assert has_http2_usage(output), output
        assert result.returncode == 0, output + "\n--- rimau log ---\n" + log.read_text(encoding="utf-8", errors="replace")
        body = (root / "curl-body.txt").read_text(encoding="utf-8")
        assert body == "Rimau real curl h2c test\n", (body, output)

        print("h2c real HTTP/2 client test passed")
        return 0
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
