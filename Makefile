BUILD_DIR ?= build
DATABASE ?= data/rimau.sqlite3
HOST ?= 127.0.0.1
PORT ?= 18080
HTTPS_PORT ?= 18443
TLS_CERT_FILE ?= certs/rimau-dev.crt
TLS_KEY_FILE ?= certs/rimau-dev.key
DEV_DATABASE := $(BUILD_DIR)/rimau-dev.sqlite3
HTTPS_DEV_DATABASE := $(BUILD_DIR)/rimau-dev-https.sqlite3
PID_FILE := $(BUILD_DIR)/rimau-server.pid
LOG_FILE := $(BUILD_DIR)/rimau-server.log
HTTPS_PID_FILE := $(BUILD_DIR)/rimau-server-https.pid
HTTPS_LOG_FILE := $(BUILD_DIR)/rimau-server-https.log

.PHONY: configure build test check run certs dev-db https-db serve serve-https start start-https stop stop-https status status-https protocols clean

configure:
	cmake -S . -B $(BUILD_DIR) -DRIMAU_ENABLE_TESTS=ON

build: configure
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

check: build
	./$(BUILD_DIR)/rimau-server --database $(DATABASE) --check-config
	./$(BUILD_DIR)/rimau-server --protocols

protocols: build
	./$(BUILD_DIR)/rimau-server --protocols

run: build
	./$(BUILD_DIR)/rimau-server --database $(DATABASE)

certs:
	@TLS_CERT_FILE="$(TLS_CERT_FILE)" TLS_KEY_FILE="$(TLS_KEY_FILE)" ./scripts/generate-dev-cert.sh

dev-db: build
	@mkdir -p $(BUILD_DIR)
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set host=$(HOST) >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set port=$(PORT) >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set document_root=public >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set max_request_bytes=65536 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set http_keep_alive_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set http_keep_alive_timeout_seconds=15 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set http_keep_alive_max_requests=100 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set listen_backlog=128 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set "server_name=Rimau Web Server" >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set worker_threads=0 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set epoll_max_events=256 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reuse_port_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tcp_keepalive_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tcp_keepalive_idle_seconds=60 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tcp_keepalive_interval_seconds=10 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tcp_keepalive_probe_count=5 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set graceful_shutdown_timeout_seconds=5 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set connection_pool_size=1024 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set request_timeout_seconds=30 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set header_timeout_seconds=10 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set body_timeout_seconds=30 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set idle_timeout_seconds=15 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set global_connection_limit=10000 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set per_ip_connection_limit=100 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set rate_limit_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set rate_limit_requests_per_minute=600 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set ip_allowlist= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set ip_blocklist= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set websocket_max_frame_bytes=65536 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set security_headers_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set "security_header_content_security_policy=default-src 'self'; frame-ancestors 'self'; base-uri 'self'" >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set security_header_strict_transport_security=max-age=31536000 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set security_header_x_content_type_options=nosniff >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set security_header_x_frame_options=SAMEORIGIN >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set security_header_referrer_policy=no-referrer >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set security_header_cross_origin_opener_policy=same-origin >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set server_header_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set virtual_hosts_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set virtual_hosts= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_connect_timeout_seconds=5 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_read_timeout_seconds=30 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_max_response_bytes=1048576 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_retry_count=1 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_tls_verify_upstream=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_circuit_breaker_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_circuit_breaker_failure_threshold=3 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set reverse_proxy_circuit_breaker_cooldown_seconds=10 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set http1_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set http2_enabled=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set http3_enabled=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tls_sni_hosts= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tls_sni_certificates= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) --set tls_enabled=false >/dev/null

https-db: build certs
	@mkdir -p $(BUILD_DIR)
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set host=$(HOST) >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set port=$(HTTPS_PORT) >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set document_root=public >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set max_request_bytes=65536 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set http_keep_alive_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set http_keep_alive_timeout_seconds=15 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set http_keep_alive_max_requests=100 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set listen_backlog=128 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set "server_name=Rimau Web Server" >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set worker_threads=0 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set epoll_max_events=256 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reuse_port_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tcp_keepalive_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tcp_keepalive_idle_seconds=60 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tcp_keepalive_interval_seconds=10 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tcp_keepalive_probe_count=5 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set graceful_shutdown_timeout_seconds=5 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set connection_pool_size=1024 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set request_timeout_seconds=30 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set header_timeout_seconds=10 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set body_timeout_seconds=30 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set idle_timeout_seconds=15 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set global_connection_limit=10000 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set per_ip_connection_limit=100 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set rate_limit_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set rate_limit_requests_per_minute=600 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set ip_allowlist= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set ip_blocklist= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set websocket_max_frame_bytes=65536 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set security_headers_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set "security_header_content_security_policy=default-src 'self'; frame-ancestors 'self'; base-uri 'self'" >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set security_header_strict_transport_security=max-age=31536000 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set security_header_x_content_type_options=nosniff >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set security_header_x_frame_options=SAMEORIGIN >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set security_header_referrer_policy=no-referrer >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set security_header_cross_origin_opener_policy=same-origin >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set server_header_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set virtual_hosts_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set virtual_hosts= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_connect_timeout_seconds=5 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_read_timeout_seconds=30 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_max_response_bytes=1048576 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_retry_count=1 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_tls_verify_upstream=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_circuit_breaker_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_circuit_breaker_failure_threshold=3 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set reverse_proxy_circuit_breaker_cooldown_seconds=10 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set http1_enabled=true >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set http2_enabled=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set http3_enabled=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_min_version=TLSv1.2 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_max_version=TLSv1.3 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_alpn_protocols=http/1.1 >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_sni_hosts= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_sni_certificates= >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_sni_required=false >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_certificate_file=$(TLS_CERT_FILE) >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_private_key_file=$(TLS_KEY_FILE) >/dev/null
	@./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) --set tls_enabled=true >/dev/null

serve: dev-db
	@echo "Rimau Web Server running at http://$(HOST):$(PORT)/"
	./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE)

serve-https: https-db
	@echo "Rimau Web Server running at https://$(HOST):$(HTTPS_PORT)/"
	./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE)

start: dev-db
	@if [ -f "$(PID_FILE)" ] && kill -0 "$$(cat $(PID_FILE))" 2>/dev/null; then \
		echo "Rimau Web Server already running at http://$(HOST):$(PORT)/ pid=$$(cat $(PID_FILE))"; \
		exit 0; \
	fi
	@setsid ./$(BUILD_DIR)/rimau-server --database $(DEV_DATABASE) > $(LOG_FILE) 2>&1 < /dev/null & echo $$! > $(PID_FILE)
	@sleep 1
	@if kill -0 "$$(cat $(PID_FILE))" 2>/dev/null; then \
		echo "Rimau Web Server running at http://$(HOST):$(PORT)/ pid=$$(cat $(PID_FILE))"; \
	else \
		echo "Rimau Web Server failed to start"; \
		cat $(LOG_FILE); \
		exit 1; \
	fi

stop:
	@if [ -f "$(PID_FILE)" ] && kill -0 "$$(cat $(PID_FILE))" 2>/dev/null; then \
		kill "$$(cat $(PID_FILE))"; \
		echo "Rimau Web Server stopped pid=$$(cat $(PID_FILE))"; \
	else \
		echo "Rimau Web Server is not running"; \
	fi
	@rm -f $(PID_FILE)

status:
	@if [ -f "$(PID_FILE)" ] && kill -0 "$$(cat $(PID_FILE))" 2>/dev/null; then \
		echo "Rimau Web Server running at http://$(HOST):$(PORT)/ pid=$$(cat $(PID_FILE))"; \
	else \
		echo "Rimau Web Server is not running"; \
	fi

start-https: https-db
	@if [ -f "$(HTTPS_PID_FILE)" ] && kill -0 "$$(cat $(HTTPS_PID_FILE))" 2>/dev/null; then \
		echo "Rimau Web Server already running at https://$(HOST):$(HTTPS_PORT)/ pid=$$(cat $(HTTPS_PID_FILE))"; \
		exit 0; \
	fi
	@setsid ./$(BUILD_DIR)/rimau-server --database $(HTTPS_DEV_DATABASE) > $(HTTPS_LOG_FILE) 2>&1 < /dev/null & echo $$! > $(HTTPS_PID_FILE)
	@sleep 1
	@if kill -0 "$$(cat $(HTTPS_PID_FILE))" 2>/dev/null; then \
		echo "Rimau Web Server running at https://$(HOST):$(HTTPS_PORT)/ pid=$$(cat $(HTTPS_PID_FILE))"; \
	else \
		echo "Rimau Web Server HTTPS failed to start"; \
		cat $(HTTPS_LOG_FILE); \
		exit 1; \
	fi

stop-https:
	@if [ -f "$(HTTPS_PID_FILE)" ] && kill -0 "$$(cat $(HTTPS_PID_FILE))" 2>/dev/null; then \
		kill "$$(cat $(HTTPS_PID_FILE))"; \
		echo "Rimau Web Server HTTPS stopped pid=$$(cat $(HTTPS_PID_FILE))"; \
	else \
		echo "Rimau Web Server HTTPS is not running"; \
	fi
	@rm -f $(HTTPS_PID_FILE)

status-https:
	@if [ -f "$(HTTPS_PID_FILE)" ] && kill -0 "$$(cat $(HTTPS_PID_FILE))" 2>/dev/null; then \
		echo "Rimau Web Server running at https://$(HOST):$(HTTPS_PORT)/ pid=$$(cat $(HTTPS_PID_FILE))"; \
	else \
		echo "Rimau Web Server HTTPS is not running"; \
	fi

clean:
	@if [ -d "$(BUILD_DIR)" ]; then cmake --build $(BUILD_DIR) --target clean; fi
