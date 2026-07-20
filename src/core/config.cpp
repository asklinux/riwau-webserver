#include "rimau/core/config.hpp"

#include "rimau/http/virtual_host.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sqlite3.h>

namespace rimau::core {
namespace {

inline constexpr int current_config_schema_version = 1;

std::uint16_t parse_port(const std::string& value)
{
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error("port must be a number");
        }
        if (parsed < 1 || parsed > 65535) {
            throw std::runtime_error("port must be between 1 and 65535");
        }
        return static_cast<std::uint16_t>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("port must be a number");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("port is out of range");
    }
}

std::size_t parse_size(const std::string& value, const char* key)
{
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error(std::string(key) + " must be a number");
        }
        if (parsed == 0) {
            throw std::runtime_error(std::string(key) + " must be greater than zero");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(std::string(key) + " must be a number");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(std::string(key) + " is out of range");
    }
}

int parse_positive_int(const std::string& value, const char* key)
{
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error(std::string(key) + " must be a number");
        }
        if (parsed < 1) {
            throw std::runtime_error(std::string(key) + " must be greater than zero");
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(std::string(key) + " must be a number");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(std::string(key) + " is out of range");
    }
}

std::size_t parse_non_negative_size(const std::string& value, const char* key)
{
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            throw std::runtime_error(std::string(key) + " must be a number");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(std::string(key) + " must be a number");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(std::string(key) + " is out of range");
    }
}

bool parse_bool(const std::string& value, const char* key)
{
    std::string normalized;
    normalized.reserve(value.size());
    std::transform(value.begin(), value.end(), std::back_inserter(normalized), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }

    throw std::runtime_error(std::string(key) + " must be a boolean");
}

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<std::string> split_delimited(std::string_view value, char delimiter)
{
    std::vector<std::string> items;
    std::istringstream input { std::string(value) };
    std::string item;
    while (std::getline(input, item, delimiter)) {
        item = trim(std::move(item));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

bool is_optional_string_key(const std::string& key)
{
    return key == "tls_sni_hosts" || key == "tls_sni_certificates" || key == "ip_allowlist" || key == "ip_blocklist"
        || key == "security_header_content_security_policy" || key == "security_header_strict_transport_security"
        || key == "security_header_x_content_type_options" || key == "security_header_x_frame_options"
        || key == "security_header_referrer_policy" || key == "security_header_cross_origin_opener_policy"
        || key == "virtual_hosts" || key == "error_page";
}

bool contains_control_character(const std::string& value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7f;
    });
}

bool valid_directory_index(std::string_view value)
{
    return !value.empty()
        && value != "."
        && value != ".."
        && value.find('/') == std::string_view::npos
        && value.find('\\') == std::string_view::npos
        && std::none_of(value.begin(), value.end(), [](unsigned char ch) {
               return ch < 0x20 || ch == 0x7f;
           });
}

bool valid_tls_version(const std::string& value)
{
    return value == "TLSv1.2" || value == "TLSv1.3";
}

bool parse_prefix_length(std::string_view value, int max_prefix, int& output)
{
    if (value.empty()) {
        return false;
    }

    int parsed = 0;
    for (const char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        parsed = parsed * 10 + (ch - '0');
        if (parsed > max_prefix) {
            return false;
        }
    }

    output = parsed;
    return true;
}

bool parse_ip_family(std::string_view value, int& family)
{
    const std::string text(value);
    in_addr ipv4 {};
    if (inet_pton(AF_INET, text.c_str(), &ipv4) == 1) {
        family = AF_INET;
        return true;
    }

    in6_addr ipv6 {};
    if (inet_pton(AF_INET6, text.c_str(), &ipv6) == 1) {
        family = AF_INET6;
        return true;
    }

    return false;
}

bool valid_ip_list(std::string_view list)
{
    for (const auto& token : split_delimited(list, ',')) {
        const auto slash = token.find('/');
        const std::string address_text = slash == std::string::npos ? token : trim(token.substr(0, slash));
        if (address_text.empty()) {
            return false;
        }

        int family = 0;
        if (!parse_ip_family(address_text, family)) {
            return false;
        }

        if (slash == std::string::npos) {
            continue;
        }

        const std::string prefix_text = trim(token.substr(slash + 1));
        int prefix = 0;
        if (!parse_prefix_length(prefix_text, family == AF_INET ? 32 : 128, prefix)) {
            return false;
        }
    }

    return true;
}

bool valid_sni_certificate_map(std::string_view value)
{
    for (const auto& entry : split_delimited(value, ';')) {
        const auto equals = entry.find('=');
        const auto separator = entry.find('|', equals == std::string::npos ? 0 : equals + 1);
        if (equals == std::string::npos || separator == std::string::npos) {
            return false;
        }

        const std::string pattern = trim(entry.substr(0, equals));
        const std::string certificate = trim(entry.substr(equals + 1, separator - equals - 1));
        const std::string private_key = trim(entry.substr(separator + 1));
        if (pattern.empty() || certificate.empty() || private_key.empty()) {
            return false;
        }
    }

    return true;
}

bool alpn_contains_protocol(std::string_view protocols, std::string_view expected)
{
    for (const auto& protocol : split_delimited(protocols, ',')) {
        if (protocol == expected) {
            return true;
        }
    }
    return false;
}

void validate_alpn_protocols(const ServerConfig& config)
{
    bool has_supported_protocol = false;
    for (const auto& protocol : split_delimited(config.tls_alpn_protocols, ',')) {
        if (protocol == "http/1.1") {
            if (config.tls_enabled && !config.http1_enabled) {
                throw std::runtime_error("tls_alpn_protocols cannot include http/1.1 when http1_enabled is false");
            }
            has_supported_protocol = true;
            continue;
        }

        if (protocol == "h2") {
            if (config.tls_enabled && !config.http2_enabled) {
                throw std::runtime_error("tls_alpn_protocols cannot include h2 when http2_enabled is false");
            }
            has_supported_protocol = true;
            continue;
        }

        if (protocol == "h3") {
            throw std::runtime_error("tls_alpn_protocols must not include h3 until HTTP/3 serving is implemented");
        }

        throw std::runtime_error("unsupported ALPN protocol: " + protocol);
    }

    if (!has_supported_protocol) {
        throw std::runtime_error("tls_alpn_protocols must include at least one supported protocol");
    }

    if (config.tls_enabled && !config.http1_enabled && config.http2_enabled && !alpn_contains_protocol(config.tls_alpn_protocols, "h2")) {
        throw std::runtime_error("tls_alpn_protocols must include h2 when TLS is enabled, http1_enabled is false, and http2_enabled is true");
    }
}

struct ConfigDefault {
    const char* key;
    const char* value;
    const char* description;
};

const std::vector<ConfigDefault>& config_defaults()
{
    static const std::vector<ConfigDefault> defaults {
        { "host", "0.0.0.0", "IPv4 address to bind" },
        { "port", "8080", "TCP port to listen on" },
        { "document_root", "public", "Static file document root" },
        { "directory_index", "index.html", "Static directory index file name" },
        { "error_page", "", "Optional custom error page path, relative to document_root unless absolute" },
        { "max_request_bytes", "65536", "Maximum request header bytes" },
        { "http_keep_alive_enabled", "true", "Enable HTTP/1.1 persistent connections" },
        { "http_keep_alive_timeout_seconds", "15", "HTTP/1.1 keep-alive idle timeout" },
        { "http_keep_alive_max_requests", "100", "Maximum requests served on one HTTP/1.1 connection" },
        { "listen_backlog", "1024", "TCP listen backlog" },
        { "server_name", "Rimau Web Server", "Server identity used in logs and responses" },
        { "worker_threads", "0", "Worker threads; 0 means auto-detect CPU cores" },
        { "epoll_max_events", "256", "Maximum epoll events processed per worker iteration" },
        { "reuse_port_enabled", "true", "Enable SO_REUSEPORT for per-worker listeners" },
        { "tcp_keepalive_enabled", "true", "Enable TCP keepalive on accepted client sockets" },
        { "tcp_keepalive_idle_seconds", "60", "TCP keepalive idle time before probes" },
        { "tcp_keepalive_interval_seconds", "10", "TCP keepalive interval between probes" },
        { "tcp_keepalive_probe_count", "5", "TCP keepalive probe count before failure" },
        { "graceful_shutdown_timeout_seconds", "5", "Graceful shutdown drain timeout" },
        { "connection_pool_size", "1024", "Idle connection objects retained per worker for reuse" },
        { "http1_enabled", "true", "Enable HTTP/1.1 listener handling" },
        { "http2_enabled", "false", "Enable HTTP/2 when the HTTP/2 implementation is available" },
        { "http3_enabled", "false", "Enable HTTP/3 when the HTTP/3 implementation is available" },
        { "tls_enabled", "false", "Enable TLS for accepted HTTP/1.1 connections" },
        { "tls_certificate_file", "certs/rimau-dev.crt", "PEM certificate file for TLS" },
        { "tls_private_key_file", "certs/rimau-dev.key", "PEM private key file for TLS" },
        { "tls_min_version", "TLSv1.2", "Minimum TLS protocol version" },
        { "tls_max_version", "TLSv1.3", "Maximum TLS protocol version" },
        { "tls_cipher_list", "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256", "TLS 1.2 cipher list" },
        { "tls_ciphersuites", "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256", "TLS 1.3 cipher suites" },
        { "tls_alpn_protocols", "http/1.1", "Comma-separated ALPN protocols to advertise; only implemented protocols should be listed" },
        { "tls_sni_hosts", "", "Comma-separated SNI hostnames accepted by the default certificate; empty means all hostnames" },
        { "tls_sni_certificates", "", "Semicolon-separated SNI certificate map: hostname=certificate.pem|private-key.pem" },
        { "tls_sni_required", "false", "Reject TLS clients that do not send SNI" },
        { "request_timeout_seconds", "30", "Maximum time to complete one HTTP request" },
        { "header_timeout_seconds", "10", "Maximum time to complete HTTP request headers" },
        { "body_timeout_seconds", "30", "Maximum time to complete HTTP request body after headers" },
        { "idle_timeout_seconds", "15", "Idle timeout for keep-alive and WebSocket connections" },
        { "global_connection_limit", "10000", "Maximum active accepted connections across all workers" },
        { "per_ip_connection_limit", "100", "Maximum active accepted connections per client IP" },
        { "rate_limit_enabled", "true", "Enable fixed-window per-IP request rate limiting" },
        { "rate_limit_requests_per_minute", "600", "Maximum requests per minute per client IP" },
        { "ip_allowlist", "", "Comma-separated IPv4/IPv6 exact or CIDR allowlist; empty allows all non-blocked IPs" },
        { "ip_blocklist", "", "Comma-separated IPv4/IPv6 exact or CIDR blocklist" },
        { "websocket_max_frame_bytes", "65536", "Maximum accepted WebSocket frame payload bytes" },
        { "security_headers_enabled", "true", "Add default HTTP security headers to responses" },
        { "security_header_content_security_policy", "default-src 'self'; frame-ancestors 'self'; base-uri 'self'", "Content-Security-Policy value; empty disables this header" },
        { "security_header_strict_transport_security", "max-age=31536000", "Strict-Transport-Security value for TLS responses; empty disables this header" },
        { "security_header_x_content_type_options", "nosniff", "X-Content-Type-Options value; empty disables this header" },
        { "security_header_x_frame_options", "SAMEORIGIN", "X-Frame-Options value; empty disables this header" },
        { "security_header_referrer_policy", "no-referrer", "Referrer-Policy value; empty disables this header" },
        { "security_header_cross_origin_opener_policy", "same-origin", "Cross-Origin-Opener-Policy value; empty disables this header" },
        { "server_header_enabled", "true", "Emit Server header in HTTP responses" },
        { "virtual_hosts_enabled", "true", "Enable Host header based virtual host routing" },
        { "virtual_hosts", "", "Semicolon-separated host=static:path, host=proxy:http://host:port,https://backup:port, or host=script:runtime:path routes" },
        { "reverse_proxy_connect_timeout_seconds", "5", "HTTP reverse proxy upstream connect timeout" },
        { "reverse_proxy_read_timeout_seconds", "30", "HTTP reverse proxy upstream read timeout" },
        { "reverse_proxy_max_response_bytes", "1048576", "Maximum buffered reverse proxy upstream response bytes" },
        { "reverse_proxy_retry_count", "1", "Additional reverse proxy upstream attempts after the first failure" },
        { "reverse_proxy_tls_verify_upstream", "false", "Verify HTTPS reverse proxy upstream certificates with default trust paths" },
        { "reverse_proxy_circuit_breaker_enabled", "true", "Temporarily skip repeatedly failing reverse proxy upstreams" },
        { "reverse_proxy_circuit_breaker_failure_threshold", "3", "Failures before a reverse proxy upstream circuit opens" },
        { "reverse_proxy_circuit_breaker_cooldown_seconds", "10", "Seconds before retrying an opened reverse proxy upstream circuit" },
        { "modsecurity_enabled", "false", "Enable Rimau built-in ModSecurity-compatible WAF inspection" },
        { "modsecurity_owasp_crs_enabled", "true", "Enable built-in OWASP CRS-inspired rule subset for the WAF" },
        { "modsecurity_blocking_enabled", "true", "Block requests whose WAF anomaly score reaches the configured threshold" },
        { "modsecurity_anomaly_threshold", "5", "WAF anomaly score threshold for blocking" },
        { "modsecurity_max_inspection_bytes", "131072", "Maximum bytes inspected per request field by the WAF" },
        { "modsecurity_audit_log_enabled", "true", "Write WAF matches to the Rimau log" },
    };
    return defaults;
}

bool is_supported_key(const std::string& key)
{
    const auto& defaults = config_defaults();
    return std::any_of(defaults.begin(), defaults.end(), [&](const ConfigDefault& item) {
        return key == item.key;
    });
}

class Database {
public:
    explicit Database(const std::filesystem::path& database_path)
    {
        if (database_path.empty()) {
            throw std::runtime_error("database path is empty");
        }

        if (database_path != std::filesystem::path(":memory:") && database_path.has_parent_path()) {
            std::filesystem::create_directories(database_path.parent_path());
        }

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
            throw std::runtime_error("cannot open sqlite config database: " + message);
        }

        sqlite3_busy_timeout(db_, 5000);
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    ~Database()
    {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    sqlite3* get() const noexcept
    {
        return db_;
    }

    void exec(const char* sql) const
    {
        char* raw_error = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &raw_error);
        if (rc != SQLITE_OK) {
            std::string message = raw_error ? raw_error : sqlite3_errmsg(db_);
            sqlite3_free(raw_error);
            throw std::runtime_error("sqlite exec failed: " + message);
        }
    }

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement(sqlite3* db, const char* sql)
        : db_(db)
    {
        const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("sqlite prepare failed: " + std::string(sqlite3_errmsg(db_)));
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    ~Statement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    void bind_text(int index, const std::string& value)
    {
        const int rc = sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("sqlite bind failed: " + std::string(sqlite3_errmsg(db_)));
        }
    }

    int step()
    {
        return sqlite3_step(stmt_);
    }

    std::string column_text(int index) const
    {
        const auto* text = sqlite3_column_text(stmt_, index);
        return text ? reinterpret_cast<const char*>(text) : "";
    }

    int column_int(int index) const
    {
        return sqlite3_column_int(stmt_, index);
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

void create_schema(const Database& database)
{
    database.exec(
        "CREATE TABLE IF NOT EXISTS rimau_config ("
        "key TEXT PRIMARY KEY NOT NULL,"
        "value TEXT NOT NULL,"
        "description TEXT NOT NULL DEFAULT '',"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");");

    database.exec(
        "CREATE TRIGGER IF NOT EXISTS rimau_config_updated_at "
        "AFTER UPDATE ON rimau_config "
        "FOR EACH ROW "
        "BEGIN "
        "UPDATE rimau_config SET updated_at = CURRENT_TIMESTAMP WHERE key = OLD.key;"
        "END;");

    database.exec(
        "CREATE TABLE IF NOT EXISTS rimau_schema_migrations ("
        "version INTEGER PRIMARY KEY NOT NULL,"
        "name TEXT NOT NULL,"
        "applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");");

    database.exec(
        "INSERT OR IGNORE INTO rimau_schema_migrations(version, name) "
        "VALUES (1, 'initial rimau_config schema');");
}

int read_schema_version(const Database& database)
{
    Statement statement(database.get(), "SELECT COALESCE(MAX(version), 0) FROM rimau_schema_migrations;");
    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return statement.column_int(0);
    }
    if (rc == SQLITE_DONE) {
        return 0;
    }

    throw std::runtime_error("sqlite read schema version failed: " + std::string(sqlite3_errmsg(database.get())));
}

void validate_schema_version(const Database& database)
{
    const int version = read_schema_version(database);
    if (version > current_config_schema_version) {
        throw std::runtime_error(
            "sqlite config database schema version " + std::to_string(version) + " is newer than supported version "
            + std::to_string(current_config_schema_version));
    }
}

void insert_default(const Database& database, const ConfigDefault& item)
{
    Statement statement(
        database.get(),
        "INSERT OR IGNORE INTO rimau_config(key, value, description) VALUES (?, ?, ?);");
    statement.bind_text(1, item.key);
    statement.bind_text(2, item.value);
    statement.bind_text(3, item.description);

    const int rc = statement.step();
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("sqlite insert default failed: " + std::string(sqlite3_errmsg(database.get())));
    }
}

std::map<std::string, std::string> read_config_values(const Database& database)
{
    std::map<std::string, std::string> values;
    Statement statement(database.get(), "SELECT key, value FROM rimau_config;");

    while (true) {
        const int rc = statement.step();
        if (rc == SQLITE_ROW) {
            values[statement.column_text(0)] = statement.column_text(1);
            continue;
        }
        if (rc == SQLITE_DONE) {
            break;
        }
        throw std::runtime_error("sqlite read config failed: " + std::string(sqlite3_errmsg(database.get())));
    }

    return values;
}

std::string value_or_default(const std::map<std::string, std::string>& values, const char* key)
{
    const auto found = values.find(key);
    if (found != values.end()) {
        return found->second;
    }

    const auto& defaults = config_defaults();
    const auto default_value = std::find_if(defaults.begin(), defaults.end(), [&](const ConfigDefault& item) {
        return std::string(item.key) == key;
    });
    if (default_value == defaults.end()) {
        throw std::runtime_error("missing default config key: " + std::string(key));
    }

    return default_value->value;
}

void validate_config_value(const std::string& key, const std::string& value)
{
    if (!is_supported_key(key)) {
        throw std::runtime_error("unknown sqlite config key: " + key);
    }

    if (value.empty() && !is_optional_string_key(key)) {
        throw std::runtime_error("config value cannot be empty: " + key);
    }

    if (key == "port") {
        parse_port(value);
    } else if (key == "max_request_bytes") {
        parse_size(value, "max_request_bytes");
    } else if (key == "http_keep_alive_timeout_seconds") {
        parse_positive_int(value, "http_keep_alive_timeout_seconds");
    } else if (key == "http_keep_alive_max_requests") {
        parse_positive_int(value, "http_keep_alive_max_requests");
    } else if (key == "listen_backlog") {
        parse_positive_int(value, "listen_backlog");
    } else if (key == "worker_threads") {
        parse_non_negative_size(value, "worker_threads");
    } else if (key == "epoll_max_events") {
        parse_positive_int(value, "epoll_max_events");
    } else if (key == "tcp_keepalive_idle_seconds") {
        parse_positive_int(value, "tcp_keepalive_idle_seconds");
    } else if (key == "tcp_keepalive_interval_seconds") {
        parse_positive_int(value, "tcp_keepalive_interval_seconds");
    } else if (key == "tcp_keepalive_probe_count") {
        parse_positive_int(value, "tcp_keepalive_probe_count");
    } else if (key == "graceful_shutdown_timeout_seconds") {
        parse_positive_int(value, "graceful_shutdown_timeout_seconds");
    } else if (key == "connection_pool_size") {
        parse_non_negative_size(value, "connection_pool_size");
    } else if (key == "global_connection_limit") {
        parse_size(value, "global_connection_limit");
    } else if (key == "per_ip_connection_limit") {
        parse_size(value, "per_ip_connection_limit");
    } else if (key == "rate_limit_requests_per_minute") {
        parse_positive_int(value, "rate_limit_requests_per_minute");
    } else if (key == "request_timeout_seconds") {
        parse_positive_int(value, "request_timeout_seconds");
    } else if (key == "header_timeout_seconds") {
        parse_positive_int(value, "header_timeout_seconds");
    } else if (key == "body_timeout_seconds") {
        parse_positive_int(value, "body_timeout_seconds");
    } else if (key == "idle_timeout_seconds") {
        parse_positive_int(value, "idle_timeout_seconds");
    } else if (key == "websocket_max_frame_bytes") {
        parse_size(value, "websocket_max_frame_bytes");
    } else if (key == "reverse_proxy_connect_timeout_seconds") {
        parse_positive_int(value, "reverse_proxy_connect_timeout_seconds");
    } else if (key == "reverse_proxy_read_timeout_seconds") {
        parse_positive_int(value, "reverse_proxy_read_timeout_seconds");
    } else if (key == "reverse_proxy_max_response_bytes") {
        parse_size(value, "reverse_proxy_max_response_bytes");
    } else if (key == "reverse_proxy_retry_count") {
        parse_non_negative_size(value, "reverse_proxy_retry_count");
    } else if (key == "reverse_proxy_circuit_breaker_failure_threshold") {
        parse_size(value, "reverse_proxy_circuit_breaker_failure_threshold");
    } else if (key == "reverse_proxy_circuit_breaker_cooldown_seconds") {
        parse_positive_int(value, "reverse_proxy_circuit_breaker_cooldown_seconds");
    } else if (key == "modsecurity_anomaly_threshold") {
        parse_size(value, "modsecurity_anomaly_threshold");
    } else if (key == "modsecurity_max_inspection_bytes") {
        parse_size(value, "modsecurity_max_inspection_bytes");
    } else if (key == "tls_min_version" || key == "tls_max_version") {
        if (!valid_tls_version(value)) {
            throw std::runtime_error(key + " must be TLSv1.2 or TLSv1.3");
        }
    } else if (key == "server_name" || key == "directory_index" || key == "error_page" || key == "tls_cipher_list" || key == "tls_ciphersuites" || key == "tls_alpn_protocols"
        || key == "tls_sni_hosts" || key == "tls_sni_certificates" || key == "ip_allowlist" || key == "ip_blocklist"
        || key == "security_header_content_security_policy" || key == "security_header_strict_transport_security"
        || key == "security_header_x_content_type_options" || key == "security_header_x_frame_options"
        || key == "security_header_referrer_policy" || key == "security_header_cross_origin_opener_policy"
        || key == "virtual_hosts") {
        if (contains_control_character(value)) {
            throw std::runtime_error(key + " cannot contain control characters");
        }
        if (key == "directory_index" && !valid_directory_index(value)) {
            throw std::runtime_error("directory_index must be a file name without slashes");
        }
        if ((key == "ip_allowlist" || key == "ip_blocklist") && !valid_ip_list(value)) {
            throw std::runtime_error(key + " must contain IPv4/IPv6 exact addresses or CIDR ranges");
        }
        if (key == "tls_sni_certificates" && !valid_sni_certificate_map(value)) {
            throw std::runtime_error("tls_sni_certificates must use hostname=certificate.pem|private-key.pem entries separated by semicolons");
        }
        if (key == "virtual_hosts") {
            (void)rimau::http::parse_virtual_host_rules(value);
        }
    } else if (key == "http_keep_alive_enabled" || key == "reuse_port_enabled" || key == "tcp_keepalive_enabled" || key == "http1_enabled" || key == "http2_enabled" || key == "http3_enabled" || key == "tls_enabled" || key == "tls_sni_required" || key == "rate_limit_enabled" || key == "security_headers_enabled" || key == "server_header_enabled" || key == "virtual_hosts_enabled" || key == "reverse_proxy_tls_verify_upstream" || key == "reverse_proxy_circuit_breaker_enabled" || key == "modsecurity_enabled" || key == "modsecurity_owasp_crs_enabled" || key == "modsecurity_blocking_enabled" || key == "modsecurity_audit_log_enabled") {
        parse_bool(value, key.c_str());
    }
}

ServerConfig build_config(const std::map<std::string, std::string>& values)
{
    ServerConfig config;
    config.host = value_or_default(values, "host");
    config.port = parse_port(value_or_default(values, "port"));
    config.document_root = value_or_default(values, "document_root");
    config.directory_index = value_or_default(values, "directory_index");
    config.error_page = value_or_default(values, "error_page");
    config.max_request_bytes = parse_size(value_or_default(values, "max_request_bytes"), "max_request_bytes");
    config.http_keep_alive_enabled = parse_bool(value_or_default(values, "http_keep_alive_enabled"), "http_keep_alive_enabled");
    config.http_keep_alive_timeout_seconds = parse_positive_int(value_or_default(values, "http_keep_alive_timeout_seconds"), "http_keep_alive_timeout_seconds");
    config.http_keep_alive_max_requests = parse_positive_int(value_or_default(values, "http_keep_alive_max_requests"), "http_keep_alive_max_requests");
    config.listen_backlog = parse_positive_int(value_or_default(values, "listen_backlog"), "listen_backlog");
    config.server_name = value_or_default(values, "server_name");
    config.worker_threads = parse_non_negative_size(value_or_default(values, "worker_threads"), "worker_threads");
    config.epoll_max_events = parse_positive_int(value_or_default(values, "epoll_max_events"), "epoll_max_events");
    config.reuse_port_enabled = parse_bool(value_or_default(values, "reuse_port_enabled"), "reuse_port_enabled");
    config.tcp_keepalive_enabled = parse_bool(value_or_default(values, "tcp_keepalive_enabled"), "tcp_keepalive_enabled");
    config.tcp_keepalive_idle_seconds = parse_positive_int(value_or_default(values, "tcp_keepalive_idle_seconds"), "tcp_keepalive_idle_seconds");
    config.tcp_keepalive_interval_seconds = parse_positive_int(value_or_default(values, "tcp_keepalive_interval_seconds"), "tcp_keepalive_interval_seconds");
    config.tcp_keepalive_probe_count = parse_positive_int(value_or_default(values, "tcp_keepalive_probe_count"), "tcp_keepalive_probe_count");
    config.graceful_shutdown_timeout_seconds = parse_positive_int(value_or_default(values, "graceful_shutdown_timeout_seconds"), "graceful_shutdown_timeout_seconds");
    config.connection_pool_size = parse_non_negative_size(value_or_default(values, "connection_pool_size"), "connection_pool_size");
    config.http1_enabled = parse_bool(value_or_default(values, "http1_enabled"), "http1_enabled");
    config.http2_enabled = parse_bool(value_or_default(values, "http2_enabled"), "http2_enabled");
    config.http3_enabled = parse_bool(value_or_default(values, "http3_enabled"), "http3_enabled");
    config.tls_enabled = parse_bool(value_or_default(values, "tls_enabled"), "tls_enabled");
    config.tls_certificate_file = value_or_default(values, "tls_certificate_file");
    config.tls_private_key_file = value_or_default(values, "tls_private_key_file");
    config.tls_min_version = value_or_default(values, "tls_min_version");
    config.tls_max_version = value_or_default(values, "tls_max_version");
    config.tls_cipher_list = value_or_default(values, "tls_cipher_list");
    config.tls_ciphersuites = value_or_default(values, "tls_ciphersuites");
    config.tls_alpn_protocols = value_or_default(values, "tls_alpn_protocols");
    config.tls_sni_hosts = value_or_default(values, "tls_sni_hosts");
    config.tls_sni_certificates = value_or_default(values, "tls_sni_certificates");
    config.tls_sni_required = parse_bool(value_or_default(values, "tls_sni_required"), "tls_sni_required");
    config.request_timeout_seconds = parse_positive_int(value_or_default(values, "request_timeout_seconds"), "request_timeout_seconds");
    config.header_timeout_seconds = parse_positive_int(value_or_default(values, "header_timeout_seconds"), "header_timeout_seconds");
    config.body_timeout_seconds = parse_positive_int(value_or_default(values, "body_timeout_seconds"), "body_timeout_seconds");
    config.idle_timeout_seconds = parse_positive_int(value_or_default(values, "idle_timeout_seconds"), "idle_timeout_seconds");
    config.global_connection_limit = parse_size(value_or_default(values, "global_connection_limit"), "global_connection_limit");
    config.per_ip_connection_limit = parse_size(value_or_default(values, "per_ip_connection_limit"), "per_ip_connection_limit");
    config.rate_limit_enabled = parse_bool(value_or_default(values, "rate_limit_enabled"), "rate_limit_enabled");
    config.rate_limit_requests_per_minute = parse_positive_int(value_or_default(values, "rate_limit_requests_per_minute"), "rate_limit_requests_per_minute");
    config.ip_allowlist = value_or_default(values, "ip_allowlist");
    config.ip_blocklist = value_or_default(values, "ip_blocklist");
    config.websocket_max_frame_bytes = parse_size(value_or_default(values, "websocket_max_frame_bytes"), "websocket_max_frame_bytes");
    config.security_headers_enabled = parse_bool(value_or_default(values, "security_headers_enabled"), "security_headers_enabled");
    config.security_header_content_security_policy = value_or_default(values, "security_header_content_security_policy");
    config.security_header_strict_transport_security = value_or_default(values, "security_header_strict_transport_security");
    config.security_header_x_content_type_options = value_or_default(values, "security_header_x_content_type_options");
    config.security_header_x_frame_options = value_or_default(values, "security_header_x_frame_options");
    config.security_header_referrer_policy = value_or_default(values, "security_header_referrer_policy");
    config.security_header_cross_origin_opener_policy = value_or_default(values, "security_header_cross_origin_opener_policy");
    config.server_header_enabled = parse_bool(value_or_default(values, "server_header_enabled"), "server_header_enabled");
    config.virtual_hosts_enabled = parse_bool(value_or_default(values, "virtual_hosts_enabled"), "virtual_hosts_enabled");
    config.virtual_hosts = value_or_default(values, "virtual_hosts");
    config.reverse_proxy_connect_timeout_seconds = parse_positive_int(value_or_default(values, "reverse_proxy_connect_timeout_seconds"), "reverse_proxy_connect_timeout_seconds");
    config.reverse_proxy_read_timeout_seconds = parse_positive_int(value_or_default(values, "reverse_proxy_read_timeout_seconds"), "reverse_proxy_read_timeout_seconds");
    config.reverse_proxy_max_response_bytes = parse_size(value_or_default(values, "reverse_proxy_max_response_bytes"), "reverse_proxy_max_response_bytes");
    config.reverse_proxy_retry_count = parse_non_negative_size(value_or_default(values, "reverse_proxy_retry_count"), "reverse_proxy_retry_count");
    config.reverse_proxy_tls_verify_upstream = parse_bool(value_or_default(values, "reverse_proxy_tls_verify_upstream"), "reverse_proxy_tls_verify_upstream");
    config.reverse_proxy_circuit_breaker_enabled = parse_bool(value_or_default(values, "reverse_proxy_circuit_breaker_enabled"), "reverse_proxy_circuit_breaker_enabled");
    config.reverse_proxy_circuit_breaker_failure_threshold = parse_size(value_or_default(values, "reverse_proxy_circuit_breaker_failure_threshold"), "reverse_proxy_circuit_breaker_failure_threshold");
    config.reverse_proxy_circuit_breaker_cooldown_seconds = parse_positive_int(value_or_default(values, "reverse_proxy_circuit_breaker_cooldown_seconds"), "reverse_proxy_circuit_breaker_cooldown_seconds");
    config.modsecurity_enabled = parse_bool(value_or_default(values, "modsecurity_enabled"), "modsecurity_enabled");
    config.modsecurity_owasp_crs_enabled = parse_bool(value_or_default(values, "modsecurity_owasp_crs_enabled"), "modsecurity_owasp_crs_enabled");
    config.modsecurity_blocking_enabled = parse_bool(value_or_default(values, "modsecurity_blocking_enabled"), "modsecurity_blocking_enabled");
    config.modsecurity_anomaly_threshold = parse_size(value_or_default(values, "modsecurity_anomaly_threshold"), "modsecurity_anomaly_threshold");
    config.modsecurity_max_inspection_bytes = parse_size(value_or_default(values, "modsecurity_max_inspection_bytes"), "modsecurity_max_inspection_bytes");
    config.modsecurity_audit_log_enabled = parse_bool(value_or_default(values, "modsecurity_audit_log_enabled"), "modsecurity_audit_log_enabled");

    if (config.host.empty()) {
        throw std::runtime_error("host cannot be empty");
    }
    if (config.document_root.empty()) {
        throw std::runtime_error("document_root cannot be empty");
    }
    if (config.server_name.empty()) {
        throw std::runtime_error("server_name cannot be empty");
    }
    if (contains_control_character(config.server_name)) {
        throw std::runtime_error("server_name cannot contain control characters");
    }
    const auto ensure_no_control = [](const char* key, const std::string& value) {
        if (contains_control_character(value)) {
            throw std::runtime_error(std::string(key) + " cannot contain control characters");
        }
    };
    ensure_no_control("tls_cipher_list", config.tls_cipher_list);
    ensure_no_control("directory_index", config.directory_index);
    ensure_no_control("error_page", config.error_page.string());
    ensure_no_control("tls_ciphersuites", config.tls_ciphersuites);
    ensure_no_control("tls_alpn_protocols", config.tls_alpn_protocols);
    ensure_no_control("tls_sni_hosts", config.tls_sni_hosts);
    ensure_no_control("tls_sni_certificates", config.tls_sni_certificates);
    ensure_no_control("ip_allowlist", config.ip_allowlist);
    ensure_no_control("ip_blocklist", config.ip_blocklist);
    ensure_no_control("security_header_content_security_policy", config.security_header_content_security_policy);
    ensure_no_control("security_header_strict_transport_security", config.security_header_strict_transport_security);
    ensure_no_control("security_header_x_content_type_options", config.security_header_x_content_type_options);
    ensure_no_control("security_header_x_frame_options", config.security_header_x_frame_options);
    ensure_no_control("security_header_referrer_policy", config.security_header_referrer_policy);
    ensure_no_control("security_header_cross_origin_opener_policy", config.security_header_cross_origin_opener_policy);
    ensure_no_control("virtual_hosts", config.virtual_hosts);
    if (!valid_tls_version(config.tls_min_version)) {
        throw std::runtime_error("tls_min_version must be TLSv1.2 or TLSv1.3");
    }
    if (!valid_directory_index(config.directory_index)) {
        throw std::runtime_error("directory_index must be a file name without slashes");
    }
    if (!valid_tls_version(config.tls_max_version)) {
        throw std::runtime_error("tls_max_version must be TLSv1.2 or TLSv1.3");
    }
    if (config.tls_min_version == "TLSv1.3" && config.tls_max_version == "TLSv1.2") {
        throw std::runtime_error("tls_min_version cannot be newer than tls_max_version");
    }
    validate_alpn_protocols(config);
    if (!valid_sni_certificate_map(config.tls_sni_certificates)) {
        throw std::runtime_error("tls_sni_certificates must use hostname=certificate.pem|private-key.pem entries separated by semicolons");
    }
    if (!valid_ip_list(config.ip_allowlist)) {
        throw std::runtime_error("ip_allowlist must contain IPv4/IPv6 exact addresses or CIDR ranges");
    }
    if (!valid_ip_list(config.ip_blocklist)) {
        throw std::runtime_error("ip_blocklist must contain IPv4/IPv6 exact addresses or CIDR ranges");
    }
    (void)rimau::http::parse_virtual_host_rules(config.virtual_hosts);
    if (config.tls_certificate_file.empty()) {
        throw std::runtime_error("tls_certificate_file cannot be empty");
    }
    if (config.tls_private_key_file.empty()) {
        throw std::runtime_error("tls_private_key_file cannot be empty");
    }
    if (config.tls_enabled) {
        if (!std::filesystem::is_regular_file(config.tls_certificate_file)) {
            throw std::runtime_error("tls_certificate_file does not exist: " + config.tls_certificate_file.string());
        }
        if (!std::filesystem::is_regular_file(config.tls_private_key_file)) {
            throw std::runtime_error("tls_private_key_file does not exist: " + config.tls_private_key_file.string());
        }
        for (const auto& entry : split_delimited(config.tls_sni_certificates, ';')) {
            const auto equals = entry.find('=');
            const auto separator = entry.find('|', equals == std::string::npos ? 0 : equals + 1);
            const std::filesystem::path certificate_file = trim(entry.substr(equals + 1, separator - equals - 1));
            const std::filesystem::path private_key_file = trim(entry.substr(separator + 1));
            if (!std::filesystem::is_regular_file(certificate_file)) {
                throw std::runtime_error("tls_sni_certificates certificate file does not exist: " + certificate_file.string());
            }
            if (!std::filesystem::is_regular_file(private_key_file)) {
                throw std::runtime_error("tls_sni_certificates private key file does not exist: " + private_key_file.string());
            }
        }
    }

    return config;
}

} // namespace

void initialize_config_database(const std::filesystem::path& database_path)
{
    const Database database(database_path);
    create_schema(database);
    validate_schema_version(database);

    for (const auto& item : config_defaults()) {
        insert_default(database, item);
    }
}

int config_schema_version(const std::filesystem::path& database_path)
{
    initialize_config_database(database_path);

    const Database database(database_path);
    return read_schema_version(database);
}

ServerConfig load_config_from_database(const std::filesystem::path& database_path)
{
    initialize_config_database(database_path);

    const Database database(database_path);
    auto config = build_config(read_config_values(database));
    config.database_path = database_path;
    return config;
}

void set_config_value(const std::filesystem::path& database_path, const std::string& key, const std::string& value)
{
    validate_config_value(key, value);
    initialize_config_database(database_path);

    const Database database(database_path);
    Statement statement(database.get(), "UPDATE rimau_config SET value = ? WHERE key = ?;");
    statement.bind_text(1, value);
    statement.bind_text(2, key);

    const int rc = statement.step();
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("sqlite update config failed: " + std::string(sqlite3_errmsg(database.get())));
    }
}

std::vector<std::string> supported_config_keys()
{
    std::vector<std::string> keys;
    keys.reserve(config_defaults().size());
    for (const auto& item : config_defaults()) {
        keys.emplace_back(item.key);
    }
    return keys;
}

std::string describe_config(const ServerConfig& config)
{
    std::ostringstream output;
    output << "host=" << config.host
           << ", port=" << config.port
           << ", document_root=" << config.document_root.string()
           << ", directory_index=" << config.directory_index
           << ", error_page=" << config.error_page.string()
           << ", max_request_bytes=" << config.max_request_bytes
           << ", http_keep_alive_enabled=" << (config.http_keep_alive_enabled ? "true" : "false")
           << ", http_keep_alive_timeout_seconds=" << config.http_keep_alive_timeout_seconds
           << ", http_keep_alive_max_requests=" << config.http_keep_alive_max_requests
           << ", listen_backlog=" << config.listen_backlog
           << ", server_name=" << config.server_name
           << ", worker_threads=" << config.worker_threads
           << ", epoll_max_events=" << config.epoll_max_events
           << ", reuse_port_enabled=" << (config.reuse_port_enabled ? "true" : "false")
           << ", tcp_keepalive_enabled=" << (config.tcp_keepalive_enabled ? "true" : "false")
           << ", tcp_keepalive_idle_seconds=" << config.tcp_keepalive_idle_seconds
           << ", tcp_keepalive_interval_seconds=" << config.tcp_keepalive_interval_seconds
           << ", tcp_keepalive_probe_count=" << config.tcp_keepalive_probe_count
           << ", graceful_shutdown_timeout_seconds=" << config.graceful_shutdown_timeout_seconds
           << ", connection_pool_size=" << config.connection_pool_size
           << ", http1_enabled=" << (config.http1_enabled ? "true" : "false")
           << ", http2_enabled=" << (config.http2_enabled ? "true" : "false")
           << ", http3_enabled=" << (config.http3_enabled ? "true" : "false")
           << ", tls_enabled=" << (config.tls_enabled ? "true" : "false")
           << ", tls_certificate_file=" << config.tls_certificate_file.string()
           << ", tls_private_key_file=" << config.tls_private_key_file.string()
           << ", tls_min_version=" << config.tls_min_version
           << ", tls_max_version=" << config.tls_max_version
           << ", tls_cipher_list=" << config.tls_cipher_list
           << ", tls_ciphersuites=" << config.tls_ciphersuites
           << ", tls_alpn_protocols=" << config.tls_alpn_protocols
           << ", tls_sni_hosts=" << config.tls_sni_hosts
           << ", tls_sni_certificates=" << config.tls_sni_certificates
           << ", tls_sni_required=" << (config.tls_sni_required ? "true" : "false")
           << ", request_timeout_seconds=" << config.request_timeout_seconds
           << ", header_timeout_seconds=" << config.header_timeout_seconds
           << ", body_timeout_seconds=" << config.body_timeout_seconds
           << ", idle_timeout_seconds=" << config.idle_timeout_seconds
           << ", global_connection_limit=" << config.global_connection_limit
           << ", per_ip_connection_limit=" << config.per_ip_connection_limit
           << ", rate_limit_enabled=" << (config.rate_limit_enabled ? "true" : "false")
           << ", rate_limit_requests_per_minute=" << config.rate_limit_requests_per_minute
           << ", ip_allowlist=" << config.ip_allowlist
           << ", ip_blocklist=" << config.ip_blocklist
           << ", websocket_max_frame_bytes=" << config.websocket_max_frame_bytes
           << ", security_headers_enabled=" << (config.security_headers_enabled ? "true" : "false")
           << ", security_header_content_security_policy=" << config.security_header_content_security_policy
           << ", security_header_strict_transport_security=" << config.security_header_strict_transport_security
           << ", security_header_x_content_type_options=" << config.security_header_x_content_type_options
           << ", security_header_x_frame_options=" << config.security_header_x_frame_options
           << ", security_header_referrer_policy=" << config.security_header_referrer_policy
           << ", security_header_cross_origin_opener_policy=" << config.security_header_cross_origin_opener_policy
           << ", server_header_enabled=" << (config.server_header_enabled ? "true" : "false")
           << ", virtual_hosts_enabled=" << (config.virtual_hosts_enabled ? "true" : "false")
           << ", virtual_hosts=" << config.virtual_hosts
           << ", reverse_proxy_connect_timeout_seconds=" << config.reverse_proxy_connect_timeout_seconds
           << ", reverse_proxy_read_timeout_seconds=" << config.reverse_proxy_read_timeout_seconds
           << ", reverse_proxy_max_response_bytes=" << config.reverse_proxy_max_response_bytes
           << ", reverse_proxy_retry_count=" << config.reverse_proxy_retry_count
           << ", reverse_proxy_tls_verify_upstream=" << (config.reverse_proxy_tls_verify_upstream ? "true" : "false")
           << ", reverse_proxy_circuit_breaker_enabled=" << (config.reverse_proxy_circuit_breaker_enabled ? "true" : "false")
           << ", reverse_proxy_circuit_breaker_failure_threshold=" << config.reverse_proxy_circuit_breaker_failure_threshold
           << ", reverse_proxy_circuit_breaker_cooldown_seconds=" << config.reverse_proxy_circuit_breaker_cooldown_seconds
           << ", modsecurity_enabled=" << (config.modsecurity_enabled ? "true" : "false")
           << ", modsecurity_owasp_crs_enabled=" << (config.modsecurity_owasp_crs_enabled ? "true" : "false")
           << ", modsecurity_blocking_enabled=" << (config.modsecurity_blocking_enabled ? "true" : "false")
           << ", modsecurity_anomaly_threshold=" << config.modsecurity_anomaly_threshold
           << ", modsecurity_max_inspection_bytes=" << config.modsecurity_max_inspection_bytes
           << ", modsecurity_audit_log_enabled=" << (config.modsecurity_audit_log_enabled ? "true" : "false");
    return output.str();
}

} // namespace rimau::core
