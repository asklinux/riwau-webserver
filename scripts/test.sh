#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"

cmake -S . -B "${build_dir}" -DRIMAU_ENABLE_TESTS=ON
cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure
