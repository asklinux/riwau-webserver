#!/usr/bin/env python3
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


CERT_PATTERN = re.compile(
    r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
    re.DOTALL,
)


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


def generate_cert(openssl: Path, cert: Path, key: Path, common_name: str, sans: list[str]) -> None:
    config = cert.parent / f"{common_name}.cnf"
    config.write_text(
        "[req]\n"
        "distinguished_name = dn\n"
        "prompt = no\n"
        "\n"
        "[dn]\n"
        f"CN = {common_name}\n",
        encoding="utf-8",
    )
    san_text = ",".join(f"DNS:{name}" for name in sans)
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
            f"/CN={common_name}",
            "-addext",
            f"subjectAltName={san_text}",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    os.chmod(key, 0o600)


def configure_server(
    server: Path,
    database: Path,
    document_root: Path,
    default_cert: Path,
    default_key: Path,
    exact_cert: Path,
    exact_key: Path,
    wildcard_cert: Path,
    wildcard_key: Path,
    port: int,
) -> None:
    sni_map = (
        f"api.example.test={exact_cert}|{exact_key};"
        f"*.tenant.test={wildcard_cert}|{wildcard_key}"
    )
    updates = [
        "host=127.0.0.1",
        f"port={port}",
        f"document_root={document_root}",
        "worker_threads=1",
        "tls_enabled=true",
        f"tls_certificate_file={default_cert}",
        f"tls_private_key_file={default_key}",
        f"tls_sni_certificates={sni_map}",
        "rate_limit_enabled=false",
    ]
    command = [str(server), "--database", str(database)]
    for update in updates:
        command.extend(["--set", update])
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def certificate_fingerprint(openssl: Path, cert: Path) -> str:
    result = subprocess.run(
        [str(openssl), "x509", "-in", str(cert), "-noout", "-fingerprint", "-sha256"],
        check=True,
        capture_output=True,
        text=True,
    )
    return parse_fingerprint(result.stdout)


def parse_fingerprint(output: str) -> str:
    for line in output.splitlines():
        if "Fingerprint=" in line:
            return line.split("=", 1)[1].replace(":", "").strip().upper()
    raise RuntimeError(f"cannot parse certificate fingerprint from: {output}")


def selected_certificate_fingerprint(openssl: Path, port: int, server_name: str | None, root: Path) -> str:
    command = [
        str(openssl),
        "s_client",
        "-connect",
        f"127.0.0.1:{port}",
        "-showcerts",
        "-verify_quiet",
    ]
    if server_name:
        command.extend(["-servername", server_name])

    result = subprocess.run(
        command,
        input="",
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )
    output = result.stdout + result.stderr
    match = CERT_PATTERN.search(output)
    if not match:
        raise RuntimeError(f"openssl s_client did not return a certificate for {server_name}: {output}")

    selected = root / f"selected-{server_name or 'default'}.pem"
    selected.write_text(match.group(0) + "\n", encoding="utf-8")
    return certificate_fingerprint(openssl, selected)


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: test_tls_sni_cert_selection.py /path/to/rimau-server /path/to/runtime-root /path/to/openssl", file=sys.stderr)
        return 2

    server = Path(sys.argv[1]).resolve()
    runtime_root = Path(sys.argv[2]).resolve()
    openssl = Path(sys.argv[3]).resolve()
    if not openssl.exists():
        raise RuntimeError(f"bundled OpenSSL binary not found: {openssl}")

    runtime_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="rimau-tls-sni-", dir=runtime_root))
    process = None
    try:
        document_root = root / "public"
        cert_dir = root / "certs"
        document_root.mkdir()
        cert_dir.mkdir()
        (document_root / "index.html").write_text("Rimau SNI certificate selection test\n", encoding="utf-8")

        default_cert = cert_dir / "default.crt"
        default_key = cert_dir / "default.key"
        exact_cert = cert_dir / "api.example.test.crt"
        exact_key = cert_dir / "api.example.test.key"
        wildcard_cert = cert_dir / "wildcard.tenant.test.crt"
        wildcard_key = cert_dir / "wildcard.tenant.test.key"

        generate_cert(openssl, default_cert, default_key, "default.test", ["default.test"])
        generate_cert(openssl, exact_cert, exact_key, "api.example.test", ["api.example.test"])
        generate_cert(openssl, wildcard_cert, wildcard_key, "*.tenant.test", ["*.tenant.test"])

        expected = {
            None: certificate_fingerprint(openssl, default_cert),
            "api.example.test": certificate_fingerprint(openssl, exact_cert),
            "app.tenant.test": certificate_fingerprint(openssl, wildcard_cert),
            "unknown.example.test": certificate_fingerprint(openssl, default_cert),
        }

        database = root / "rimau.sqlite3"
        log = root / "rimau.log"
        port = free_port()
        configure_server(
            server,
            database,
            document_root,
            default_cert,
            default_key,
            exact_cert,
            exact_key,
            wildcard_cert,
            wildcard_key,
            port,
        )

        log_handle = log.open("wb")
        process = subprocess.Popen(
            [str(server), "--database", str(database)],
            stdout=log_handle,
            stderr=subprocess.STDOUT,
        )
        log_handle.close()
        wait_for_port(port, process)

        for server_name, fingerprint in expected.items():
            selected = selected_certificate_fingerprint(openssl, port, server_name, root)
            assert selected == fingerprint, (server_name, selected, fingerprint)

        print("TLS SNI multi-certificate selection test passed")
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
