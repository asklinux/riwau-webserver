#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rimau::core {

inline constexpr const char* default_database_path = "data/rimau.sqlite3";

struct ServerConfig {
    std::filesystem::path database_path = default_database_path;
    std::string host = "0.0.0.0";
    std::uint16_t port = 8080;
    std::filesystem::path document_root = "public";
    std::size_t max_request_bytes = 64 * 1024;
    bool http_keep_alive_enabled = true;
    int http_keep_alive_timeout_seconds = 15;
    int http_keep_alive_max_requests = 100;
    int listen_backlog = 1024;
    std::string server_name = "Rimau Web Server";
    std::size_t worker_threads = 0;
    int epoll_max_events = 256;
    bool reuse_port_enabled = true;
    bool tcp_keepalive_enabled = true;
    int tcp_keepalive_idle_seconds = 60;
    int tcp_keepalive_interval_seconds = 10;
    int tcp_keepalive_probe_count = 5;
    int graceful_shutdown_timeout_seconds = 5;
    std::size_t connection_pool_size = 1024;
    bool http1_enabled = true;
    bool http2_enabled = false;
    bool http3_enabled = false;
    bool tls_enabled = false;
    std::filesystem::path tls_certificate_file = "certs/rimau-dev.crt";
    std::filesystem::path tls_private_key_file = "certs/rimau-dev.key";
    std::string tls_min_version = "TLSv1.2";
    std::string tls_max_version = "TLSv1.3";
    std::string tls_cipher_list = "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256";
    std::string tls_ciphersuites = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256";
    std::string tls_alpn_protocols = "http/1.1";
    std::string tls_sni_hosts;
    std::string tls_sni_certificates;
    bool tls_sni_required = false;
    int request_timeout_seconds = 30;
    int header_timeout_seconds = 10;
    int body_timeout_seconds = 30;
    int idle_timeout_seconds = 15;
    std::size_t global_connection_limit = 10000;
    std::size_t per_ip_connection_limit = 100;
    bool rate_limit_enabled = true;
    int rate_limit_requests_per_minute = 600;
    std::string ip_allowlist;
    std::string ip_blocklist;
    std::size_t websocket_max_frame_bytes = 64 * 1024;
    bool security_headers_enabled = true;
    std::string security_header_content_security_policy = "default-src 'self'; frame-ancestors 'self'; base-uri 'self'";
    std::string security_header_strict_transport_security = "max-age=31536000";
    std::string security_header_x_content_type_options = "nosniff";
    std::string security_header_x_frame_options = "SAMEORIGIN";
    std::string security_header_referrer_policy = "no-referrer";
    std::string security_header_cross_origin_opener_policy = "same-origin";
    bool server_header_enabled = true;
    bool virtual_hosts_enabled = true;
    std::string virtual_hosts;
    int reverse_proxy_connect_timeout_seconds = 5;
    int reverse_proxy_read_timeout_seconds = 30;
    std::size_t reverse_proxy_max_response_bytes = 1024 * 1024;
    std::size_t reverse_proxy_retry_count = 1;
    bool reverse_proxy_tls_verify_upstream = false;
    bool reverse_proxy_circuit_breaker_enabled = true;
    std::size_t reverse_proxy_circuit_breaker_failure_threshold = 3;
    int reverse_proxy_circuit_breaker_cooldown_seconds = 10;
};

void initialize_config_database(const std::filesystem::path& database_path);
ServerConfig load_config_from_database(const std::filesystem::path& database_path);
void set_config_value(const std::filesystem::path& database_path, const std::string& key, const std::string& value);
std::vector<std::string> supported_config_keys();
std::string describe_config(const ServerConfig& config);

} // namespace rimau::core
