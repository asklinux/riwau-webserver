#!/usr/bin/env python3
import shutil
import subprocess
import sys
from pathlib import Path


def run_tool(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, check=False)


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"required tool not found: {name}")
    return path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_static_elf.py /path/to/rimau-server true|false", file=sys.stderr)
        return 2

    server = Path(sys.argv[1]).resolve()
    expect_static = sys.argv[2].lower() in {"1", "true", "on", "yes"}

    if not expect_static:
        print("SKIP: fully static ELF checks disabled for this build")
        return 0

    if not server.exists():
        raise RuntimeError(f"rimau-server not found: {server}")

    ldd = require_tool("ldd")
    file_tool = require_tool("file")
    readelf = require_tool("readelf")

    ldd_result = run_tool([ldd, str(server)])
    ldd_output = (ldd_result.stdout + ldd_result.stderr).strip()
    if "not a dynamic executable" not in ldd_output.lower():
        raise RuntimeError(f"ldd did not report a static executable:\n{ldd_output}")

    file_result = run_tool([file_tool, str(server)])
    file_output = (file_result.stdout + file_result.stderr).strip()
    file_lower = file_output.lower()
    if "dynamically linked" in file_lower or (
        "statically linked" not in file_lower and "static-pie" not in file_lower
    ):
        raise RuntimeError(f"file did not report a static ELF:\n{file_output}")

    readelf_result = run_tool([readelf, "-l", str(server)])
    if readelf_result.returncode != 0:
        raise RuntimeError((readelf_result.stdout + readelf_result.stderr).strip())

    readelf_output = readelf_result.stdout + readelf_result.stderr
    if "INTERP" in readelf_output or "Requesting program interpreter" in readelf_output:
        raise RuntimeError("readelf found a dynamic interpreter in rimau-server")

    print("rimau-server is a fully static ELF with no dynamic interpreter")
    print(ldd_output)
    print(file_output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
