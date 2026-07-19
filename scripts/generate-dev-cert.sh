#!/usr/bin/env bash
set -euo pipefail

cert_dir="${CERT_DIR:-certs}"
cert_file="${TLS_CERT_FILE:-${cert_dir}/rimau-dev.crt}"
key_file="${TLS_KEY_FILE:-${cert_dir}/rimau-dev.key}"
openssl_bin="${OPENSSL_BIN:-${BUILD_DIR:-build}/_deps/openssl/install/bin/openssl}"

mkdir -p "${cert_dir}"

if [[ -f "${cert_file}" && -f "${key_file}" ]]; then
  echo "Using existing TLS certificate: ${cert_file}"
  exit 0
fi

if [[ ! -x "${openssl_bin}" ]]; then
  echo "Bundled OpenSSL binary not found: ${openssl_bin}" >&2
  echo "Run: cmake -S . -B ${BUILD_DIR:-build} && cmake --build ${BUILD_DIR:-build}" >&2
  exit 1
fi

config_file="$(mktemp)"
trap 'rm -f "${config_file}"' EXIT

cat > "${config_file}" <<'EOF_CONFIG'
[req]
distinguished_name = dn
prompt = no

[dn]
CN = localhost
EOF_CONFIG

"${openssl_bin}" req \
  -x509 \
  -newkey rsa:2048 \
  -sha256 \
  -nodes \
  -days 365 \
  -config "${config_file}" \
  -keyout "${key_file}" \
  -out "${cert_file}" \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

chmod 600 "${key_file}"
echo "Generated TLS certificate: ${cert_file}"
