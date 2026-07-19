#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build}"
host="${HOST:-127.0.0.1}"
port="${PORT:-18080}"
database_path="${DATABASE:-${build_dir}/rimau-dev.sqlite3}"

cmake -S . -B "${build_dir}" -DRIMAU_ENABLE_TESTS=ON
cmake --build "${build_dir}"

"./${build_dir}/rimau-server" --database "${database_path}" --set "host=${host}" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "port=${port}" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "document_root=public" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "max_request_bytes=65536" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "http_keep_alive_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "http_keep_alive_timeout_seconds=15" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "http_keep_alive_max_requests=100" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "listen_backlog=128" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "server_name=Rimau Web Server" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "worker_threads=0" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "epoll_max_events=256" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reuse_port_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tcp_keepalive_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tcp_keepalive_idle_seconds=60" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tcp_keepalive_interval_seconds=10" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tcp_keepalive_probe_count=5" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "graceful_shutdown_timeout_seconds=5" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "connection_pool_size=1024" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "request_timeout_seconds=30" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "header_timeout_seconds=10" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "body_timeout_seconds=30" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "idle_timeout_seconds=15" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "global_connection_limit=10000" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "per_ip_connection_limit=100" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "rate_limit_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "rate_limit_requests_per_minute=600" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "ip_allowlist=" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "ip_blocklist=" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "websocket_max_frame_bytes=65536" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_headers_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_header_content_security_policy=default-src 'self'; frame-ancestors 'self'; base-uri 'self'" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_header_strict_transport_security=max-age=31536000" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_header_x_content_type_options=nosniff" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_header_x_frame_options=SAMEORIGIN" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_header_referrer_policy=no-referrer" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "security_header_cross_origin_opener_policy=same-origin" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "server_header_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "virtual_hosts_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "virtual_hosts=" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_connect_timeout_seconds=5" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_read_timeout_seconds=30" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_max_response_bytes=1048576" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_retry_count=1" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_tls_verify_upstream=false" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_circuit_breaker_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_circuit_breaker_failure_threshold=3" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "reverse_proxy_circuit_breaker_cooldown_seconds=10" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "http1_enabled=true" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "http2_enabled=false" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "http3_enabled=false" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tls_sni_hosts=" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tls_sni_certificates=" >/dev/null
"./${build_dir}/rimau-server" --database "${database_path}" --set "tls_enabled=false" >/dev/null

echo "Rimau Web Server running at http://${host}:${port}/"
exec "./${build_dir}/rimau-server" --database "${database_path}"
