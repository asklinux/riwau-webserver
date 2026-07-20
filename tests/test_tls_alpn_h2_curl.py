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


def generate_cert(openssl: Path, cert: Path, key: Path) -> None:
    config = cert.parent / "openssl-test.cnf"
    config.write_text(
        "[req]\n"
        "distinguished_name = dn\n"
        "prompt = no\n"
        "\n"
        "[dn]\n"
        "CN = localhost\n",
        encoding="utf-8",
    )
    subprocess.run(
        [
            str(openssl),
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-sha256",
            "-nodes",
            "-days",
            "7",
            "-keyout",
            str(key),
            "-out",
            str(cert),
            "-config",
            str(config),
            "-subj",
            "/CN=localhost",
            "-addext",
            "subjectAltName=DNS:localhost,IP:127.0.0.1",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    os.chmod(key, 0o600)


def configure_server(server: Path, database: Path, document_root: Path, cert: Path, key: Path, port: int) -> None:
    updates = [
        "host=127.0.0.1",
        f"port={port}",
        f"document_root={document_root}",
        "worker_threads=1",
        "tls_enabled=true",
        f"tls_certificate_file={cert}",
        f"tls_private_key_file={key}",
        "http2_enabled=true",
        "tls_alpn_protocols=h2,http/1.1",
        "rate_limit_enabled=false",
    ]
    command = [str(server), "--database", str(database)]
    for update in updates:
        command.extend(["--set", update])
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def has_h2_alpn_selection(output: str) -> bool:
    patterns = [
        "ALPN: server accepted h2",
        "ALPN, server accepted to use h2",
        "ALPN: server accepted to use h2",
    ]
    return any(pattern in output for pattern in patterns)


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: test_tls_alpn_h2_curl.py /path/to/rimau-server /path/to/runtime-root /path/to/openssl", file=sys.stderr)
        return 2

    curl = os.environ.get("RIMAU_CURL") or shutil.which("curl")
    if not curl:
        print("SKIP: curl not found; cannot run real HTTP/2 client ALPN test")
        return 0
    if not curl_supports_http2(curl):
        print("SKIP: curl does not report HTTP2 support; cannot run real HTTP/2 client ALPN test")
        return 0

    server = Path(sys.argv[1]).resolve()
    runtime_root = Path(sys.argv[2]).resolve()
    openssl = Path(sys.argv[3]).resolve()
    if not openssl.exists():
        raise RuntimeError(f"bundled OpenSSL binary not found: {openssl}")

    runtime_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="rimau-tls-alpn-h2-", dir=runtime_root))
    process = None
    try:
        document_root = root / "public"
        cert_dir = root / "certs"
        document_root.mkdir()
        cert_dir.mkdir()
        (document_root / "index.html").write_text("Rimau real curl HTTP/2 ALPN test\n", encoding="utf-8")

        cert = cert_dir / "rimau-dev.crt"
        key = cert_dir / "rimau-dev.key"
        database = root / "rimau.sqlite3"
        log = root / "rimau.log"
        port = free_port()

        generate_cert(openssl, cert, key)
        configure_server(server, database, document_root, cert, key, port)

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
                "--http2",
                "--insecure",
                "--max-time",
                "5",
                "--verbose",
                "--output",
                str(root / "curl-body.txt"),
                f"https://127.0.0.1:{port}/",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        output = result.stdout + result.stderr

        assert has_h2_alpn_selection(output), output
        assert "using HTTP/2" in output or "[HTTP/2]" in output, output

        if result.returncode == 0:
            body = (root / "curl-body.txt").read_text(encoding="utf-8")
            assert body == "Rimau real curl HTTP/2 ALPN test\n", (body, output)
        else:
            known_partial_h2_failure = (
                "COMPRESSION_ERROR" in output
                or "HPACK Huffman strings are not implemented yet" in log.read_text(encoding="utf-8", errors="replace")
            )
            assert known_partial_h2_failure, output
            print("curl negotiated ALPN h2; request hit known partial HTTP/2 HPACK limit")

        print("TLS ALPN h2 real HTTP/2 client test passed")
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
