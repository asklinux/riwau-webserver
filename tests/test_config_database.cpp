#include "rimau/core/config.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace {

std::filesystem::path make_database_path(const std::string& prefix = "rimau-config-test-")
{
    static int sequence = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / (prefix + std::to_string(stamp) + "-" + std::to_string(++sequence) + ".sqlite3");
}

class TestDatabase {
public:
    explicit TestDatabase(const std::filesystem::path& database_path)
    {
        sqlite3* opened = nullptr;
        const int rc = sqlite3_open_v2(
            database_path.string().c_str(),
            &opened,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            nullptr);

        db_ = opened;
        if (rc != SQLITE_OK) {
            const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown sqlite error";
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            throw std::runtime_error("test cannot open sqlite database: " + message);
        }
    }

    TestDatabase(const TestDatabase&) = delete;
    TestDatabase& operator=(const TestDatabase&) = delete;

    ~TestDatabase()
    {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    void exec(const char* sql) const
    {
        char* raw_error = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &raw_error);
        if (rc != SQLITE_OK) {
            const std::string message = raw_error ? raw_error : sqlite3_errmsg(db_);
            sqlite3_free(raw_error);
            throw std::runtime_error("test sqlite exec failed: " + message);
        }
    }

    int scalar_int(const char* sql) const
    {
        sqlite3_stmt* statement = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("test sqlite prepare failed: " + std::string(sqlite3_errmsg(db_)));
        }

        rc = sqlite3_step(statement);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(statement);
            throw std::runtime_error("test sqlite scalar query returned no row");
        }

        const int value = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
        return value;
    }

private:
    sqlite3* db_ = nullptr;
};

} // namespace

int main()
{
    const auto database_path = make_database_path();

    {
        const auto config = rimau::core::load_config_from_database(database_path);
        assert(config.host == "0.0.0.0");
        assert(config.port == 8080);
        assert(config.document_root == "public");
        assert(config.directory_index == "index.html");
        assert(config.error_page.empty());
        assert(config.max_request_bytes == 65536);
        assert(config.http_keep_alive_enabled);
        assert(config.http_keep_alive_timeout_seconds == 15);
        assert(config.http_keep_alive_max_requests == 100);
        assert(config.listen_backlog == 1024);
        assert(config.server_name == "Rimau Web Server");
        assert(config.worker_threads == 0);
        assert(config.epoll_max_events == 256);
        assert(config.reuse_port_enabled);
        assert(config.tcp_keepalive_enabled);
        assert(config.tcp_keepalive_idle_seconds == 60);
        assert(config.tcp_keepalive_interval_seconds == 10);
        assert(config.tcp_keepalive_probe_count == 5);
        assert(config.graceful_shutdown_timeout_seconds == 5);
        assert(config.connection_pool_size == 1024);
        assert(config.http1_enabled);
        assert(!config.http2_enabled);
        assert(!config.http3_enabled);
        assert(!config.tls_enabled);
        assert(config.tls_certificate_file == "certs/rimau-dev.crt");
        assert(config.tls_private_key_file == "certs/rimau-dev.key");
        assert(config.tls_min_version == "TLSv1.2");
        assert(config.tls_max_version == "TLSv1.3");
        assert(config.tls_alpn_protocols == "http/1.1");
        assert(config.tls_sni_certificates.empty());
        assert(!config.tls_sni_required);
        assert(config.request_timeout_seconds == 30);
        assert(config.header_timeout_seconds == 10);
        assert(config.body_timeout_seconds == 30);
        assert(config.idle_timeout_seconds == 15);
        assert(config.global_connection_limit == 10000);
        assert(config.per_ip_connection_limit == 100);
        assert(config.rate_limit_enabled);
        assert(config.rate_limit_requests_per_minute == 600);
        assert(config.ip_allowlist.empty());
        assert(config.ip_blocklist.empty());
        assert(config.websocket_max_frame_bytes == 65536);
        assert(config.security_headers_enabled);
        assert(config.security_header_content_security_policy == "default-src 'self'; frame-ancestors 'self'; base-uri 'self'");
        assert(config.security_header_strict_transport_security == "max-age=31536000");
        assert(config.security_header_x_content_type_options == "nosniff");
        assert(config.security_header_x_frame_options == "SAMEORIGIN");
        assert(config.security_header_referrer_policy == "no-referrer");
        assert(config.security_header_cross_origin_opener_policy == "same-origin");
        assert(config.server_header_enabled);
        assert(config.virtual_hosts_enabled);
        assert(config.virtual_hosts.empty());
        assert(config.virtual_host_waf_overrides.empty());
        assert(config.reverse_proxy_connect_timeout_seconds == 5);
        assert(config.reverse_proxy_read_timeout_seconds == 30);
        assert(config.reverse_proxy_max_response_bytes == 1048576);
        assert(config.reverse_proxy_retry_count == 1);
        assert(config.reverse_proxy_load_balancing_policy == "round_robin");
        assert(!config.reverse_proxy_tls_verify_upstream);
        assert(config.reverse_proxy_circuit_breaker_enabled);
        assert(config.reverse_proxy_circuit_breaker_failure_threshold == 3);
        assert(config.reverse_proxy_circuit_breaker_cooldown_seconds == 10);
        assert(!config.modsecurity_enabled);
        assert(config.modsecurity_owasp_crs_enabled);
        assert(config.modsecurity_blocking_enabled);
        assert(config.modsecurity_anomaly_threshold == 5);
        assert(config.modsecurity_max_inspection_bytes == 131072);
        assert(config.modsecurity_audit_log_enabled);
        assert(rimau::core::config_schema_version(database_path) == 1);
        const TestDatabase database(database_path);
        assert(database.scalar_int("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'rimau_schema_migrations';") == 1);
        assert(database.scalar_int("SELECT COUNT(*) FROM rimau_schema_migrations WHERE version = 1 AND name = 'initial rimau_config schema';") == 1);
    }

    rimau::core::set_config_value(database_path, "host", "127.0.0.1");
    rimau::core::set_config_value(database_path, "port", "18080");
    rimau::core::set_config_value(database_path, "document_root", "public");
    rimau::core::set_config_value(database_path, "directory_index", "home.html");
    rimau::core::set_config_value(database_path, "error_page", "errors/default.html");
    rimau::core::set_config_value(database_path, "max_request_bytes", "131072");
    rimau::core::set_config_value(database_path, "http_keep_alive_enabled", "true");
    rimau::core::set_config_value(database_path, "http_keep_alive_timeout_seconds", "7");
    rimau::core::set_config_value(database_path, "http_keep_alive_max_requests", "4");
    rimau::core::set_config_value(database_path, "listen_backlog", "128");
    rimau::core::set_config_value(database_path, "server_name", "Rimau Test Server");
    rimau::core::set_config_value(database_path, "worker_threads", "2");
    rimau::core::set_config_value(database_path, "epoll_max_events", "64");
    rimau::core::set_config_value(database_path, "reuse_port_enabled", "true");
    rimau::core::set_config_value(database_path, "tcp_keepalive_enabled", "true");
    rimau::core::set_config_value(database_path, "tcp_keepalive_idle_seconds", "30");
    rimau::core::set_config_value(database_path, "tcp_keepalive_interval_seconds", "5");
    rimau::core::set_config_value(database_path, "tcp_keepalive_probe_count", "3");
    rimau::core::set_config_value(database_path, "graceful_shutdown_timeout_seconds", "2");
    rimau::core::set_config_value(database_path, "connection_pool_size", "16");
    rimau::core::set_config_value(database_path, "http1_enabled", "true");
    rimau::core::set_config_value(database_path, "http2_enabled", "true");
    rimau::core::set_config_value(database_path, "http3_enabled", "true");
    rimau::core::set_config_value(database_path, "tls_enabled", "false");
    rimau::core::set_config_value(database_path, "tls_certificate_file", "certs/test.crt");
    rimau::core::set_config_value(database_path, "tls_private_key_file", "certs/test.key");
    rimau::core::set_config_value(database_path, "tls_min_version", "TLSv1.2");
    rimau::core::set_config_value(database_path, "tls_max_version", "TLSv1.3");
    rimau::core::set_config_value(database_path, "tls_cipher_list", "ECDHE-RSA-AES128-GCM-SHA256");
    rimau::core::set_config_value(database_path, "tls_ciphersuites", "TLS_AES_128_GCM_SHA256");
    rimau::core::set_config_value(database_path, "tls_alpn_protocols", "http/1.1");
    rimau::core::set_config_value(database_path, "tls_sni_hosts", "example.test,*.rimau.test");
    rimau::core::set_config_value(database_path, "tls_sni_certificates", "api.example.test=certs/api.crt|certs/api.key;*.tenant.test=certs/tenant.crt|certs/tenant.key");
    rimau::core::set_config_value(database_path, "tls_sni_required", "true");
    rimau::core::set_config_value(database_path, "request_timeout_seconds", "11");
    rimau::core::set_config_value(database_path, "header_timeout_seconds", "3");
    rimau::core::set_config_value(database_path, "body_timeout_seconds", "9");
    rimau::core::set_config_value(database_path, "idle_timeout_seconds", "5");
    rimau::core::set_config_value(database_path, "global_connection_limit", "200");
    rimau::core::set_config_value(database_path, "per_ip_connection_limit", "20");
    rimau::core::set_config_value(database_path, "rate_limit_enabled", "true");
    rimau::core::set_config_value(database_path, "rate_limit_requests_per_minute", "60");
    rimau::core::set_config_value(database_path, "ip_allowlist", "127.0.0.1,10.0.0.0/8,::1,2001:db8::/32");
    rimau::core::set_config_value(database_path, "ip_blocklist", "192.0.2.1,2001:db8:ffff::/48");
    rimau::core::set_config_value(database_path, "websocket_max_frame_bytes", "4096");
    rimau::core::set_config_value(database_path, "security_headers_enabled", "false");
    rimau::core::set_config_value(database_path, "security_header_content_security_policy", "default-src 'none'");
    rimau::core::set_config_value(database_path, "security_header_strict_transport_security", "");
    rimau::core::set_config_value(database_path, "security_header_x_content_type_options", "");
    rimau::core::set_config_value(database_path, "security_header_x_frame_options", "DENY");
    rimau::core::set_config_value(database_path, "security_header_referrer_policy", "same-origin");
    rimau::core::set_config_value(database_path, "security_header_cross_origin_opener_policy", "same-origin-allow-popups");
    rimau::core::set_config_value(database_path, "server_header_enabled", "false");
    rimau::core::set_config_value(database_path, "virtual_hosts_enabled", "true");
    rimau::core::set_config_value(database_path, "virtual_hosts", "site.test=static:public/site;api.test=proxy:http://127.0.0.1:19090,https://backend.test:9443;app.test=script:php:public/app");
    rimau::core::set_config_value(database_path, "virtual_host_waf_overrides", "site.test=enabled:false,threshold:9,rule_exceptions:930100|942100;api.test=blocking:false");
    rimau::core::set_config_value(database_path, "reverse_proxy_connect_timeout_seconds", "2");
    rimau::core::set_config_value(database_path, "reverse_proxy_read_timeout_seconds", "4");
    rimau::core::set_config_value(database_path, "reverse_proxy_max_response_bytes", "32768");
    rimau::core::set_config_value(database_path, "reverse_proxy_retry_count", "3");
    rimau::core::set_config_value(database_path, "reverse_proxy_load_balancing_policy", "stable_hash");
    rimau::core::set_config_value(database_path, "reverse_proxy_tls_verify_upstream", "true");
    rimau::core::set_config_value(database_path, "reverse_proxy_circuit_breaker_enabled", "true");
    rimau::core::set_config_value(database_path, "reverse_proxy_circuit_breaker_failure_threshold", "2");
    rimau::core::set_config_value(database_path, "reverse_proxy_circuit_breaker_cooldown_seconds", "7");
    rimau::core::set_config_value(database_path, "modsecurity_enabled", "true");
    rimau::core::set_config_value(database_path, "modsecurity_owasp_crs_enabled", "true");
    rimau::core::set_config_value(database_path, "modsecurity_blocking_enabled", "false");
    rimau::core::set_config_value(database_path, "modsecurity_anomaly_threshold", "7");
    rimau::core::set_config_value(database_path, "modsecurity_max_inspection_bytes", "65536");
    rimau::core::set_config_value(database_path, "modsecurity_audit_log_enabled", "false");

    {
        const auto config = rimau::core::load_config_from_database(database_path);
        assert(config.host == "127.0.0.1");
        assert(config.port == 18080);
        assert(config.document_root == "public");
        assert(config.directory_index == "home.html");
        assert(config.error_page == "errors/default.html");
        assert(config.max_request_bytes == 131072);
        assert(config.http_keep_alive_enabled);
        assert(config.http_keep_alive_timeout_seconds == 7);
        assert(config.http_keep_alive_max_requests == 4);
        assert(config.listen_backlog == 128);
        assert(config.server_name == "Rimau Test Server");
        assert(config.worker_threads == 2);
        assert(config.epoll_max_events == 64);
        assert(config.reuse_port_enabled);
        assert(config.tcp_keepalive_enabled);
        assert(config.tcp_keepalive_idle_seconds == 30);
        assert(config.tcp_keepalive_interval_seconds == 5);
        assert(config.tcp_keepalive_probe_count == 3);
        assert(config.graceful_shutdown_timeout_seconds == 2);
        assert(config.connection_pool_size == 16);
        assert(config.http1_enabled);
        assert(config.http2_enabled);
        assert(config.http3_enabled);
        assert(!config.tls_enabled);
        assert(config.tls_certificate_file == "certs/test.crt");
        assert(config.tls_private_key_file == "certs/test.key");
        assert(config.tls_min_version == "TLSv1.2");
        assert(config.tls_max_version == "TLSv1.3");
        assert(config.tls_cipher_list == "ECDHE-RSA-AES128-GCM-SHA256");
        assert(config.tls_ciphersuites == "TLS_AES_128_GCM_SHA256");
        assert(config.tls_alpn_protocols == "http/1.1");
        assert(config.tls_sni_hosts == "example.test,*.rimau.test");
        assert(config.tls_sni_certificates == "api.example.test=certs/api.crt|certs/api.key;*.tenant.test=certs/tenant.crt|certs/tenant.key");
        assert(config.tls_sni_required);
        assert(config.request_timeout_seconds == 11);
        assert(config.header_timeout_seconds == 3);
        assert(config.body_timeout_seconds == 9);
        assert(config.idle_timeout_seconds == 5);
        assert(config.global_connection_limit == 200);
        assert(config.per_ip_connection_limit == 20);
        assert(config.rate_limit_enabled);
        assert(config.rate_limit_requests_per_minute == 60);
        assert(config.ip_allowlist == "127.0.0.1,10.0.0.0/8,::1,2001:db8::/32");
        assert(config.ip_blocklist == "192.0.2.1,2001:db8:ffff::/48");
        assert(config.websocket_max_frame_bytes == 4096);
        assert(!config.security_headers_enabled);
        assert(config.security_header_content_security_policy == "default-src 'none'");
        assert(config.security_header_strict_transport_security.empty());
        assert(config.security_header_x_content_type_options.empty());
        assert(config.security_header_x_frame_options == "DENY");
        assert(config.security_header_referrer_policy == "same-origin");
        assert(config.security_header_cross_origin_opener_policy == "same-origin-allow-popups");
        assert(!config.server_header_enabled);
        assert(config.virtual_hosts_enabled);
        assert(config.virtual_hosts == "site.test=static:public/site;api.test=proxy:http://127.0.0.1:19090,https://backend.test:9443;app.test=script:php:public/app");
        assert(config.virtual_host_waf_overrides == "site.test=enabled:false,threshold:9,rule_exceptions:930100|942100;api.test=blocking:false");
        assert(config.reverse_proxy_connect_timeout_seconds == 2);
        assert(config.reverse_proxy_read_timeout_seconds == 4);
        assert(config.reverse_proxy_max_response_bytes == 32768);
        assert(config.reverse_proxy_retry_count == 3);
        assert(config.reverse_proxy_load_balancing_policy == "stable_hash");
        assert(config.reverse_proxy_tls_verify_upstream);
        assert(config.reverse_proxy_circuit_breaker_enabled);
        assert(config.reverse_proxy_circuit_breaker_failure_threshold == 2);
        assert(config.reverse_proxy_circuit_breaker_cooldown_seconds == 7);
        assert(config.modsecurity_enabled);
        assert(config.modsecurity_owasp_crs_enabled);
        assert(!config.modsecurity_blocking_enabled);
        assert(config.modsecurity_anomaly_threshold == 7);
        assert(config.modsecurity_max_inspection_bytes == 65536);
        assert(!config.modsecurity_audit_log_enabled);
        assert(rimau::core::config_schema_version(database_path) == 1);
    }

    bool unknown_key_failed = false;
    try {
        rimau::core::set_config_value(database_path, "unknown", "value");
    } catch (const std::runtime_error&) {
        unknown_key_failed = true;
    }
    assert(unknown_key_failed);

    bool invalid_port_failed = false;
    try {
        rimau::core::set_config_value(database_path, "port", "70000");
    } catch (const std::runtime_error&) {
        invalid_port_failed = true;
    }
    assert(invalid_port_failed);

    bool invalid_tls_bool_failed = false;
    try {
        rimau::core::set_config_value(database_path, "tls_enabled", "maybe");
    } catch (const std::runtime_error&) {
        invalid_tls_bool_failed = true;
    }
    assert(invalid_tls_bool_failed);

    bool invalid_http2_bool_failed = false;
    try {
        rimau::core::set_config_value(database_path, "http2_enabled", "maybe");
    } catch (const std::runtime_error&) {
        invalid_http2_bool_failed = true;
    }
    assert(invalid_http2_bool_failed);

    bool invalid_worker_threads_failed = false;
    try {
        rimau::core::set_config_value(database_path, "worker_threads", "two");
    } catch (const std::runtime_error&) {
        invalid_worker_threads_failed = true;
    }
    assert(invalid_worker_threads_failed);

    bool invalid_epoll_events_failed = false;
    try {
        rimau::core::set_config_value(database_path, "epoll_max_events", "0");
    } catch (const std::runtime_error&) {
        invalid_epoll_events_failed = true;
    }
    assert(invalid_epoll_events_failed);

    bool invalid_keepalive_bool_failed = false;
    try {
        rimau::core::set_config_value(database_path, "tcp_keepalive_enabled", "sometimes");
    } catch (const std::runtime_error&) {
        invalid_keepalive_bool_failed = true;
    }
    assert(invalid_keepalive_bool_failed);

    bool invalid_http_keepalive_bool_failed = false;
    try {
        rimau::core::set_config_value(database_path, "http_keep_alive_enabled", "sometimes");
    } catch (const std::runtime_error&) {
        invalid_http_keepalive_bool_failed = true;
    }
    assert(invalid_http_keepalive_bool_failed);

    bool invalid_http_keepalive_timeout_failed = false;
    try {
        rimau::core::set_config_value(database_path, "http_keep_alive_timeout_seconds", "0");
    } catch (const std::runtime_error&) {
        invalid_http_keepalive_timeout_failed = true;
    }
    assert(invalid_http_keepalive_timeout_failed);

    bool invalid_http_keepalive_max_failed = false;
    try {
        rimau::core::set_config_value(database_path, "http_keep_alive_max_requests", "none");
    } catch (const std::runtime_error&) {
        invalid_http_keepalive_max_failed = true;
    }
    assert(invalid_http_keepalive_max_failed);

    bool invalid_directory_index_failed = false;
    try {
        rimau::core::set_config_value(database_path, "directory_index", "../index.html");
    } catch (const std::runtime_error&) {
        invalid_directory_index_failed = true;
    }
    assert(invalid_directory_index_failed);

    rimau::core::set_config_value(database_path, "tls_alpn_protocols", "h2,http/1.1");
    {
        const auto config = rimau::core::load_config_from_database(database_path);
        assert(config.tls_alpn_protocols == "h2,http/1.1");
        assert(config.http2_enabled);
    }

    bool invalid_h2_without_http2_failed = false;
    try {
        rimau::core::set_config_value(database_path, "tls_certificate_file", "certs/rimau-dev.crt");
        rimau::core::set_config_value(database_path, "tls_private_key_file", "certs/rimau-dev.key");
        rimau::core::set_config_value(database_path, "http2_enabled", "false");
        rimau::core::set_config_value(database_path, "tls_enabled", "true");
        rimau::core::set_config_value(database_path, "tls_alpn_protocols", "h2");
        (void)rimau::core::load_config_from_database(database_path);
    } catch (const std::runtime_error&) {
        invalid_h2_without_http2_failed = true;
    }
    assert(invalid_h2_without_http2_failed);

    rimau::core::set_config_value(database_path, "tls_enabled", "false");
    rimau::core::set_config_value(database_path, "http2_enabled", "true");

    bool invalid_h3_alpn_failed = false;
    try {
        rimau::core::set_config_value(database_path, "tls_alpn_protocols", "h3");
        (void)rimau::core::load_config_from_database(database_path);
    } catch (const std::runtime_error&) {
        invalid_h3_alpn_failed = true;
    }
    assert(invalid_h3_alpn_failed);

    rimau::core::set_config_value(database_path, "tls_alpn_protocols", "http/1.1");

    bool invalid_server_name_failed = false;
    try {
        rimau::core::set_config_value(database_path, "server_name", "bad\r\nname");
    } catch (const std::runtime_error&) {
        invalid_server_name_failed = true;
    }
    assert(invalid_server_name_failed);

    bool invalid_ip_list_failed = false;
    try {
        rimau::core::set_config_value(database_path, "ip_allowlist", "2001:db8::/129");
    } catch (const std::runtime_error&) {
        invalid_ip_list_failed = true;
    }
    assert(invalid_ip_list_failed);

    bool invalid_sni_cert_map_failed = false;
    try {
        rimau::core::set_config_value(database_path, "tls_sni_certificates", "example.test=certs/example.crt");
    } catch (const std::runtime_error&) {
        invalid_sni_cert_map_failed = true;
    }
    assert(invalid_sni_cert_map_failed);

    bool invalid_security_header_failed = false;
    try {
        rimau::core::set_config_value(database_path, "security_header_x_frame_options", "DENY\r\nx-bad: true");
    } catch (const std::runtime_error&) {
        invalid_security_header_failed = true;
    }
    assert(invalid_security_header_failed);

    bool invalid_virtual_hosts_failed = false;
    try {
        rimau::core::set_config_value(database_path, "virtual_hosts", "broken");
    } catch (const std::runtime_error&) {
        invalid_virtual_hosts_failed = true;
    }
    assert(invalid_virtual_hosts_failed);

    bool invalid_proxy_timeout_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_connect_timeout_seconds", "0");
    } catch (const std::runtime_error&) {
        invalid_proxy_timeout_failed = true;
    }
    assert(invalid_proxy_timeout_failed);

    bool invalid_proxy_retry_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_retry_count", "many");
    } catch (const std::runtime_error&) {
        invalid_proxy_retry_failed = true;
    }
    assert(invalid_proxy_retry_failed);

    bool invalid_proxy_policy_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_load_balancing_policy", "random");
    } catch (const std::runtime_error&) {
        invalid_proxy_policy_failed = true;
    }
    assert(invalid_proxy_policy_failed);

    bool invalid_proxy_tls_verify_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_tls_verify_upstream", "maybe");
    } catch (const std::runtime_error&) {
        invalid_proxy_tls_verify_failed = true;
    }
    assert(invalid_proxy_tls_verify_failed);

    bool invalid_proxy_circuit_bool_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_circuit_breaker_enabled", "maybe");
    } catch (const std::runtime_error&) {
        invalid_proxy_circuit_bool_failed = true;
    }
    assert(invalid_proxy_circuit_bool_failed);

    bool invalid_proxy_circuit_threshold_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_circuit_breaker_failure_threshold", "0");
    } catch (const std::runtime_error&) {
        invalid_proxy_circuit_threshold_failed = true;
    }
    assert(invalid_proxy_circuit_threshold_failed);

    bool invalid_proxy_circuit_cooldown_failed = false;
    try {
        rimau::core::set_config_value(database_path, "reverse_proxy_circuit_breaker_cooldown_seconds", "0");
    } catch (const std::runtime_error&) {
        invalid_proxy_circuit_cooldown_failed = true;
    }
    assert(invalid_proxy_circuit_cooldown_failed);

    bool invalid_modsecurity_bool_failed = false;
    try {
        rimau::core::set_config_value(database_path, "modsecurity_enabled", "maybe");
    } catch (const std::runtime_error&) {
        invalid_modsecurity_bool_failed = true;
    }
    assert(invalid_modsecurity_bool_failed);

    bool invalid_modsecurity_threshold_failed = false;
    try {
        rimau::core::set_config_value(database_path, "modsecurity_anomaly_threshold", "0");
    } catch (const std::runtime_error&) {
        invalid_modsecurity_threshold_failed = true;
    }
    assert(invalid_modsecurity_threshold_failed);

    bool invalid_virtual_host_waf_failed = false;
    try {
        rimau::core::set_config_value(database_path, "virtual_host_waf_overrides", "site.test=rule_exceptions:abc");
    } catch (const std::runtime_error&) {
        invalid_virtual_host_waf_failed = true;
    }
    assert(invalid_virtual_host_waf_failed);

    const auto legacy_database_path = make_database_path("rimau-config-legacy-test-");
    {
        TestDatabase database(legacy_database_path);
        database.exec(
            "CREATE TABLE rimau_config ("
            "key TEXT PRIMARY KEY NOT NULL,"
            "value TEXT NOT NULL,"
            "description TEXT NOT NULL DEFAULT '',"
            "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
            "INSERT INTO rimau_config(key, value, description) "
            "VALUES ('host', '127.0.0.2', 'legacy host');");
    }
    {
        const auto config = rimau::core::load_config_from_database(legacy_database_path);
        assert(config.host == "127.0.0.2");
        assert(config.port == 8080);
        assert(rimau::core::config_schema_version(legacy_database_path) == 1);

        const TestDatabase database(legacy_database_path);
        assert(database.scalar_int("SELECT COUNT(*) FROM rimau_schema_migrations WHERE version = 1;") == 1);
    }

    const auto future_database_path = make_database_path("rimau-config-future-test-");
    (void)rimau::core::load_config_from_database(future_database_path);
    {
        TestDatabase database(future_database_path);
        database.exec("INSERT OR REPLACE INTO rimau_schema_migrations(version, name) VALUES (999, 'future test schema');");
    }

    bool future_schema_failed = false;
    try {
        (void)rimau::core::load_config_from_database(future_database_path);
    } catch (const std::runtime_error& error) {
        future_schema_failed = std::string(error.what()).find("newer than supported version") != std::string::npos;
    }
    assert(future_schema_failed);

    std::filesystem::remove(database_path);
    std::filesystem::remove(legacy_database_path);
    std::filesystem::remove(future_database_path);
    return 0;
}
