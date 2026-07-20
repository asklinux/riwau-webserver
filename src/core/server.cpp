#include "rimau/core/server.hpp"

#include "rimau/core/logger.hpp"
#include "rimau/http/http1_session.hpp"
#include "rimau/http/parser.hpp"
#include "rimau/http/response.hpp"
#include "rimau/http/response_sink.hpp"
#include "rimau/http/static_file_handler.hpp"
#include "rimau/http/transaction.hpp"
#include "rimau/http/virtual_host.hpp"
#include "rimau/http/waf.hpp"
#include "rimau/protocol/http2_frame.hpp"
#include "rimau/protocol/http2_gateway.hpp"
#include "rimau/protocol/http2_hpack.hpp"
#include "rimau/protocol/http3_gateway.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>

namespace rimau::core {
namespace {

volatile std::sig_atomic_t g_shutdown_signal_requested = 0;
volatile std::sig_atomic_t g_reload_signal_requested = 0;
volatile std::sig_atomic_t g_last_signal = 0;
std::atomic<std::size_t> g_next_websocket_proxy_upstream { 0 };
std::atomic<std::size_t> g_next_request_body_spool { 0 };

void handle_runtime_signal(int signal_number)
{
    g_last_signal = signal_number;
    if (signal_number == SIGTERM || signal_number == SIGINT) {
        g_shutdown_signal_requested = 1;
    } else if (signal_number == SIGHUP) {
        g_reload_signal_requested = 1;
    }
}

void install_runtime_signal_handlers()
{
    struct sigaction action {};
    action.sa_handler = handle_runtime_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGTERM, &action, nullptr) < 0) {
        throw std::runtime_error(std::string("sigaction(SIGTERM) failed: ") + std::strerror(errno));
    }
    if (sigaction(SIGINT, &action, nullptr) < 0) {
        throw std::runtime_error(std::string("sigaction(SIGINT) failed: ") + std::strerror(errno));
    }
    if (sigaction(SIGHUP, &action, nullptr) < 0) {
        throw std::runtime_error(std::string("sigaction(SIGHUP) failed: ") + std::strerror(errno));
    }
}

class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(int fd)
        : fd_(fd)
    {
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1))
    {
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    ~UniqueFd()
    {
        reset();
    }

    int get() const noexcept
    {
        return fd_;
    }

    bool valid() const noexcept
    {
        return fd_ >= 0;
    }

    int release() noexcept
    {
        return std::exchange(fd_, -1);
    }

    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

[[noreturn]] void throw_errno(const std::string& action)
{
    throw std::runtime_error(action + ": " + std::strerror(errno));
}

bool would_block() noexcept
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

std::string openssl_error()
{
    const unsigned long code = ERR_get_error();
    if (code == 0) {
        return "unknown OpenSSL error";
    }

    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

std::uint32_t event_for_ssl_error(int error)
{
    if (error == SSL_ERROR_WANT_WRITE) {
        return EPOLLOUT;
    }
    return EPOLLIN;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

std::vector<std::string> split_csv(std::string_view value)
{
    std::vector<std::string> items;
    std::istringstream input { std::string(value) };
    std::string item;
    while (std::getline(input, item, ',')) {
        item = trim(std::move(item));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
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

bool valid_header_name(std::string_view name)
{
    if (name.empty()) {
        return false;
    }

    for (const unsigned char ch : name) {
        const bool token_char = std::isalnum(ch) || ch == '!' || ch == '#' || ch == '$' || ch == '%'
            || ch == '&' || ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.'
            || ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
        if (!token_char) {
            return false;
        }
    }

    return true;
}

struct ParsedIpAddress {
    int family = AF_UNSPEC;
    std::array<unsigned char, 16> bytes {};
    int bit_count = 0;
};

std::optional<ParsedIpAddress> parse_ip_address(std::string_view value)
{
    const std::string text(value);

    in_addr ipv4 {};
    if (inet_pton(AF_INET, text.c_str(), &ipv4) == 1) {
        ParsedIpAddress parsed;
        parsed.family = AF_INET;
        parsed.bit_count = 32;
        std::memcpy(parsed.bytes.data(), &ipv4.s_addr, 4);
        return parsed;
    }

    in6_addr ipv6 {};
    if (inet_pton(AF_INET6, text.c_str(), &ipv6) == 1) {
        ParsedIpAddress parsed;
        parsed.family = AF_INET6;
        parsed.bit_count = 128;
        std::memcpy(parsed.bytes.data(), &ipv6.s6_addr, 16);
        return parsed;
    }

    return std::nullopt;
}

bool cidr_match(const ParsedIpAddress& ip, const ParsedIpAddress& network, int prefix_length)
{
    if (ip.family != network.family || prefix_length < 0 || prefix_length > ip.bit_count) {
        return false;
    }

    const int full_bytes = prefix_length / 8;
    const int remaining_bits = prefix_length % 8;

    for (int index = 0; index < full_bytes; ++index) {
        if (ip.bytes[static_cast<std::size_t>(index)] != network.bytes[static_cast<std::size_t>(index)]) {
            return false;
        }
    }

    if (remaining_bits == 0) {
        return true;
    }

    const auto mask = static_cast<unsigned char>(0xffU << (8 - remaining_bits));
    return (ip.bytes[static_cast<std::size_t>(full_bytes)] & mask) == (network.bytes[static_cast<std::size_t>(full_bytes)] & mask);
}

std::optional<int> parse_prefix(std::string_view value, int max_prefix)
{
    if (value.empty()) {
        return std::nullopt;
    }

    int prefix = 0;
    for (const char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
        prefix = prefix * 10 + (ch - '0');
        if (prefix > max_prefix) {
            return std::nullopt;
        }
    }
    return prefix;
}

bool ip_matches_token(std::string_view ip_text, std::string token)
{
    const auto ip = parse_ip_address(ip_text);
    if (!ip) {
        return false;
    }

    token = trim(std::move(token));
    const auto slash = token.find('/');
    if (slash == std::string::npos) {
        const auto exact = parse_ip_address(token);
        return exact && exact->family == ip->family && exact->bytes == ip->bytes;
    }

    const auto network = parse_ip_address(std::string_view(token).substr(0, slash));
    if (!network || network->family != ip->family) {
        return false;
    }

    const auto prefix = parse_prefix(std::string_view(token).substr(slash + 1), network->bit_count);
    if (!prefix) {
        return false;
    }

    return cidr_match(*ip, *network, *prefix);
}

bool ip_matches_list(std::string_view ip_text, std::string_view list)
{
    for (auto token : split_csv(list)) {
        if (ip_matches_token(ip_text, std::move(token))) {
            return true;
        }
    }
    return false;
}

bool connection_header_has_token(const rimau::http::Request& request, const std::string& token)
{
    const auto header = request.header("connection");
    if (!header) {
        return false;
    }

    std::istringstream tokens(*header);
    std::string item;
    while (std::getline(tokens, item, ',')) {
        if (lowercase(trim(std::move(item))) == token) {
            return true;
        }
    }

    return false;
}

std::optional<std::size_t> parse_decimal_size(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    std::size_t parsed = 0;
    for (const char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
        const auto digit = static_cast<std::size_t>(ch - '0');
        if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        parsed = parsed * 10 + digit;
    }
    return parsed;
}

std::optional<std::size_t> parse_hex_size(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    std::size_t parsed = 0;
    for (const char ch : value) {
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = 10 + ch - 'a';
        } else if (ch >= 'A' && ch <= 'F') {
            digit = 10 + ch - 'A';
        } else {
            return std::nullopt;
        }

        if (parsed > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(digit)) / 16) {
            return std::nullopt;
        }
        parsed = parsed * 16 + static_cast<std::size_t>(digit);
    }

    return parsed;
}

std::string base64_encode(const unsigned char* data, std::size_t size)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((size + 2) / 3) * 4);

    for (std::size_t index = 0; index < size; index += 3) {
        const std::uint32_t octet_a = data[index];
        const std::uint32_t octet_b = index + 1 < size ? data[index + 1] : 0;
        const std::uint32_t octet_c = index + 2 < size ? data[index + 2] : 0;
        const std::uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output.push_back(table[(triple >> 18) & 0x3f]);
        output.push_back(table[(triple >> 12) & 0x3f]);
        output.push_back(index + 1 < size ? table[(triple >> 6) & 0x3f] : '=');
        output.push_back(index + 2 < size ? table[triple & 0x3f] : '=');
    }

    return output;
}

std::string websocket_accept_key(const std::string& key)
{
    static constexpr std::string_view guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string input = key + std::string(guid);
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int digest_size = 0;
    if (EVP_Digest(input.data(), input.size(), digest.data(), &digest_size, EVP_sha1(), nullptr) != 1) {
        throw std::runtime_error("EVP_Digest SHA1 failed for WebSocket accept key");
    }
    return base64_encode(digest.data(), digest_size);
}

bool is_websocket_upgrade_request(const rimau::http::Request& request)
{
    const auto upgrade = request.header("upgrade");
    const auto version = request.header("sec-websocket-version");
    const auto key = request.header("sec-websocket-key");
    return request.method == "GET"
        && upgrade
        && lowercase(*upgrade) == "websocket"
        && connection_header_has_token(request, "upgrade")
        && version
        && trim(*version) == "13"
        && key
        && !trim(*key).empty();
}

rimau::http::Response websocket_handshake_response(const rimau::http::Request& request)
{
    const auto key = request.header("sec-websocket-key");
    if (!key) {
        return rimau::http::text_response(400, "Missing Sec-WebSocket-Key\n");
    }

    rimau::http::Response response;
    response.status = 101;
    response.reason = rimau::http::reason_phrase(response.status);
    response.headers["upgrade"] = "websocket";
    response.headers["connection"] = "Upgrade";
    response.headers["sec-websocket-accept"] = websocket_accept_key(trim(*key));
    return response;
}

std::string websocket_frame(std::uint8_t opcode, std::string_view payload)
{
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | (opcode & 0x0f)));

    if (payload.size() < 126) {
        frame.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xffff) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
        frame.push_back(static_cast<char>(payload.size() & 0xff));
    } else {
        frame.push_back(127);
        const auto length = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((length >> shift) & 0xff));
        }
    }

    frame.append(payload);
    return frame;
}

std::size_t resolve_worker_count(const ServerConfig& config)
{
    if (config.worker_threads > 0) {
        return config.worker_threads;
    }

    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    return hardware_threads == 0 ? 1 : static_cast<std::size_t>(hardware_threads);
}

template <typename Value>
void add_changed_key(std::vector<std::string>& keys, const char* key, const Value& current, const Value& next)
{
    if (current != next) {
        keys.emplace_back(key);
    }
}

std::string join_keys(const std::vector<std::string>& keys)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (index > 0) {
            output << ", ";
        }
        output << keys[index];
    }
    return output.str();
}

std::vector<std::string> live_reload_blockers(const ServerConfig& current, const ServerConfig& next)
{
    std::vector<std::string> blockers;
    add_changed_key(blockers, "host", current.host, next.host);
    add_changed_key(blockers, "port", current.port, next.port);
    add_changed_key(blockers, "listen_backlog", current.listen_backlog, next.listen_backlog);
    add_changed_key(blockers, "worker_threads", current.worker_threads, next.worker_threads);
    add_changed_key(blockers, "epoll_max_events", current.epoll_max_events, next.epoll_max_events);
    add_changed_key(blockers, "reuse_port_enabled", current.reuse_port_enabled, next.reuse_port_enabled);
    add_changed_key(blockers, "connection_pool_size", current.connection_pool_size, next.connection_pool_size);
    add_changed_key(blockers, "http1_enabled", current.http1_enabled, next.http1_enabled);
    add_changed_key(blockers, "tls_enabled", current.tls_enabled, next.tls_enabled);
    return blockers;
}

bool tls_runtime_settings_changed(const ServerConfig& current, const ServerConfig& next)
{
    return current.tls_certificate_file != next.tls_certificate_file
        || current.tls_private_key_file != next.tls_private_key_file
        || current.tls_min_version != next.tls_min_version
        || current.tls_max_version != next.tls_max_version
        || current.tls_cipher_list != next.tls_cipher_list
        || current.tls_ciphersuites != next.tls_ciphersuites
        || current.tls_alpn_protocols != next.tls_alpn_protocols
        || current.tls_sni_hosts != next.tls_sni_hosts
        || current.tls_sni_certificates != next.tls_sni_certificates
        || current.tls_sni_required != next.tls_sni_required;
}

void log_protocol_config_warnings(const ServerConfig& config)
{
    if (config.http2_enabled) {
        log(LogLevel::warning, "HTTP/2 is enabled in SQLite config but support is still partial: " + rimau::protocol::http2_status(config));
    }

    if (config.http3_enabled) {
        log(LogLevel::warning, "HTTP/3 is enabled in SQLite config but is not served yet: " + rimau::protocol::http3_status(config));
    }
}

void set_required_socket_option(int fd, int level, int name, int value, const char* option_name)
{
    if (setsockopt(fd, level, name, &value, sizeof(value)) < 0) {
        throw std::runtime_error(std::string("setsockopt(") + option_name + ") failed: " + std::strerror(errno));
    }
}

void set_optional_socket_option(int fd, int level, int name, int value, const char* option_name)
{
    if (setsockopt(fd, level, name, &value, sizeof(value)) < 0) {
        log(LogLevel::warning, std::string("setsockopt(") + option_name + ") failed: " + std::strerror(errno));
    }
}

void configure_client_socket(int fd, const ServerConfig& config)
{
    if (!config.tcp_keepalive_enabled) {
        return;
    }

    set_optional_socket_option(fd, SOL_SOCKET, SO_KEEPALIVE, 1, "SO_KEEPALIVE");

#if defined(TCP_KEEPIDLE)
    set_optional_socket_option(fd, IPPROTO_TCP, TCP_KEEPIDLE, config.tcp_keepalive_idle_seconds, "TCP_KEEPIDLE");
#endif
#if defined(TCP_KEEPINTVL)
    set_optional_socket_option(fd, IPPROTO_TCP, TCP_KEEPINTVL, config.tcp_keepalive_interval_seconds, "TCP_KEEPINTVL");
#endif
#if defined(TCP_KEEPCNT)
    set_optional_socket_option(fd, IPPROTO_TCP, TCP_KEEPCNT, config.tcp_keepalive_probe_count, "TCP_KEEPCNT");
#endif
}

struct SocketAddress {
    sockaddr_storage storage {};
    socklen_t length = 0;
    int family = AF_UNSPEC;
};

SocketAddress make_bind_address(const std::string& host, std::uint16_t port)
{
    SocketAddress address;

    sockaddr_in ipv4 {};
    ipv4.sin_family = AF_INET;
    ipv4.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &ipv4.sin_addr) == 1) {
        std::memcpy(&address.storage, &ipv4, sizeof(ipv4));
        address.length = sizeof(ipv4);
        address.family = AF_INET;
        return address;
    }

    sockaddr_in6 ipv6 {};
    ipv6.sin6_family = AF_INET6;
    ipv6.sin6_port = htons(port);
    if (inet_pton(AF_INET6, host.c_str(), &ipv6.sin6_addr) == 1) {
        std::memcpy(&address.storage, &ipv6, sizeof(ipv6));
        address.length = sizeof(ipv6);
        address.family = AF_INET6;
        return address;
    }

    throw std::runtime_error("host must be an IPv4 or IPv6 address for the current scaffold: " + host);
}

std::string render_client_ip(const sockaddr_storage& address)
{
    char buffer[INET6_ADDRSTRLEN] {};

    if (address.ss_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        const char* rendered = inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer));
        return rendered ? rendered : "0.0.0.0";
    }

    if (address.ss_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
            in_addr mapped_ipv4 {};
            std::memcpy(&mapped_ipv4.s_addr, &ipv6->sin6_addr.s6_addr[12], 4);
            const char* rendered = inet_ntop(AF_INET, &mapped_ipv4, buffer, sizeof(buffer));
            return rendered ? rendered : "0.0.0.0";
        }

        const char* rendered = inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer));
        return rendered ? rendered : "::";
    }

    return "unknown";
}

int tls_version_number(const std::string& version)
{
    if (version == "TLSv1.2") {
        return TLS1_2_VERSION;
    }
    if (version == "TLSv1.3") {
        return TLS1_3_VERSION;
    }
    throw std::runtime_error("unsupported TLS version: " + version);
}

std::vector<unsigned char> build_alpn_wire_list(const ServerConfig& config)
{
    std::vector<unsigned char> wire;
    for (const auto& protocol : split_csv(config.tls_alpn_protocols)) {
        if (protocol.empty() || protocol.size() > 255) {
            throw std::runtime_error("invalid ALPN protocol length");
        }
        if (protocol == "http/1.1" && config.http1_enabled) {
            wire.push_back(static_cast<unsigned char>(protocol.size()));
            wire.insert(wire.end(), protocol.begin(), protocol.end());
            continue;
        }
        if (protocol == "h2" && config.http2_enabled) {
            wire.push_back(static_cast<unsigned char>(protocol.size()));
            wire.insert(wire.end(), protocol.begin(), protocol.end());
            continue;
        }
        if (protocol == "h3") {
            throw std::runtime_error("unsupported ALPN protocol until HTTP/3 serving is implemented: " + protocol);
        }
        if (protocol == "http/1.1" || protocol == "h2") {
            continue;
        }
        throw std::runtime_error("unsupported ALPN protocol: " + protocol);
    }
    if (wire.empty()) {
        throw std::runtime_error("tls_alpn_protocols does not contain a protocol enabled by Rimau SQLite config");
    }
    return wire;
}

std::string selected_alpn_protocol(SSL* ssl)
{
    const unsigned char* data = nullptr;
    unsigned int length = 0;
    SSL_get0_alpn_selected(ssl, &data, &length);
    if (!data || length == 0) {
        return {};
    }
    return { reinterpret_cast<const char*>(data), length };
}

bool alpn_protocol_configured(std::string_view protocols, std::string_view expected)
{
    for (const auto& protocol : split_csv(protocols)) {
        if (protocol == expected) {
            return true;
        }
    }
    return false;
}

bool hostname_matches_pattern(std::string hostname, std::string pattern)
{
    hostname = lowercase(std::move(hostname));
    pattern = lowercase(std::move(pattern));
    if (pattern.starts_with("*.")) {
        const auto suffix = pattern.substr(1);
        return hostname.size() > suffix.size() && hostname.ends_with(suffix);
    }
    return hostname == pattern;
}

struct SniCertificateSpec {
    std::string pattern;
    std::filesystem::path certificate_file;
    std::filesystem::path private_key_file;
};

std::vector<SniCertificateSpec> parse_sni_certificate_specs(std::string_view value)
{
    std::vector<SniCertificateSpec> specs;
    for (const auto& entry : split_delimited(value, ';')) {
        const auto equals = entry.find('=');
        const auto separator = entry.find('|', equals == std::string::npos ? 0 : equals + 1);
        if (equals == std::string::npos || separator == std::string::npos) {
            throw std::runtime_error("tls_sni_certificates must use hostname=certificate.pem|private-key.pem entries separated by semicolons");
        }

        SniCertificateSpec spec;
        spec.pattern = trim(entry.substr(0, equals));
        spec.certificate_file = trim(entry.substr(equals + 1, separator - equals - 1));
        spec.private_key_file = trim(entry.substr(separator + 1));
        if (spec.pattern.empty() || spec.certificate_file.empty() || spec.private_key_file.empty()) {
            throw std::runtime_error("tls_sni_certificates contains an empty hostname, certificate, or private key path");
        }
        specs.push_back(std::move(spec));
    }
    return specs;
}

struct SslCtxDeleter {
    void operator()(SSL_CTX* ctx) const noexcept
    {
        if (ctx) {
            SSL_CTX_free(ctx);
        }
    }
};

class TlsContext {
public:
    explicit TlsContext(const ServerConfig& config)
        : alpn_protocols_(build_alpn_wire_list(config))
        , sni_hosts_(split_csv(config.tls_sni_hosts))
        , sni_required_(config.tls_sni_required)
        , tls_min_version_(tls_version_number(config.tls_min_version))
        , tls_max_version_(tls_version_number(config.tls_max_version))
        , tls_cipher_list_(config.tls_cipher_list)
        , tls_ciphersuites_(config.tls_ciphersuites)
    {
        OPENSSL_init_ssl(0, nullptr);

        ctx_ = make_ssl_context(config.tls_certificate_file, config.tls_private_key_file, "default TLS certificate");

        for (auto spec : parse_sni_certificate_specs(config.tls_sni_certificates)) {
            SniCertificate certificate;
            certificate.pattern = std::move(spec.pattern);
            certificate.ctx = make_ssl_context(spec.certificate_file, spec.private_key_file, std::string("SNI certificate for ") + certificate.pattern);
            sni_certificates_.push_back(std::move(certificate));
        }
    }

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    ~TlsContext() = default;

    SSL_CTX* get() const noexcept
    {
        return ctx_.get();
    }

private:
    using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;

    struct SniCertificate {
        std::string pattern;
        SslCtxPtr ctx;
    };

    SslCtxPtr make_ssl_context(
        const std::filesystem::path& certificate_file,
        const std::filesystem::path& private_key_file,
        const std::string& label)
    {
        SslCtxPtr ctx(SSL_CTX_new(TLS_server_method()));
        if (!ctx) {
            throw std::runtime_error("SSL_CTX_new failed for " + label + ": " + openssl_error());
        }

        SSL_CTX_set_options(ctx.get(), SSL_OP_NO_COMPRESSION | SSL_OP_CIPHER_SERVER_PREFERENCE);
        if (SSL_CTX_set_min_proto_version(ctx.get(), tls_min_version_) != 1) {
            throw std::runtime_error("failed to set TLS minimum version for " + label + ": " + openssl_error());
        }
        if (SSL_CTX_set_max_proto_version(ctx.get(), tls_max_version_) != 1) {
            throw std::runtime_error("failed to set TLS maximum version for " + label + ": " + openssl_error());
        }

        if (SSL_CTX_set_cipher_list(ctx.get(), tls_cipher_list_.c_str()) != 1) {
            throw std::runtime_error("failed to set TLS 1.2 cipher list for " + label + ": " + openssl_error());
        }

        if (SSL_CTX_set_ciphersuites(ctx.get(), tls_ciphersuites_.c_str()) != 1) {
            throw std::runtime_error("failed to set TLS 1.3 cipher suites for " + label + ": " + openssl_error());
        }

        if (SSL_CTX_use_certificate_file(ctx.get(), certificate_file.string().c_str(), SSL_FILETYPE_PEM) != 1) {
            throw std::runtime_error("failed to load " + label + ": " + openssl_error());
        }

        if (SSL_CTX_use_PrivateKey_file(ctx.get(), private_key_file.string().c_str(), SSL_FILETYPE_PEM) != 1) {
            throw std::runtime_error("failed to load private key for " + label + ": " + openssl_error());
        }

        if (SSL_CTX_check_private_key(ctx.get()) != 1) {
            throw std::runtime_error("TLS private key does not match " + label + ": " + openssl_error());
        }

        SSL_CTX_set_alpn_select_cb(ctx.get(), &TlsContext::select_alpn, this);
        SSL_CTX_set_tlsext_servername_callback(ctx.get(), &TlsContext::servername_callback);
        SSL_CTX_set_tlsext_servername_arg(ctx.get(), this);
        return ctx;
    }

    static int select_alpn(
        SSL*,
        const unsigned char** out,
        unsigned char* out_length,
        const unsigned char* client_protocols,
        unsigned int client_protocols_length,
        void* arg)
    {
        const auto* self = static_cast<const TlsContext*>(arg);
        if (!self || self->alpn_protocols_.empty()) {
            return SSL_TLSEXT_ERR_NOACK;
        }

        unsigned char* selected = nullptr;
        unsigned char selected_length = 0;
        const int result = SSL_select_next_proto(
            &selected,
            &selected_length,
            self->alpn_protocols_.data(),
            static_cast<unsigned int>(self->alpn_protocols_.size()),
            client_protocols,
            client_protocols_length);
        if (result != OPENSSL_NPN_NEGOTIATED) {
            return SSL_TLSEXT_ERR_NOACK;
        }

        *out = selected;
        *out_length = selected_length;
        return SSL_TLSEXT_ERR_OK;
    }

    static int servername_callback(SSL* ssl, int* alert, void* arg)
    {
        const auto* self = static_cast<const TlsContext*>(arg);
        if (!self) {
            return SSL_TLSEXT_ERR_NOACK;
        }

        const char* raw_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        if (!raw_name || std::string(raw_name).empty()) {
            if (self->sni_required_) {
                *alert = SSL_AD_UNRECOGNIZED_NAME;
                return SSL_TLSEXT_ERR_ALERT_FATAL;
            }
            return SSL_TLSEXT_ERR_OK;
        }

        const std::string hostname(raw_name);
        bool matched_certificate = false;
        for (const auto& certificate : self->sni_certificates_) {
            if (hostname_matches_pattern(hostname, certificate.pattern)) {
                SSL_set_SSL_CTX(ssl, certificate.ctx.get());
                matched_certificate = true;
                break;
            }
        }

        const bool matched_host = std::any_of(self->sni_hosts_.begin(), self->sni_hosts_.end(), [&](const std::string& pattern) {
            return hostname_matches_pattern(hostname, pattern);
        });
        if (self->sni_hosts_.empty() || matched_host || matched_certificate) {
            return SSL_TLSEXT_ERR_OK;
        }

        *alert = SSL_AD_UNRECOGNIZED_NAME;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    std::vector<unsigned char> alpn_protocols_;
    std::vector<std::string> sni_hosts_;
    std::vector<SniCertificate> sni_certificates_;
    bool sni_required_ = false;
    int tls_min_version_ = TLS1_2_VERSION;
    int tls_max_version_ = TLS1_3_VERSION;
    std::string tls_cipher_list_;
    std::string tls_ciphersuites_;
    SslCtxPtr ctx_;
};

struct SslDeleter {
    void operator()(SSL* ssl) const noexcept
    {
        if (ssl) {
            SSL_free(ssl);
        }
    }
};

rimau::http::Response::SerializationOptions response_serialization_options(const ServerConfig& config)
{
    rimau::http::Response::SerializationOptions options;
    options.server_header_enabled = config.server_header_enabled;
    options.server_name = config.server_name;

    if (config.security_headers_enabled) {
        const auto add_header = [&](std::string name, const std::string& value) {
            if (!value.empty()) {
                options.default_headers[std::move(name)] = value;
            }
        };

        add_header("x-content-type-options", config.security_header_x_content_type_options);
        add_header("x-frame-options", config.security_header_x_frame_options);
        add_header("referrer-policy", config.security_header_referrer_policy);
        add_header("cross-origin-opener-policy", config.security_header_cross_origin_opener_policy);
        add_header("content-security-policy", config.security_header_content_security_policy);
        if (config.tls_enabled) {
            add_header("strict-transport-security", config.security_header_strict_transport_security);
        }
    }

    return options;
}

class BufferedResponseSink final : public rimau::http::ResponseSink {
public:
    void send(rimau::http::Response response, rimau::http::BodyMode body_mode = rimau::http::BodyMode::include) override
    {
        if (sent_) {
            return;
        }

        last_status_ = response.status;
        response_ = std::move(response);
        body_mode_ = body_mode;
        chunked_ = false;
        chunks_.clear();
        sent_ = true;
    }

    void send_chunked(rimau::http::Response response, std::vector<std::string> chunks, rimau::http::BodyMode body_mode = rimau::http::BodyMode::include) override
    {
        if (sent_) {
            return;
        }

        last_status_ = response.status;
        response_ = std::move(response);
        response_.body.clear();
        for (const auto& chunk : chunks) {
            response_.body += chunk;
        }
        chunks_ = std::move(chunks);
        body_mode_ = body_mode;
        chunked_ = true;
        sent_ = true;
    }

    bool sent() const noexcept override
    {
        return sent_;
    }

    std::string payload(const ServerConfig& config, bool keep_alive, int timeout_seconds, int remaining_requests) const
    {
        auto response = response_;
        if (keep_alive) {
            response.headers["connection"] = "keep-alive";
            response.headers["keep-alive"] = "timeout=" + std::to_string(timeout_seconds) + ", max=" + std::to_string(remaining_requests);
        } else {
            response.headers["connection"] = "close";
            response.headers.erase("keep-alive");
        }
        if (chunked_) {
            return response.to_http_chunked_string(
                chunks_,
                body_mode_ == rimau::http::BodyMode::include,
                response_serialization_options(config));
        }
        return response.to_http_string(body_mode_ == rimau::http::BodyMode::include, response_serialization_options(config));
    }

    int last_status() const noexcept
    {
        return last_status_;
    }

    const rimau::http::Response& response() const noexcept
    {
        return response_;
    }

    rimau::http::BodyMode body_mode() const noexcept
    {
        return body_mode_;
    }

private:
    bool sent_ = false;
    int last_status_ = 500;
    rimau::http::Response response_;
    bool chunked_ = false;
    std::vector<std::string> chunks_;
    rimau::http::BodyMode body_mode_ = rimau::http::BodyMode::include;
};

int descriptor_timeout_ms(int seconds)
{
    if (seconds <= 0) {
        return 1;
    }
    if (seconds > std::numeric_limits<int>::max() / 1000) {
        return std::numeric_limits<int>::max();
    }
    return seconds * 1000;
}

bool wait_descriptor(int fd, short events, int timeout_seconds)
{
    pollfd descriptor {};
    descriptor.fd = fd;
    descriptor.events = events;

    while (true) {
        const int ready = poll(&descriptor, 1, descriptor_timeout_ms(timeout_seconds));
        if (ready > 0) {
            return (descriptor.revents & (events | POLLERR | POLLHUP | POLLNVAL)) != 0;
        }
        if (ready == 0) {
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}

UniqueFd connect_websocket_proxy_tcp(const rimau::http::ReverseProxyTarget& upstream, int timeout_seconds)
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* raw_results = nullptr;
    const std::string port = std::to_string(upstream.port);
    const int rc = getaddrinfo(upstream.host.c_str(), port.c_str(), &hints, &raw_results);
    if (rc != 0) {
        throw std::runtime_error(std::string("websocket upstream address resolution failed: ") + gai_strerror(rc));
    }

    struct AddrInfoDeleter {
        void operator()(addrinfo* value) const noexcept
        {
            if (value) {
                freeaddrinfo(value);
            }
        }
    };

    std::unique_ptr<addrinfo, AddrInfoDeleter> results(raw_results);
    std::string last_error = "no upstream address";
    for (addrinfo* item = results.get(); item; item = item->ai_next) {
        UniqueFd fd(socket(item->ai_family, item->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, item->ai_protocol));
        if (!fd.valid()) {
            last_error = std::strerror(errno);
            continue;
        }

        if (connect(fd.get(), item->ai_addr, item->ai_addrlen) == 0) {
            return fd;
        }

        if (errno != EINPROGRESS) {
            last_error = std::strerror(errno);
            continue;
        }

        if (!wait_descriptor(fd.get(), POLLOUT, timeout_seconds)) {
            last_error = "connect timeout";
            continue;
        }

        int socket_error = 0;
        socklen_t socket_error_length = sizeof(socket_error);
        if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) < 0) {
            last_error = std::strerror(errno);
            continue;
        }
        if (socket_error != 0) {
            last_error = std::strerror(socket_error);
            continue;
        }

        return fd;
    }

    throw std::runtime_error("websocket upstream connect failed: " + last_error);
}

bool should_send_upstream_sni(std::string_view host)
{
    if (host.empty() || host.find(':') != std::string_view::npos) {
        return false;
    }

    return std::any_of(host.begin(), host.end(), [](unsigned char ch) {
        return std::isalpha(ch) != 0;
    });
}

short websocket_proxy_ssl_wait_event(SSL* ssl, int result)
{
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ) {
        return POLLIN;
    }
    if (error == SSL_ERROR_WANT_WRITE) {
        return POLLOUT;
    }
    return 0;
}

std::unique_ptr<SSL_CTX, SslCtxDeleter> create_websocket_upstream_tls_context(bool verify_upstream)
{
    std::unique_ptr<SSL_CTX, SslCtxDeleter> context(SSL_CTX_new(TLS_client_method()));
    if (!context) {
        throw std::runtime_error("cannot create websocket upstream TLS context");
    }

    SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION);
    SSL_CTX_set_options(context.get(), SSL_OP_NO_COMPRESSION | SSL_OP_IGNORE_UNEXPECTED_EOF);
    if (verify_upstream) {
        SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(context.get()) != 1) {
            throw std::runtime_error("cannot load default websocket upstream TLS verify paths");
        }
    } else {
        SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
    }
    return context;
}

void configure_websocket_upstream_identity_verification(SSL* ssl, const rimau::http::ReverseProxyTarget& upstream)
{
    if (upstream.host.find(':') != std::string::npos || upstream.host.find_first_not_of("0123456789.") == std::string::npos) {
        if (SSL_set1_ipaddr(ssl, upstream.host.c_str()) != 1) {
            throw std::runtime_error("cannot set websocket upstream TLS IP verification target");
        }
    } else if (SSL_set1_dnsname(ssl, upstream.host.c_str()) != 1) {
        throw std::runtime_error("cannot set websocket upstream TLS hostname verification target");
    }
}

bool header_value_contains_control(std::string_view value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch != '\t' && (ch < 0x20 || ch == 0x7f);
    });
}

bool csv_value_has_token(std::string_view value, std::string_view token)
{
    const std::string expected = lowercase(std::string(token));
    for (auto item : split_csv(value)) {
        if (lowercase(std::move(item)) == expected) {
            return true;
        }
    }
    return false;
}

bool websocket_proxy_response_header_allowed(const std::string& name)
{
    return name != "content-length"
        && name != "transfer-encoding"
        && name != "keep-alive"
        && name != "proxy-authenticate"
        && name != "proxy-authorization"
        && name != "te"
        && name != "trailer";
}

rimau::http::Response parse_websocket_proxy_handshake_headers(std::string_view raw_headers)
{
    std::istringstream input { std::string(raw_headers) };
    std::string status_line;
    if (!std::getline(input, status_line)) {
        throw std::runtime_error("websocket upstream response missing status line");
    }
    if (!status_line.empty() && status_line.back() == '\r') {
        status_line.pop_back();
    }

    std::istringstream status_input(status_line);
    std::string version;
    int status_code = 0;
    if (!(status_input >> version >> status_code) || !version.starts_with("HTTP/")) {
        throw std::runtime_error("websocket upstream response status line is invalid");
    }
    if (status_code != 101) {
        throw std::runtime_error("websocket upstream did not switch protocols");
    }

    rimau::http::Response response;
    response.status = 101;
    response.reason = rimau::http::reason_phrase(101);

    bool saw_upgrade = false;
    bool saw_connection_upgrade = false;
    bool saw_accept = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }

        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            throw std::runtime_error("websocket upstream response header is invalid");
        }

        std::string name = lowercase(trim(line.substr(0, separator)));
        std::string value = trim(line.substr(separator + 1));
        if (!valid_header_name(name) || header_value_contains_control(value)) {
            throw std::runtime_error("websocket upstream response header contains invalid bytes");
        }

        if (name == "upgrade") {
            saw_upgrade = lowercase(value) == "websocket";
        } else if (name == "connection") {
            saw_connection_upgrade = csv_value_has_token(value, "upgrade");
        } else if (name == "sec-websocket-accept") {
            saw_accept = !value.empty();
        }

        if (websocket_proxy_response_header_allowed(name)) {
            response.headers[std::move(name)] = std::move(value);
        }
    }

    if (!saw_upgrade || !saw_connection_upgrade || !saw_accept) {
        throw std::runtime_error("websocket upstream response missing required upgrade headers");
    }

    response.headers["upgrade"] = "websocket";
    response.headers["connection"] = "Upgrade";
    return response;
}

std::string build_websocket_proxy_request(
    const rimau::http::Request& request,
    const rimau::http::ReverseProxyTarget& upstream,
    std::string_view client_ip,
    bool client_tls)
{
    std::ostringstream output;
    output << request.method << ' ' << rimau::http::reverse_proxy_target_path(upstream, request.target) << " HTTP/1.1\r\n";
    output << "host: " << upstream.authority << "\r\n";
    output << "connection: Upgrade\r\n";
    output << "upgrade: websocket\r\n";
    output << "x-forwarded-for: " << client_ip << "\r\n";
    output << "x-forwarded-proto: " << (client_tls ? "https" : "http") << "\r\n";

    if (const auto original_host = request.header("host")) {
        output << "x-forwarded-host: " << *original_host << "\r\n";
    }

    for (const auto& [name, value] : request.headers) {
        const std::string normalized_name = lowercase(name);
        if (normalized_name == "host"
            || normalized_name == "connection"
            || normalized_name == "upgrade"
            || normalized_name == "content-length"
            || normalized_name == "transfer-encoding"
            || normalized_name == "keep-alive"
            || normalized_name == "proxy-authenticate"
            || normalized_name == "proxy-authorization"
            || normalized_name == "te"
            || normalized_name == "trailer") {
            continue;
        }
        output << normalized_name << ": " << value << "\r\n";
    }

    output << "\r\n";
    return output.str();
}

std::string bytes_to_string(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty()) {
        return {};
    }
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

std::vector<std::uint8_t> http2_u32_payload(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>((value >> 24) & 0xffU),
        static_cast<std::uint8_t>((value >> 16) & 0xffU),
        static_cast<std::uint8_t>((value >> 8) & 0xffU),
        static_cast<std::uint8_t>(value & 0xffU),
    };
}

std::string http2_goaway_frame(std::uint32_t error_code, std::string_view debug_data)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(8 + debug_data.size());
    const auto last_stream_id = http2_u32_payload(0);
    const auto error = http2_u32_payload(error_code);
    payload.insert(payload.end(), last_stream_id.begin(), last_stream_id.end());
    payload.insert(payload.end(), error.begin(), error.end());
    payload.insert(payload.end(), debug_data.begin(), debug_data.end());

    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::goaway);
    frame.stream_id = 0;
    frame.payload = std::move(payload);
    return bytes_to_string(rimau::protocol::http2::serialize_frame(frame));
}

std::string http2_settings_frame(bool ack)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::settings);
    frame.flags = ack ? 0x1 : 0x0;
    frame.stream_id = 0;
    return bytes_to_string(rimau::protocol::http2::serialize_frame(frame));
}

constexpr std::uint8_t http2_flag_end_stream = 0x1;
constexpr std::uint8_t http2_flag_end_headers = 0x4;
constexpr std::uint8_t http2_flag_padded = 0x8;
constexpr std::uint8_t http2_flag_priority = 0x20;

constexpr std::uint32_t http2_no_error = 0x0;
constexpr std::uint32_t http2_protocol_error = 0x1;
constexpr std::uint32_t http2_internal_error = 0x2;
constexpr std::uint32_t http2_stream_closed = 0x5;
constexpr std::uint32_t http2_compression_error = 0x9;
constexpr std::uint32_t http2_enhance_your_calm = 0xb;

std::string http2_frame_string(rimau::protocol::http2::Frame frame)
{
    return bytes_to_string(rimau::protocol::http2::serialize_frame(frame));
}

std::string http2_rst_stream_frame(std::uint32_t stream_id, std::uint32_t error_code)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::rst_stream);
    frame.stream_id = stream_id;
    frame.payload = http2_u32_payload(error_code);
    return http2_frame_string(std::move(frame));
}

std::string http2_ping_frame(std::vector<std::uint8_t> payload, bool ack)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::ping);
    frame.flags = ack ? 0x1 : 0x0;
    frame.stream_id = 0;
    frame.payload = std::move(payload);
    return http2_frame_string(std::move(frame));
}

std::string http2_headers_frame(std::uint32_t stream_id, const std::vector<rimau::protocol::http2::HeaderField>& headers, bool end_stream)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::headers);
    frame.flags = http2_flag_end_headers | (end_stream ? http2_flag_end_stream : 0);
    frame.stream_id = stream_id;
    frame.payload = rimau::protocol::http2::hpack_encode_header_block(headers);
    return http2_frame_string(std::move(frame));
}

std::string http2_data_frame(std::uint32_t stream_id, std::string_view payload, bool end_stream)
{
    rimau::protocol::http2::Frame frame;
    frame.type = static_cast<std::uint8_t>(rimau::protocol::http2::FrameType::data);
    frame.flags = end_stream ? http2_flag_end_stream : 0;
    frame.stream_id = stream_id;
    frame.payload.assign(payload.begin(), payload.end());
    return http2_frame_string(std::move(frame));
}

bool http2_header_name_has_uppercase(std::string_view name)
{
    return std::any_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isupper(ch) != 0;
    });
}

bool http2_header_value_has_invalid_control(std::string_view value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch != '\t' && (ch < 0x20 || ch == 0x7f);
    });
}

bool http2_forbidden_request_header(std::string_view name, std::string_view value)
{
    if (name == "te") {
        return lowercase(std::string(value)) != "trailers";
    }

    return name == "connection"
        || name == "keep-alive"
        || name == "proxy-connection"
        || name == "transfer-encoding"
        || name == "upgrade";
}

bool http2_forbidden_response_header(std::string_view name)
{
    return name == "connection"
        || name == "keep-alive"
        || name == "proxy-authenticate"
        || name == "proxy-authorization"
        || name == "te"
        || name == "trailer"
        || name == "transfer-encoding"
        || name == "upgrade";
}

std::string sanitize_http2_header_value(std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch == '\r' || ch == '\n') {
            sanitized.push_back(' ');
        } else if (ch == '\t' || ch >= 0x20) {
            sanitized.push_back(static_cast<char>(ch));
        }
    }
    return sanitized;
}

std::optional<std::vector<std::uint8_t>> http2_header_block_payload(const rimau::protocol::http2::Frame& frame)
{
    std::size_t cursor = 0;
    std::size_t end = frame.payload.size();
    std::uint8_t padding = 0;

    if ((frame.flags & http2_flag_padded) != 0) {
        if (frame.payload.empty()) {
            return std::nullopt;
        }
        padding = frame.payload[cursor++];
        if (padding > frame.payload.size() - cursor) {
            return std::nullopt;
        }
        end -= padding;
    }

    if ((frame.flags & http2_flag_priority) != 0) {
        if (end - cursor < 5) {
            return std::nullopt;
        }
        cursor += 5;
    }

    if (cursor > end) {
        return std::nullopt;
    }

    return std::vector<std::uint8_t>(frame.payload.begin() + static_cast<std::ptrdiff_t>(cursor), frame.payload.begin() + static_cast<std::ptrdiff_t>(end));
}

std::optional<std::vector<std::uint8_t>> http2_data_payload(const rimau::protocol::http2::Frame& frame)
{
    std::size_t cursor = 0;
    std::size_t end = frame.payload.size();
    if ((frame.flags & http2_flag_padded) != 0) {
        if (frame.payload.empty()) {
            return std::nullopt;
        }
        const auto padding = frame.payload[cursor++];
        if (padding > frame.payload.size() - cursor) {
            return std::nullopt;
        }
        end -= padding;
    }

    if (cursor > end) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(frame.payload.begin() + static_cast<std::ptrdiff_t>(cursor), frame.payload.begin() + static_cast<std::ptrdiff_t>(end));
}

rimau::http::Response response_with_protocol_defaults(
    const rimau::http::Response& input,
    const ServerConfig& config)
{
    auto response = input;
    const auto options = response_serialization_options(config);
    if (response.status != 101) {
        for (const auto& [name, value] : options.default_headers) {
            if (!response.headers.contains(name)) {
                response.headers[name] = value;
            }
        }
    }

    if (response.status != 101 && response.status != 204 && !response.headers.contains("transfer-encoding")) {
        response.headers["content-length"] = std::to_string(response.body.size());
    }

    if (options.server_header_enabled) {
        response.headers["server"] = options.server_name.empty() ? "Rimau Web Server" : options.server_name;
    } else {
        response.headers.erase("server");
    }
    return response;
}

bool http2_response_may_have_body(int status)
{
    return status >= 200 && status != 204 && status != 304;
}

std::string serialize_http2_response(
    std::uint32_t stream_id,
    const rimau::http::Response& input,
    rimau::http::BodyMode body_mode,
    const ServerConfig& config)
{
    const auto response = response_with_protocol_defaults(input, config);
    const bool include_body = body_mode == rimau::http::BodyMode::include && http2_response_may_have_body(response.status);

    std::vector<rimau::protocol::http2::HeaderField> headers;
    headers.push_back({ ":status", std::to_string(response.status) });
    for (const auto& [raw_name, raw_value] : response.headers) {
        std::string name = lowercase(raw_name);
        if (!valid_header_name(name) || http2_forbidden_response_header(name)) {
            continue;
        }
        headers.push_back({ std::move(name), sanitize_http2_header_value(raw_value) });
    }

    std::string output;
    output += http2_headers_frame(stream_id, headers, !include_body || response.body.empty());
    if (!include_body || response.body.empty()) {
        return output;
    }

    constexpr std::size_t max_data_payload = rimau::protocol::http2::default_max_frame_size;
    for (std::size_t offset = 0; offset < response.body.size();) {
        const auto remaining = response.body.size() - offset;
        const auto chunk_size = std::min<std::size_t>(remaining, max_data_payload);
        const bool end_stream = offset + chunk_size == response.body.size();
        output += http2_data_frame(stream_id, std::string_view(response.body).substr(offset, chunk_size), end_stream);
        offset += chunk_size;
    }
    return output;
}

void write_websocket_proxy_plain(int fd, std::string_view payload, int timeout_seconds)
{
    std::size_t sent_total = 0;
    while (sent_total < payload.size()) {
        const ssize_t sent = send(fd, payload.data() + sent_total, payload.size() - sent_total, MSG_NOSIGNAL);
        if (sent > 0) {
            sent_total += static_cast<std::size_t>(sent);
            continue;
        }

        if (sent < 0 && would_block()) {
            if (!wait_descriptor(fd, POLLOUT, timeout_seconds)) {
                throw std::runtime_error("websocket upstream write timeout");
            }
            continue;
        }

        throw std::runtime_error(std::string("websocket upstream write failed: ") + std::strerror(errno));
    }
}

void write_websocket_proxy_tls(SSL* ssl, int fd, std::string_view payload, int timeout_seconds)
{
    std::size_t sent_total = 0;
    while (sent_total < payload.size()) {
        const auto remaining = payload.size() - sent_total;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int sent = SSL_write(ssl, payload.data() + sent_total, chunk);
        if (sent > 0) {
            sent_total += static_cast<std::size_t>(sent);
            continue;
        }

        const short event = websocket_proxy_ssl_wait_event(ssl, sent);
        if (event != 0 && wait_descriptor(fd, event, timeout_seconds)) {
            continue;
        }

        throw std::runtime_error("websocket upstream TLS write failed");
    }
}

struct WebSocketProxyHandshakeBytes {
    std::string headers;
    std::string extra;
};

WebSocketProxyHandshakeBytes split_websocket_proxy_handshake_buffer(std::string buffer)
{
    const auto header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("websocket upstream response missing header terminator");
    }

    WebSocketProxyHandshakeBytes result;
    result.headers = buffer.substr(0, header_end + 4);
    result.extra = buffer.substr(header_end + 4);
    return result;
}

WebSocketProxyHandshakeBytes read_websocket_proxy_plain_handshake(int fd, int timeout_seconds)
{
    constexpr std::size_t max_handshake_bytes = 64 * 1024;
    std::string buffer;
    std::array<char, 4096> chunk {};

    while (buffer.find("\r\n\r\n") == std::string::npos) {
        const ssize_t received = recv(fd, chunk.data(), chunk.size(), 0);
        if (received > 0) {
            buffer.append(chunk.data(), static_cast<std::size_t>(received));
            if (buffer.size() > max_handshake_bytes) {
                throw std::runtime_error("websocket upstream handshake too large");
            }
            continue;
        }

        if (received == 0) {
            throw std::runtime_error("websocket upstream closed during handshake");
        }

        if (would_block()) {
            if (!wait_descriptor(fd, POLLIN, timeout_seconds)) {
                throw std::runtime_error("websocket upstream read timeout");
            }
            continue;
        }

        throw std::runtime_error(std::string("websocket upstream read failed: ") + std::strerror(errno));
    }

    return split_websocket_proxy_handshake_buffer(std::move(buffer));
}

WebSocketProxyHandshakeBytes read_websocket_proxy_tls_handshake(SSL* ssl, int fd, int timeout_seconds)
{
    constexpr std::size_t max_handshake_bytes = 64 * 1024;
    std::string buffer;
    std::array<char, 4096> chunk {};

    while (buffer.find("\r\n\r\n") == std::string::npos) {
        const int received = SSL_read(ssl, chunk.data(), static_cast<int>(chunk.size()));
        if (received > 0) {
            buffer.append(chunk.data(), static_cast<std::size_t>(received));
            if (buffer.size() > max_handshake_bytes) {
                throw std::runtime_error("websocket upstream TLS handshake too large");
            }
            continue;
        }

        const int error = SSL_get_error(ssl, received);
        if (error == SSL_ERROR_ZERO_RETURN) {
            throw std::runtime_error("websocket upstream TLS closed during handshake");
        }

        const short event = error == SSL_ERROR_WANT_READ ? POLLIN : (error == SSL_ERROR_WANT_WRITE ? POLLOUT : 0);
        if (event != 0 && wait_descriptor(fd, event, timeout_seconds)) {
            continue;
        }

        throw std::runtime_error("websocket upstream TLS read failed");
    }

    return split_websocket_proxy_handshake_buffer(std::move(buffer));
}

struct WebSocketProxyUpstream {
    UniqueFd fd;
    std::unique_ptr<SSL_CTX, SslCtxDeleter> tls_context;
    std::unique_ptr<SSL, SslDeleter> tls;
    rimau::http::Response handshake_response;
    std::string pending_bytes;
};

void connect_websocket_proxy_tls(
    WebSocketProxyUpstream& connection,
    const rimau::http::ReverseProxyTarget& upstream,
    int timeout_seconds,
    bool verify_upstream)
{
    connection.tls_context = create_websocket_upstream_tls_context(verify_upstream);
    connection.tls.reset(SSL_new(connection.tls_context.get()));
    if (!connection.tls) {
        throw std::runtime_error("cannot create websocket upstream TLS connection");
    }
    if (SSL_set_fd(connection.tls.get(), connection.fd.get()) != 1) {
        throw std::runtime_error("cannot attach websocket upstream TLS socket");
    }
    if (should_send_upstream_sni(upstream.host)) {
        SSL_set_tlsext_host_name(connection.tls.get(), upstream.host.c_str());
    }
    if (verify_upstream) {
        configure_websocket_upstream_identity_verification(connection.tls.get(), upstream);
    }
    SSL_set_connect_state(connection.tls.get());

    while (true) {
        const int result = SSL_connect(connection.tls.get());
        if (result == 1) {
            if (verify_upstream && SSL_get_verify_result(connection.tls.get()) != X509_V_OK) {
                throw std::runtime_error("websocket upstream TLS certificate verification failed");
            }
            return;
        }

        const short event = websocket_proxy_ssl_wait_event(connection.tls.get(), result);
        if (event == 0 || !wait_descriptor(connection.fd.get(), event, timeout_seconds)) {
            throw std::runtime_error("websocket upstream TLS handshake failed");
        }
    }
}

WebSocketProxyUpstream connect_websocket_proxy_upstream(
    const rimau::http::Request& request,
    const rimau::http::ReverseProxyTarget& upstream,
    const rimau::http::ReverseProxySettings& settings,
    std::string_view client_ip,
    bool client_tls)
{
    WebSocketProxyUpstream connection;
    connection.fd = connect_websocket_proxy_tcp(upstream, settings.connect_timeout_seconds);
    if (upstream.scheme == "https") {
        connect_websocket_proxy_tls(connection, upstream, settings.connect_timeout_seconds, settings.verify_tls_upstream);
    }

    const auto upstream_request = build_websocket_proxy_request(request, upstream, client_ip, client_tls);
    if (connection.tls) {
        write_websocket_proxy_tls(connection.tls.get(), connection.fd.get(), upstream_request, settings.connect_timeout_seconds);
        auto bytes = read_websocket_proxy_tls_handshake(connection.tls.get(), connection.fd.get(), settings.read_timeout_seconds);
        connection.handshake_response = parse_websocket_proxy_handshake_headers(bytes.headers);
        connection.pending_bytes = std::move(bytes.extra);
    } else {
        write_websocket_proxy_plain(connection.fd.get(), upstream_request, settings.connect_timeout_seconds);
        auto bytes = read_websocket_proxy_plain_handshake(connection.fd.get(), settings.read_timeout_seconds);
        connection.handshake_response = parse_websocket_proxy_handshake_headers(bytes.headers);
        connection.pending_bytes = std::move(bytes.extra);
    }

    return connection;
}

std::vector<rimau::http::ReverseProxyTarget> ordered_websocket_proxy_upstreams(
    const std::vector<rimau::http::ReverseProxyTarget>& upstreams,
    std::size_t retry_count)
{
    if (upstreams.empty()) {
        return {};
    }

    const auto start = g_next_websocket_proxy_upstream.fetch_add(1, std::memory_order_relaxed);
    std::vector<rimau::http::ReverseProxyTarget> ordered;
    constexpr std::size_t max_proxy_attempts = 16;
    const auto requested_attempts = std::min(
        retry_count == std::numeric_limits<std::size_t>::max() ? retry_count : retry_count + 1,
        max_proxy_attempts);

    if (upstreams.size() == 1) {
        ordered.assign(requested_attempts, upstreams.front());
        return ordered;
    }

    const auto attempts = std::min(upstreams.size(), requested_attempts);
    ordered.reserve(attempts);
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        ordered.push_back(upstreams[(start + attempt) % upstreams.size()]);
    }
    return ordered;
}

std::optional<std::vector<rimau::http::ReverseProxyTarget>> websocket_proxy_upstreams_for_request(
    const rimau::http::Request& request,
    const ServerConfig& config)
{
    if (!config.virtual_hosts_enabled || config.virtual_hosts.empty()) {
        return std::nullopt;
    }

    const auto rules = rimau::http::parse_virtual_host_rules(config.virtual_hosts);
    const auto* rule = rimau::http::select_virtual_host_rule(request, rules);
    if (!rule || rule->action != rimau::http::VirtualHostAction::reverse_proxy) {
        return std::nullopt;
    }

    return rule->upstreams;
}

std::string json_escape(std::string_view value)
{
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (ch < 0x20 || ch == 0x7f) {
                output << "\\u00";
                constexpr char hex[] = "0123456789abcdef";
                output << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

std::string bounded_audit_value(std::string value)
{
    constexpr std::size_t max_audit_value_bytes = 256;
    if (value.size() > max_audit_value_bytes) {
        value.resize(max_audit_value_bytes);
        value += "...";
    }
    return value;
}

std::string waf_audit_log_line(
    std::size_t worker_id,
    std::string_view client_ip,
    const rimau::http::Request& request,
    const rimau::http::WafSettings& settings,
    const rimau::http::WafResult& result)
{
    const auto& match = result.matches.front();
    const auto host = request.header("host").value_or("");

    std::ostringstream output;
    output << "{\"event\":\"rimau_waf_audit\""
           << ",\"worker\":" << worker_id
           << ",\"client_ip\":\"" << json_escape(bounded_audit_value(std::string(client_ip))) << "\""
           << ",\"outcome\":\"" << (result.allowed ? "matched" : "blocked") << "\""
           << ",\"engine\":\"" << json_escape(result.engine) << "\""
           << ",\"ruleset\":\"" << json_escape(result.ruleset) << "\""
           << ",\"rule_id\":" << match.rule_id
           << ",\"severity\":\"" << json_escape(match.severity) << "\""
           << ",\"tag\":\"" << json_escape(match.tag) << "\""
           << ",\"score\":" << result.anomaly_score
           << ",\"threshold\":" << settings.anomaly_threshold
           << ",\"blocking\":" << (settings.blocking_enabled ? "true" : "false")
           << ",\"matches\":" << result.matches.size()
           << ",\"method\":\"" << json_escape(bounded_audit_value(request.method)) << "\""
           << ",\"host\":\"" << json_escape(bounded_audit_value(host)) << "\""
           << ",\"path\":\"" << json_escape(bounded_audit_value(request.path)) << "\""
           << ",\"variable\":\"" << json_escape(match.variable) << "\""
           << "}";
    return output.str();
}

class SecurityState {
public:
    enum class AcceptDecision {
        allowed,
        blocked_ip,
        not_allowlisted,
        global_limit,
        per_ip_limit
    };

    AcceptDecision try_open_connection(const std::string& ip, const ServerConfig& config)
    {
        if (ip_matches_list(ip, config.ip_blocklist)) {
            return AcceptDecision::blocked_ip;
        }

        if (!config.ip_allowlist.empty() && !ip_matches_list(ip, config.ip_allowlist)) {
            return AcceptDecision::not_allowlisted;
        }

        std::lock_guard lock(mutex_);
        if (total_connections_ >= config.global_connection_limit) {
            return AcceptDecision::global_limit;
        }

        const std::size_t ip_connections = active_connections_by_ip_[ip];
        if (ip_connections >= config.per_ip_connection_limit) {
            return AcceptDecision::per_ip_limit;
        }

        ++active_connections_by_ip_[ip];
        ++total_connections_;
        return AcceptDecision::allowed;
    }

    void close_connection(const std::string& ip)
    {
        if (ip.empty()) {
            return;
        }

        std::lock_guard lock(mutex_);
        const auto found = active_connections_by_ip_.find(ip);
        if (found != active_connections_by_ip_.end()) {
            if (found->second > 1) {
                --found->second;
            } else {
                active_connections_by_ip_.erase(found);
            }
        }
        if (total_connections_ > 0) {
            --total_connections_;
        }
    }

    bool allow_request(const std::string& ip, const ServerConfig& config)
    {
        if (!config.rate_limit_enabled || ip.empty()) {
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        auto& bucket = request_buckets_[ip];
        if (bucket.window_start.time_since_epoch().count() == 0 || now - bucket.window_start >= std::chrono::minutes(1)) {
            bucket.window_start = now;
            bucket.count = 0;
        }

        if (bucket.count >= static_cast<std::size_t>(config.rate_limit_requests_per_minute)) {
            return false;
        }

        ++bucket.count;
        return true;
    }

private:
    struct RequestBucket {
        std::chrono::steady_clock::time_point window_start;
        std::size_t count = 0;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, std::size_t> active_connections_by_ip_;
    std::unordered_map<std::string, RequestBucket> request_buckets_;
    std::size_t total_connections_ = 0;
};

enum class EventTokenType {
    listener,
    client,
    upstream
};

class ClientConnection;

struct EventToken {
    EventTokenType type;
    ClientConnection* connection = nullptr;
};

std::string request_body_spool_template()
{
    const auto sequence = g_next_request_body_spool.fetch_add(1, std::memory_order_relaxed);
    return (std::filesystem::temp_directory_path()
        / ("rimau-request-body-" + std::to_string(getpid()) + "-" + std::to_string(sequence) + "-XXXXXX"))
        .string();
}

class RequestBodyAccumulator {
public:
    explicit RequestBodyAccumulator(std::size_t memory_limit)
        : memory_limit_(memory_limit)
    {
    }

    RequestBodyAccumulator(const RequestBodyAccumulator&) = delete;
    RequestBodyAccumulator& operator=(const RequestBodyAccumulator&) = delete;

    void append(std::string_view data)
    {
        if (data.empty()) {
            return;
        }

        if (!body_file_ && memory_.size() + data.size() <= memory_limit_) {
            memory_.append(data);
            size_ += data.size();
            return;
        }

        ensure_spool_file();
        write_spool(data);
        size_ += data.size();
    }

    std::size_t size() const noexcept
    {
        return size_;
    }

    rimau::http::Request finish(rimau::http::Request request)
    {
        request.body_size_bytes = size_;
        if (body_file_) {
            spool_fd_.reset();
            request.body.clear();
            request.body_file = body_file_;
            return request;
        }

        request.body = std::move(memory_);
        return request;
    }

private:
    void ensure_spool_file()
    {
        if (body_file_) {
            return;
        }

        auto path_template = request_body_spool_template();
        std::vector<char> path_buffer(path_template.begin(), path_template.end());
        path_buffer.push_back('\0');

        const int fd = mkstemp(path_buffer.data());
        if (fd < 0) {
            throw std::runtime_error(std::string("request body spool create failed: ") + std::strerror(errno));
        }
        spool_fd_.reset(fd);
        body_file_ = std::make_shared<rimau::http::RequestBodyFile>(std::filesystem::path(path_buffer.data()));

        if (!memory_.empty()) {
            write_spool(memory_);
            memory_.clear();
            memory_.shrink_to_fit();
        }
    }

    void write_spool(std::string_view data)
    {
        const char* cursor = data.data();
        std::size_t remaining = data.size();
        while (remaining > 0) {
            const auto chunk = std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const ssize_t written = write(spool_fd_.get(), cursor, chunk);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                throw std::runtime_error(std::string("request body spool write failed: ") + std::strerror(errno));
            }
            cursor += written;
            remaining -= static_cast<std::size_t>(written);
        }
    }

    std::size_t memory_limit_;
    std::size_t size_ = 0;
    std::string memory_;
    std::shared_ptr<rimau::http::RequestBodyFile> body_file_;
    UniqueFd spool_fd_;
};

enum class ChunkedStreamPhase {
    size_line,
    data,
    data_crlf,
    trailer_line
};

struct Http1BodyStreamState {
    explicit Http1BodyStreamState(std::size_t memory_limit)
        : accumulator(memory_limit)
    {
    }

    rimau::http::Request request;
    RequestBodyAccumulator accumulator;
    bool chunked = false;
    std::size_t header_bytes = 0;
    std::size_t max_body_bytes = 0;
    std::size_t content_length = 0;
    std::size_t received = 0;
    ChunkedStreamPhase chunked_phase = ChunkedStreamPhase::size_line;
    std::string line_buffer;
    std::size_t chunk_remaining = 0;
    std::size_t crlf_seen = 0;
};

enum class ClientPhase {
    tls_handshake,
    reading,
    writing,
    websocket,
    websocket_proxy,
    tls_shutdown,
    closed
};

struct Http2StreamState {
    std::uint32_t id = 0;
    bool headers_received = false;
    std::vector<rimau::protocol::http2::HeaderField> headers;
    std::string body;
};

struct Http2RequestBuildResult {
    std::optional<rimau::http::Request> request;
    rimau::http::Response error_response;
};

class ClientConnection {
public:
    ClientConnection()
        : token_ { EventTokenType::client, this }
        , upstream_token_ { EventTokenType::upstream, this }
    {
        request_buffer_.reserve(4096);
        response_buffer_.reserve(4096);
    }

    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;

    ~ClientConnection()
    {
        prepare_for_pool();
    }

    void reset(
        int fd,
        int epoll_fd,
        std::shared_ptr<const ServerConfig> config,
        std::shared_ptr<const TlsContext> tls_context,
        std::size_t worker_id,
        std::string client_ip,
        SecurityState* security_state)
    {
        prepare_for_pool();

        fd_ = fd;
        epoll_fd_ = epoll_fd;
        config_ = std::move(config);
        tls_context_holder_ = std::move(tls_context);
        worker_id_ = worker_id;
        client_ip_ = std::move(client_ip);
        security_state_ = security_state;
        phase_ = tls_context_holder_ ? ClientPhase::tls_handshake : ClientPhase::reading;
        events_ = EPOLLIN;
        token_.connection = this;
        upstream_token_.connection = this;
        last_activity_ = std::chrono::steady_clock::now();
        request_started_at_ = {};
        body_started_at_ = {};
        waiting_for_body_ = false;

        if (tls_context_holder_) {
            SSL* raw_ssl = SSL_new(tls_context_holder_->get());
            if (!raw_ssl) {
                throw std::runtime_error("SSL_new failed: " + openssl_error());
            }

            ssl_.reset(raw_ssl);
            SSL_set_fd(ssl_.get(), fd_);
            SSL_set_accept_state(ssl_.get());
            SSL_set_mode(ssl_.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        }
    }

    void prepare_for_pool() noexcept
    {
        close_websocket_upstream();
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = -1;
        epoll_fd_ = -1;
        config_.reset();
        ssl_.reset();
        tls_context_holder_.reset();
        negotiated_alpn_.clear();
        http2_tls_selected_ = false;
        phase_ = ClientPhase::closed;
        events_ = 0;
        client_ip_.clear();
        security_state_ = nullptr;
        request_buffer_.clear();
        http1_body_stream_.reset();
        response_buffer_.clear();
        response_offset_ = 0;
        keep_alive_after_response_ = false;
        websocket_after_response_ = false;
        websocket_proxy_after_response_ = false;
        websocket_close_after_write_ = false;
        websocket_proxy_client_to_upstream_.clear();
        websocket_proxy_upstream_to_client_.clear();
        websocket_proxy_client_to_upstream_offset_ = 0;
        websocket_proxy_upstream_to_client_offset_ = 0;
        websocket_proxy_client_extra_events_ = 0;
        websocket_proxy_upstream_extra_events_ = 0;
        http2_active_ = false;
        http2_last_client_stream_id_ = 0;
        http2_streams_.clear();
        requests_served_ = 0;
        last_activity_ = {};
        request_started_at_ = {};
        body_started_at_ = {};
        waiting_for_body_ = false;
    }

    int fd() const noexcept
    {
        return fd_;
    }

    bool closed() const noexcept
    {
        return phase_ == ClientPhase::closed;
    }

    std::uint32_t events() const noexcept
    {
        return events_;
    }

    EventToken* token() noexcept
    {
        return &token_;
    }

    EventToken* upstream_token() noexcept
    {
        return &upstream_token_;
    }

    const std::string& client_ip() const noexcept
    {
        return client_ip_;
    }

    void update_config(std::shared_ptr<const ServerConfig> config)
    {
        if (config) {
            config_ = std::move(config);
        }
    }

    bool expire_if_timed_out(std::chrono::steady_clock::time_point now)
    {
        if (!config_ || closed()) {
            return false;
        }

        if (phase_ == ClientPhase::tls_handshake && now - last_activity_ >= std::chrono::seconds(config_->request_timeout_seconds)) {
            close_now();
            return true;
        }

        if (phase_ == ClientPhase::websocket && now - last_activity_ >= std::chrono::seconds(config_->idle_timeout_seconds)) {
            close_now();
            return true;
        }

        if (phase_ == ClientPhase::websocket_proxy && now - last_activity_ >= std::chrono::seconds(config_->idle_timeout_seconds)) {
            close_now();
            return true;
        }

        if (phase_ != ClientPhase::reading) {
            return false;
        }

        if (http2_active_) {
            if (request_buffer_.empty()) {
                if (now - last_activity_ >= std::chrono::seconds(config_->idle_timeout_seconds)) {
                    close_now();
                    return true;
                }
                return false;
            }

            if (request_started_at_.time_since_epoch().count() != 0
                && now - request_started_at_ >= std::chrono::seconds(config_->request_timeout_seconds)) {
                set_serialized_response(http2_goaway_frame(http2_enhance_your_calm, "HTTP/2 request timeout"), false);
                return true;
            }

            return false;
        }

        if (waiting_for_body_ && body_started_at_.time_since_epoch().count() != 0
            && now - body_started_at_ >= std::chrono::seconds(config_->body_timeout_seconds)) {
            set_response(rimau::http::text_response(408, "Request Timeout\n"), rimau::http::BodyMode::include, false);
            return true;
        }

        if (requests_served_ > 0 && request_buffer_.empty()) {
            if (!config_->http_keep_alive_enabled || now - last_activity_ >= std::chrono::seconds(config_->idle_timeout_seconds)) {
                close_now();
                return true;
            }
            return false;
        }

        if (request_buffer_.empty() || request_started_at_.time_since_epoch().count() == 0) {
            return false;
        }

        if (now - request_started_at_ >= std::chrono::seconds(config_->request_timeout_seconds)) {
            set_response(rimau::http::text_response(408, "Request Timeout\n"), rimau::http::BodyMode::include, false);
            return true;
        }

        if (!waiting_for_body_ && now - request_started_at_ >= std::chrono::seconds(config_->header_timeout_seconds)) {
            set_response(rimau::http::text_response(408, "Request Timeout\n"), rimau::http::BodyMode::include, false);
            return true;
        }

        return false;
    }

    void handle(std::uint32_t revents)
    {
        if (closed()) {
            return;
        }

        if ((revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0 && (revents & (EPOLLIN | EPOLLOUT)) == 0) {
            close_now();
            return;
        }

        switch (phase_) {
        case ClientPhase::tls_handshake:
            do_tls_handshake();
            break;
        case ClientPhase::reading:
            if (revents & (EPOLLIN | EPOLLOUT | EPOLLRDHUP)) {
                read_request();
            }
            break;
        case ClientPhase::writing:
            if (revents & (EPOLLIN | EPOLLOUT | EPOLLRDHUP)) {
                write_response();
            }
            break;
        case ClientPhase::websocket:
            handle_websocket(revents);
            break;
        case ClientPhase::websocket_proxy:
            handle_websocket_proxy_client(revents);
            break;
        case ClientPhase::tls_shutdown:
            do_tls_shutdown();
            break;
        case ClientPhase::closed:
            break;
        }
    }

    void handle_upstream(std::uint32_t revents)
    {
        if (closed() || phase_ != ClientPhase::websocket_proxy) {
            return;
        }

        if ((revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0 && (revents & (EPOLLIN | EPOLLOUT)) == 0) {
            close_now();
            return;
        }

        if (revents & EPOLLIN) {
            read_websocket_proxy_upstream();
        }
        if (!closed() && (!websocket_proxy_client_to_upstream_.empty() || (revents & EPOLLOUT) != 0)) {
            flush_websocket_proxy_upstream();
        }
        if (!closed()) {
            update_websocket_proxy_events();
        }
    }

private:
    void close_websocket_upstream() noexcept
    {
        if (websocket_upstream_fd_ >= 0) {
            if (epoll_fd_ >= 0 && websocket_upstream_registered_) {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, websocket_upstream_fd_, nullptr);
            }
            close(websocket_upstream_fd_);
        }
        websocket_upstream_fd_ = -1;
        websocket_upstream_registered_ = false;
        websocket_upstream_tls_.reset();
        websocket_upstream_tls_context_.reset();
    }

    void close_now() noexcept
    {
        close_websocket_upstream();
        phase_ = ClientPhase::closed;
        events_ = 0;
    }

    void set_response(rimau::http::Response response, rimau::http::BodyMode body_mode, bool keep_alive)
    {
        keep_alive_after_response_ = keep_alive;
        if (keep_alive) {
            const int remaining_requests = std::max(0, config_->http_keep_alive_max_requests - requests_served_);
            response.headers["connection"] = "keep-alive";
            response.headers["keep-alive"] = "timeout=" + std::to_string(config_->http_keep_alive_timeout_seconds)
                + ", max=" + std::to_string(remaining_requests);
        } else {
            response.headers["connection"] = "close";
            response.headers.erase("keep-alive");
        }

        response_buffer_ = response.to_http_string(body_mode == rimau::http::BodyMode::include, response_serialization_options(*config_));
        response_offset_ = 0;
        phase_ = ClientPhase::writing;
        events_ = EPOLLOUT;
    }

    void set_serialized_response(std::string payload, bool keep_alive)
    {
        keep_alive_after_response_ = keep_alive;
        response_buffer_ = std::move(payload);
        response_offset_ = 0;
        phase_ = ClientPhase::writing;
        events_ = EPOLLOUT;
    }

    void set_websocket_handshake_response(rimau::http::Response response)
    {
        websocket_after_response_ = true;
        keep_alive_after_response_ = false;
        response_buffer_ = response.to_http_string(true, response_serialization_options(*config_));
        response_offset_ = 0;
        phase_ = ClientPhase::writing;
        events_ = EPOLLOUT;
    }

    void set_websocket_proxy_handshake_response(rimau::http::Response response)
    {
        websocket_proxy_after_response_ = true;
        keep_alive_after_response_ = false;
        response_buffer_ = response.to_http_string(true, response_serialization_options(*config_));
        response_offset_ = 0;
        phase_ = ClientPhase::writing;
        events_ = EPOLLOUT;
    }

    void do_tls_handshake()
    {
        const int result = SSL_accept(ssl_.get());
        if (result == 1) {
            negotiated_alpn_ = selected_alpn_protocol(ssl_.get());
            http2_tls_selected_ = negotiated_alpn_ == "h2";
            if (http2_tls_selected_) {
                log(LogLevel::info, "worker " + std::to_string(worker_id_) + " negotiated TLS ALPN h2");
            }
            phase_ = ClientPhase::reading;
            events_ = EPOLLIN;
            return;
        }

        const int error = SSL_get_error(ssl_.get(), result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            events_ = event_for_ssl_error(error);
            return;
        }

        log(LogLevel::warning, "TLS handshake failed: " + openssl_error());
        close_now();
    }

    void read_request()
    {
        if (ssl_) {
            read_tls_request();
        } else {
            read_plain_request();
        }
    }

    void read_plain_request()
    {
        char chunk[4096];
        while (true) {
            const ssize_t received = recv(fd_, chunk, sizeof(chunk), 0);
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                append_request_bytes(chunk, static_cast<std::size_t>(received));
                if (phase_ != ClientPhase::reading) {
                    return;
                }
                continue;
            }

            if (received == 0) {
                close_now();
                return;
            }

            if (would_block()) {
                events_ = EPOLLIN;
                return;
            }

            log(LogLevel::warning, std::string("recv failed: ") + std::strerror(errno));
            close_now();
            return;
        }
    }

    void read_tls_request()
    {
        char chunk[4096];
        while (true) {
            const int received = SSL_read(ssl_.get(), chunk, static_cast<int>(sizeof(chunk)));
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                append_request_bytes(chunk, static_cast<std::size_t>(received));
                if (phase_ != ClientPhase::reading) {
                    return;
                }
                continue;
            }

            const int error = SSL_get_error(ssl_.get(), received);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                events_ = event_for_ssl_error(error);
                return;
            }
            if (error == SSL_ERROR_ZERO_RETURN) {
                close_now();
                return;
            }

            log(LogLevel::warning, "TLS read failed: " + openssl_error());
            close_now();
            return;
        }
    }

    void append_request_bytes(const char* data, std::size_t size)
    {
        if (request_buffer_.empty()) {
            request_started_at_ = std::chrono::steady_clock::now();
            body_started_at_ = {};
            waiting_for_body_ = false;
        }
        request_buffer_.append(data, size);
        if (http1_body_stream_) {
            process_http1_body_stream();
            return;
        }
        process_buffered_request();
    }

    void process_buffered_request()
    {
        if (!config_ || phase_ != ClientPhase::reading) {
            return;
        }

        if (http2_active_) {
            process_http2_frames();
            return;
        }

        if (process_http2_prior_knowledge()) {
            return;
        }

        if (http2_tls_selected_) {
            request_buffer_.clear();
            set_serialized_response(http2_goaway_frame(http2_protocol_error, "HTTP/2 TLS connection expected client connection preface"), false);
            return;
        }

        if (!config_->http1_enabled) {
            request_buffer_.clear();
            set_response(rimau::http::text_response(400, "HTTP/1.1 is disabled in Rimau SQLite config\n"), rimau::http::BodyMode::include, false);
            return;
        }

        const auto frame = rimau::http::next_http1_request_frame(
            request_buffer_,
            rimau::http::Http1FrameOptions { config_->max_request_bytes });

        if (frame.state == rimau::http::Http1FrameState::incomplete) {
            waiting_for_body_ = frame.waiting_for_body;
            return;
        }

        if (frame.state == rimau::http::Http1FrameState::error) {
            if (frame.discard_buffer) {
                request_buffer_.clear();
            } else if (frame.consumed > 0) {
                request_buffer_.erase(0, std::min(request_buffer_.size(), frame.consumed));
            }
            set_response(rimau::http::text_response(frame.error_status, frame.error_message), rimau::http::BodyMode::include, false);
            return;
        }

        if (frame.request && try_handle_websocket_upgrade(*frame.request, frame.consumed)) {
            return;
        }

        if (frame.state == rimau::http::Http1FrameState::header_complete) {
            if (frame.request && frame.waiting_for_body) {
                start_http1_body_stream(*frame.request, frame);
                process_http1_body_stream();
                return;
            }
            waiting_for_body_ = frame.waiting_for_body;
            if (waiting_for_body_ && body_started_at_.time_since_epoch().count() == 0) {
                body_started_at_ = std::chrono::steady_clock::now();
            }
            return;
        }

        request_buffer_.erase(0, frame.consumed);
        waiting_for_body_ = false;
        body_started_at_ = {};
        request_started_at_ = request_buffer_.empty() ? std::chrono::steady_clock::time_point {} : std::chrono::steady_clock::now();
        process_request(std::move(frame.raw_request));
    }

    void start_http1_body_stream(rimau::http::Request request, const rimau::http::Http1FrameResult& frame)
    {
        static constexpr std::size_t memory_body_limit = 16 * 1024;

        http1_body_stream_ = std::make_unique<Http1BodyStreamState>(memory_body_limit);
        auto& stream = *http1_body_stream_;
        stream.request = std::move(request);
        stream.chunked = frame.chunked;
        stream.header_bytes = frame.consumed;
        stream.max_body_bytes = config_->max_request_bytes > frame.consumed ? config_->max_request_bytes - frame.consumed : 0;
        stream.content_length = frame.content_length.value_or(0);
        request_buffer_.erase(0, std::min(request_buffer_.size(), frame.consumed));
        waiting_for_body_ = true;
        if (body_started_at_.time_since_epoch().count() == 0) {
            body_started_at_ = std::chrono::steady_clock::now();
        }
    }

    void process_http1_body_stream()
    {
        if (!http1_body_stream_) {
            return;
        }

        try {
            if (http1_body_stream_->chunked) {
                process_chunked_http1_body_stream();
            } else {
                process_content_length_http1_body_stream();
            }
        } catch (const std::exception& error) {
            log(LogLevel::warning, std::string("HTTP/1.1 request body stream failed: ") + error.what());
            http1_body_stream_.reset();
            request_buffer_.clear();
            set_response(rimau::http::text_response(500, "Internal Server Error\n"), rimau::http::BodyMode::include, false);
        }
    }

    void process_content_length_http1_body_stream()
    {
        auto& stream = *http1_body_stream_;
        while (!request_buffer_.empty() && stream.received < stream.content_length) {
            const auto take = std::min(stream.content_length - stream.received, request_buffer_.size());
            stream.accumulator.append(std::string_view(request_buffer_).substr(0, take));
            request_buffer_.erase(0, take);
            stream.received += take;
        }

        if (stream.received == stream.content_length) {
            finish_http1_body_stream();
        }
    }

    void process_chunked_http1_body_stream()
    {
        auto& stream = *http1_body_stream_;
        while (!request_buffer_.empty()) {
            if (stream.chunked_phase == ChunkedStreamPhase::size_line || stream.chunked_phase == ChunkedStreamPhase::trailer_line) {
                const auto line_end = request_buffer_.find('\n');
                if (line_end == std::string::npos) {
                    stream.line_buffer += request_buffer_;
                    request_buffer_.clear();
                    if (stream.line_buffer.size() > 8192) {
                        fail_http1_body_stream(400, "Bad Request\n");
                    }
                    return;
                }

                stream.line_buffer += request_buffer_.substr(0, line_end + 1);
                request_buffer_.erase(0, line_end + 1);
                if (stream.line_buffer.size() < 2 || stream.line_buffer[stream.line_buffer.size() - 2] != '\r') {
                    fail_http1_body_stream(400, "Bad Request\n");
                    return;
                }

                std::string line = stream.line_buffer.substr(0, stream.line_buffer.size() - 2);
                stream.line_buffer.clear();

                if (stream.chunked_phase == ChunkedStreamPhase::trailer_line) {
                    if (line.empty()) {
                        finish_http1_body_stream();
                        return;
                    }
                    if (line.size() > 8192) {
                        fail_http1_body_stream(400, "Bad Request\n");
                        return;
                    }
                    continue;
                }

                const auto extension = line.find(';');
                if (extension != std::string::npos) {
                    line.erase(extension);
                }
                const auto chunk_size = parse_hex_size(trim(std::move(line)));
                if (!chunk_size) {
                    fail_http1_body_stream(400, "Bad Request\n");
                    return;
                }
                if (*chunk_size == 0) {
                    stream.chunked_phase = ChunkedStreamPhase::trailer_line;
                    continue;
                }
                stream.chunk_remaining = *chunk_size;
                stream.chunked_phase = ChunkedStreamPhase::data;
                continue;
            }

            if (stream.chunked_phase == ChunkedStreamPhase::data) {
                const auto take = std::min(stream.chunk_remaining, request_buffer_.size());
                if (stream.accumulator.size() + take > stream.max_body_bytes) {
                    fail_http1_body_stream(413, "Content Too Large\n");
                    return;
                }
                stream.accumulator.append(std::string_view(request_buffer_).substr(0, take));
                request_buffer_.erase(0, take);
                stream.chunk_remaining -= take;
                if (stream.chunk_remaining == 0) {
                    stream.chunked_phase = ChunkedStreamPhase::data_crlf;
                    stream.crlf_seen = 0;
                }
                continue;
            }

            if (stream.chunked_phase == ChunkedStreamPhase::data_crlf) {
                const char expected = stream.crlf_seen == 0 ? '\r' : '\n';
                if (request_buffer_.front() != expected) {
                    fail_http1_body_stream(400, "Bad Request\n");
                    return;
                }
                request_buffer_.erase(0, 1);
                ++stream.crlf_seen;
                if (stream.crlf_seen == 2) {
                    stream.chunked_phase = ChunkedStreamPhase::size_line;
                }
            }
        }
    }

    void fail_http1_body_stream(int status, std::string message)
    {
        http1_body_stream_.reset();
        request_buffer_.clear();
        waiting_for_body_ = false;
        body_started_at_ = {};
        set_response(rimau::http::text_response(status, std::move(message)), rimau::http::BodyMode::include, false);
    }

    void finish_http1_body_stream()
    {
        auto stream = std::move(http1_body_stream_);
        waiting_for_body_ = false;
        body_started_at_ = {};
        request_started_at_ = request_buffer_.empty() ? std::chrono::steady_clock::time_point {} : std::chrono::steady_clock::now();
        auto request = stream->accumulator.finish(std::move(stream->request));
        dispatch_request(std::move(request));
    }

    bool try_handle_websocket_upgrade(const rimau::http::Request& request, std::size_t consumed)
    {
        if (!is_websocket_upgrade_request(request)) {
            return false;
        }

        request_buffer_.erase(0, std::min(request_buffer_.size(), consumed));
        request_started_at_ = request_buffer_.empty() ? std::chrono::steady_clock::time_point {} : std::chrono::steady_clock::now();
        if (security_state_ && !security_state_->allow_request(client_ip_, *config_)) {
            set_response(rimau::http::text_response(429, "Too Many Requests\n"), rimau::http::BodyMode::include, false);
            return true;
        }
        if (auto waf_response = inspect_waf(request)) {
            set_response(std::move(*waf_response), rimau::http::BodyMode::include, false);
            return true;
        }
        if (try_start_websocket_proxy(request)) {
            return true;
        }
        ++requests_served_;
        set_websocket_handshake_response(websocket_handshake_response(request));
        return true;
    }

    bool process_http2_prior_knowledge()
    {
        namespace h2 = rimau::protocol::http2;

        if (ssl_ && !http2_tls_selected_) {
            return false;
        }

        const auto preface = h2::client_connection_preface;
        if (request_buffer_.empty()) {
            return false;
        }

        const auto comparable = std::min(request_buffer_.size(), preface.size());
        if (std::string_view(request_buffer_).substr(0, comparable) != preface.substr(0, comparable)) {
            return false;
        }

        if (request_buffer_.size() < preface.size()) {
            return true;
        }

        if (request_buffer_.size() > config_->max_request_bytes) {
            request_buffer_.clear();
            set_serialized_response(http2_goaway_frame(0x1, "HTTP/2 preface exceeded Rimau request limit"), false);
            return true;
        }

        if (request_buffer_.size() < preface.size() + 9) {
            return true;
        }

        const auto frame_bytes = std::string_view(request_buffer_).substr(preface.size());
        const auto parsed = h2::parse_frame(frame_bytes);
        if (parsed.incomplete) {
            return true;
        }
        if (!parsed.ok || parsed.frame.type != static_cast<std::uint8_t>(h2::FrameType::settings) || parsed.frame.stream_id != 0) {
            request_buffer_.clear();
            set_serialized_response(http2_goaway_frame(0x1, "Rimau expected an HTTP/2 SETTINGS frame after the preface"), false);
            return true;
        }

        request_buffer_.erase(0, preface.size() + parsed.consumed);
        request_started_at_ = {};
        body_started_at_ = {};
        waiting_for_body_ = false;

        if (!config_->http2_enabled) {
            set_serialized_response(http2_goaway_frame(0xd, "HTTP/2 is disabled in Rimau SQLite config"), false);
            return true;
        }

        if (security_state_ && !security_state_->allow_request(client_ip_, *config_)) {
            set_serialized_response(http2_goaway_frame(0xb, "Rimau HTTP/2 rate limit exceeded"), false);
            return true;
        }

        http2_active_ = true;
        std::string response;
        response += http2_settings_frame(false);
        response += http2_settings_frame(true);
        set_serialized_response(std::move(response), true);
        const std::string transport = http2_tls_selected_ ? "TLS ALPN h2" : "h2c";
        log(LogLevel::info, "worker " + std::to_string(worker_id_) + " accepted HTTP/2 " + transport + " preface and entered partial request-serving mode");
        return true;
    }

    void process_http2_frames()
    {
        namespace h2 = rimau::protocol::http2;

        while (phase_ == ClientPhase::reading && !request_buffer_.empty()) {
            if (request_buffer_.size() > config_->max_request_bytes) {
                request_buffer_.clear();
                fail_http2_connection(http2_enhance_your_calm, "HTTP/2 frame buffer exceeded Rimau request limit");
                return;
            }

            const auto parsed = h2::parse_frame(std::string_view(request_buffer_));
            if (parsed.incomplete) {
                return;
            }
            if (!parsed.ok) {
                request_buffer_.clear();
                fail_http2_connection(http2_protocol_error, parsed.error.empty() ? "invalid HTTP/2 frame" : parsed.error);
                return;
            }

            request_buffer_.erase(0, parsed.consumed);
            if (!handle_http2_frame(parsed.frame)) {
                return;
            }
        }
    }

    bool handle_http2_frame(const rimau::protocol::http2::Frame& frame)
    {
        namespace h2 = rimau::protocol::http2;

        const auto type = static_cast<h2::FrameType>(frame.type);
        switch (type) {
        case h2::FrameType::settings:
            if ((frame.flags & 0x1) == 0) {
                try {
                    (void)h2::parse_settings_payload(frame.payload);
                } catch (const std::exception& error) {
                    fail_http2_connection(http2_protocol_error, error.what());
                    return false;
                }
                set_serialized_response(http2_settings_frame(true), true);
                return false;
            }
            return true;

        case h2::FrameType::ping:
            if ((frame.flags & 0x1) == 0) {
                set_serialized_response(http2_ping_frame(frame.payload, true), true);
                return false;
            }
            return true;

        case h2::FrameType::goaway:
            close_now();
            return false;

        case h2::FrameType::rst_stream:
            http2_streams_.erase(frame.stream_id);
            return true;

        case h2::FrameType::window_update:
        case h2::FrameType::priority:
            return true;

        case h2::FrameType::headers:
            return handle_http2_headers_frame(frame);

        case h2::FrameType::data:
            return handle_http2_data_frame(frame);

        case h2::FrameType::continuation:
            fail_http2_connection(http2_compression_error, "HTTP/2 CONTINUATION assembly is not implemented");
            return false;

        case h2::FrameType::push_promise:
            fail_http2_connection(http2_protocol_error, "HTTP/2 clients must not send PUSH_PROMISE");
            return false;
        }

        return true;
    }

    bool handle_http2_headers_frame(const rimau::protocol::http2::Frame& frame)
    {
        if ((frame.stream_id % 2U) == 0) {
            fail_http2_connection(http2_protocol_error, "HTTP/2 client stream id must be odd");
            return false;
        }

        const bool known_stream = http2_streams_.contains(frame.stream_id);
        if (!known_stream && frame.stream_id <= http2_last_client_stream_id_) {
            queue_http2_stream_reset(frame.stream_id, http2_stream_closed);
            return false;
        }

        if ((frame.flags & http2_flag_end_headers) == 0) {
            queue_http2_stream_reset(frame.stream_id, http2_compression_error);
            return false;
        }

        const auto block = http2_header_block_payload(frame);
        if (!block) {
            queue_http2_stream_reset(frame.stream_id, http2_protocol_error);
            return false;
        }

        std::vector<rimau::protocol::http2::HeaderField> headers;
        try {
            headers = rimau::protocol::http2::hpack_decode_header_block(*block);
        } catch (const std::exception& error) {
            log(LogLevel::warning, std::string("HTTP/2 HPACK decode failed: ") + error.what());
            queue_http2_stream_reset(frame.stream_id, http2_compression_error);
            return false;
        }

        auto [stream_it, inserted] = http2_streams_.emplace(frame.stream_id, Http2StreamState {});
        auto& stream = stream_it->second;
        if (!inserted && stream.headers_received) {
            queue_http2_stream_reset(frame.stream_id, http2_protocol_error);
            return false;
        }

        stream.id = frame.stream_id;
        stream.headers_received = true;
        stream.headers = std::move(headers);
        if (frame.stream_id > http2_last_client_stream_id_) {
            http2_last_client_stream_id_ = frame.stream_id;
        }

        if ((frame.flags & http2_flag_end_stream) != 0) {
            dispatch_http2_stream(frame.stream_id);
            return false;
        }

        return true;
    }

    bool handle_http2_data_frame(const rimau::protocol::http2::Frame& frame)
    {
        const auto found = http2_streams_.find(frame.stream_id);
        if (found == http2_streams_.end() || !found->second.headers_received) {
            queue_http2_stream_reset(frame.stream_id, http2_stream_closed);
            return false;
        }

        const auto payload = http2_data_payload(frame);
        if (!payload) {
            queue_http2_stream_reset(frame.stream_id, http2_protocol_error);
            return false;
        }

        auto& stream = found->second;
        if (stream.body.size() + payload->size() > config_->max_request_bytes) {
            queue_http2_stream_reset(frame.stream_id, http2_enhance_your_calm);
            return false;
        }
        stream.body.append(reinterpret_cast<const char*>(payload->data()), payload->size());

        if ((frame.flags & http2_flag_end_stream) != 0) {
            dispatch_http2_stream(frame.stream_id);
            return false;
        }

        return true;
    }

    void fail_http2_connection(std::uint32_t error_code, std::string_view debug_data)
    {
        http2_streams_.clear();
        set_serialized_response(http2_goaway_frame(error_code, debug_data), false);
    }

    void queue_http2_stream_reset(std::uint32_t stream_id, std::uint32_t error_code)
    {
        http2_streams_.erase(stream_id);
        set_serialized_response(http2_rst_stream_frame(stream_id, error_code), true);
    }

    Http2RequestBuildResult build_http2_request(const Http2StreamState& stream) const
    {
        std::unordered_map<std::string, std::string> pseudo_headers;
        rimau::http::Headers headers;
        bool regular_header_seen = false;
        std::optional<std::size_t> content_length;

        const auto error = [](int status, std::string message) {
            Http2RequestBuildResult result;
            result.error_response = rimau::http::text_response(status, std::move(message));
            return result;
        };

        for (const auto& field : stream.headers) {
            const std::string name = lowercase(field.name);
            if (name.empty() || http2_header_name_has_uppercase(field.name) || http2_header_value_has_invalid_control(field.value)) {
                return error(400, "Bad Request\n");
            }

            if (name.front() == ':') {
                if (name != ":method" && name != ":path" && name != ":scheme" && name != ":authority") {
                    return error(400, "Bad Request\n");
                }
                if (regular_header_seen || !pseudo_headers.emplace(name, field.value).second) {
                    return error(400, "Bad Request\n");
                }
                continue;
            }

            regular_header_seen = true;
            if (!valid_header_name(name) || http2_forbidden_request_header(name, field.value)) {
                return error(400, "Bad Request\n");
            }

            if (name == "content-length") {
                if (content_length) {
                    return error(400, "Bad Request\n");
                }
                const auto parsed = parse_decimal_size(trim(field.value));
                if (!parsed) {
                    return error(400, "Bad Request\n");
                }
                content_length = *parsed;
            }

            auto found = headers.find(name);
            if (found == headers.end()) {
                headers[name] = field.value;
            } else if (name == "cookie") {
                found->second += "; ";
                found->second += field.value;
            } else {
                found->second += ", ";
                found->second += field.value;
            }
        }

        const auto method = pseudo_headers.find(":method");
        const auto path = pseudo_headers.find(":path");
        const auto scheme = pseudo_headers.find(":scheme");
        if (method == pseudo_headers.end() || path == pseudo_headers.end() || scheme == pseudo_headers.end()) {
            return error(400, "Bad Request\n");
        }
        if (scheme->second != "http" && scheme->second != "https") {
            return error(400, "Bad Request\n");
        }
        if (content_length && *content_length != stream.body.size()) {
            return error(400, "Bad Request\n");
        }

        const auto authority = pseudo_headers.find(":authority");
        if (authority != pseudo_headers.end()) {
            const auto host = headers.find("host");
            if (host != headers.end() && host->second != authority->second) {
                return error(400, "Bad Request\n");
            }
            headers["host"] = authority->second;
        }

        std::ostringstream raw_request;
        raw_request << method->second << ' ' << path->second << " HTTP/1.1\r\n";
        for (const auto& [name, value] : headers) {
            raw_request << name << ": " << value << "\r\n";
        }
        if (!stream.body.empty() && !content_length) {
            raw_request << "content-length: " << stream.body.size() << "\r\n";
        }
        raw_request << "\r\n";
        raw_request << stream.body;

        auto parsed = rimau::http::parse_request(raw_request.str());
        if (!parsed || !parsed.request) {
            return error(400, "Bad Request\n");
        }
        parsed.request->version = "HTTP/2";
        return Http2RequestBuildResult { std::move(parsed.request), {} };
    }

    void dispatch_http2_stream(std::uint32_t stream_id)
    {
        const auto found = http2_streams_.find(stream_id);
        if (found == http2_streams_.end()) {
            queue_http2_stream_reset(stream_id, http2_stream_closed);
            return;
        }

        auto stream = std::move(found->second);
        http2_streams_.erase(found);

        auto built = build_http2_request(stream);
        if (!built.request) {
            set_serialized_response(
                serialize_http2_response(stream_id, built.error_response, rimau::http::BodyMode::include, *config_),
                true);
            return;
        }

        if (security_state_ && !security_state_->allow_request(client_ip_, *config_)) {
            set_serialized_response(
                serialize_http2_response(stream_id, rimau::http::text_response(429, "Too Many Requests\n"), rimau::http::BodyMode::include, *config_),
                true);
            return;
        }

        if (auto waf_response = inspect_waf(*built.request)) {
            set_serialized_response(
                serialize_http2_response(stream_id, *waf_response, rimau::http::BodyMode::include, *config_),
                true);
            return;
        }

        ++requests_served_;

        rimau::http::VirtualHostHandlerFactory handler_factory(
            config_->document_root,
            config_->virtual_hosts_enabled ? config_->virtual_hosts : "",
            reverse_proxy_settings(),
            static_file_options());
        BufferedResponseSink downstream;
        rimau::http::Transaction transaction(static_cast<std::uint64_t>(requests_served_), *built.request);
        transaction.dispatch(handler_factory, downstream);

        if (!downstream.sent()) {
            set_serialized_response(
                serialize_http2_response(stream_id, rimau::http::text_response(500, "Internal Server Error\n"), rimau::http::BodyMode::include, *config_),
                true);
            return;
        }

        log(
            LogLevel::info,
            "worker " + std::to_string(worker_id_) + " h2 "
                + transaction.request().method + " " + transaction.request().target + " -> " + std::to_string(downstream.last_status()));

        bool keep_connection = true;
        std::string payload = serialize_http2_response(stream_id, downstream.response(), downstream.body_mode(), *config_);
        if (config_->http_keep_alive_max_requests > 0 && requests_served_ >= config_->http_keep_alive_max_requests) {
            payload += http2_goaway_frame(http2_no_error, "Rimau HTTP/2 max requests reached");
            keep_connection = false;
        }
        set_serialized_response(std::move(payload), keep_connection);
    }

    rimau::http::ReverseProxySettings reverse_proxy_settings() const
    {
        rimau::http::ReverseProxySettings settings;
        settings.connect_timeout_seconds = config_->reverse_proxy_connect_timeout_seconds;
        settings.read_timeout_seconds = config_->reverse_proxy_read_timeout_seconds;
        settings.max_response_bytes = config_->reverse_proxy_max_response_bytes;
        settings.retry_count = config_->reverse_proxy_retry_count;
        settings.verify_tls_upstream = config_->reverse_proxy_tls_verify_upstream;
        settings.circuit_breaker_enabled = config_->reverse_proxy_circuit_breaker_enabled;
        settings.circuit_breaker_failure_threshold = config_->reverse_proxy_circuit_breaker_failure_threshold;
        settings.circuit_breaker_cooldown_seconds = config_->reverse_proxy_circuit_breaker_cooldown_seconds;
        return settings;
    }

    rimau::http::StaticFileOptions static_file_options() const
    {
        rimau::http::StaticFileOptions options;
        options.directory_index = config_->directory_index;
        options.error_page = config_->error_page;
        return options;
    }

    rimau::http::WafSettings waf_settings(const rimau::http::Request& request) const
    {
        rimau::http::WafSettings settings;
        settings.enabled = config_->modsecurity_enabled;
        settings.owasp_crs_enabled = config_->modsecurity_owasp_crs_enabled;
        settings.blocking_enabled = config_->modsecurity_blocking_enabled;
        settings.anomaly_threshold = config_->modsecurity_anomaly_threshold;
        settings.max_inspection_bytes = config_->modsecurity_max_inspection_bytes;
        if (config_->virtual_hosts_enabled && !config_->virtual_host_waf_overrides.empty()) {
            const auto overrides = rimau::http::parse_virtual_host_waf_overrides(config_->virtual_host_waf_overrides);
            settings = rimau::http::apply_virtual_host_waf_override(
                std::move(settings),
                rimau::http::select_virtual_host_waf_override(request, overrides));
        }
        return settings;
    }

    std::optional<rimau::http::Response> inspect_waf(const rimau::http::Request& request) const
    {
        const auto settings = waf_settings(request);
        const auto result = rimau::http::inspect_request(request, settings);
        if (!result.inspected || result.matches.empty()) {
            return std::nullopt;
        }

        if (config_->modsecurity_audit_log_enabled) {
            log(LogLevel::warning, waf_audit_log_line(worker_id_, client_ip_, request, settings, result));
        }

        if (!result.allowed) {
            return rimau::http::waf_block_response(result);
        }

        return std::nullopt;
    }

    std::size_t websocket_proxy_buffer_limit() const
    {
        const std::size_t configured = config_ ? config_->websocket_max_frame_bytes : 64 * 1024;
        return std::max<std::size_t>(configured, 64 * 1024);
    }

    void compact_websocket_proxy_buffer(std::string& buffer, std::size_t& offset)
    {
        if (offset == 0) {
            return;
        }
        if (offset >= buffer.size()) {
            buffer.clear();
            offset = 0;
            return;
        }
        buffer.erase(0, offset);
        offset = 0;
    }

    bool append_websocket_proxy_buffer(std::string& buffer, std::size_t& offset, std::string_view data)
    {
        compact_websocket_proxy_buffer(buffer, offset);
        if (buffer.size() + data.size() > websocket_proxy_buffer_limit()) {
            close_now();
            return false;
        }
        buffer.append(data);
        return true;
    }

    void register_websocket_upstream()
    {
        if (websocket_upstream_fd_ < 0 || epoll_fd_ < 0) {
            throw std::runtime_error("websocket upstream cannot be registered without an epoll fd");
        }

        epoll_event event {};
        event.events = EPOLLIN | EPOLLRDHUP | (websocket_proxy_client_to_upstream_.empty() ? 0U : EPOLLOUT);
        event.data.ptr = upstream_token();

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, websocket_upstream_fd_, &event) < 0) {
            throw std::runtime_error(std::string("epoll_ctl ADD websocket upstream failed: ") + std::strerror(errno));
        }
        websocket_upstream_registered_ = true;
    }

    bool try_start_websocket_proxy(const rimau::http::Request& request)
    {
        std::optional<std::vector<rimau::http::ReverseProxyTarget>> upstreams;
        try {
            upstreams = websocket_proxy_upstreams_for_request(request, *config_);
        } catch (const std::exception& error) {
            log(LogLevel::warning, std::string("websocket virtual host config failed: ") + error.what());
            request_buffer_.clear();
            set_response(rimau::http::text_response(500, "Internal Server Error\n"), rimau::http::BodyMode::include, false);
            return true;
        }

        if (!upstreams) {
            return false;
        }

        ++requests_served_;
        const auto settings = reverse_proxy_settings();
        const std::string client_early_bytes = request_buffer_;
        request_buffer_.clear();
        bool skipped_open_circuit = false;
        bool attempted_upstream = false;

        for (const auto& upstream : ordered_websocket_proxy_upstreams(*upstreams, settings.retry_count)) {
            if (!rimau::http::reverse_proxy_upstream_available(upstream, settings)) {
                skipped_open_circuit = true;
                continue;
            }

            attempted_upstream = true;
            try {
                auto connection = connect_websocket_proxy_upstream(request, upstream, settings, client_ip_, ssl_ != nullptr);
                websocket_upstream_fd_ = connection.fd.release();
                websocket_upstream_tls_context_ = std::move(connection.tls_context);
                websocket_upstream_tls_ = std::move(connection.tls);
                rimau::http::record_reverse_proxy_upstream_success(upstream, settings);

                if (!connection.pending_bytes.empty()
                    && !append_websocket_proxy_buffer(
                        websocket_proxy_upstream_to_client_,
                        websocket_proxy_upstream_to_client_offset_,
                        connection.pending_bytes)) {
                    return true;
                }
                if (!client_early_bytes.empty()
                    && !append_websocket_proxy_buffer(
                        websocket_proxy_client_to_upstream_,
                        websocket_proxy_client_to_upstream_offset_,
                        client_early_bytes)) {
                    return true;
                }

                register_websocket_upstream();
                set_websocket_proxy_handshake_response(std::move(connection.handshake_response));
                log(
                    LogLevel::info,
                    "worker " + std::to_string(worker_id_) + " websocket proxy "
                        + request.target + " -> " + upstream.scheme + "://" + upstream.authority);
                return true;
            } catch (const std::exception& error) {
                log(LogLevel::warning, std::string("websocket upstream failed: ") + error.what());
                rimau::http::record_reverse_proxy_upstream_failure(upstream, settings);
                close_websocket_upstream();
                websocket_proxy_client_to_upstream_.clear();
                websocket_proxy_upstream_to_client_.clear();
                websocket_proxy_client_to_upstream_offset_ = 0;
                websocket_proxy_upstream_to_client_offset_ = 0;
                continue;
            }
        }

        if (!attempted_upstream && skipped_open_circuit) {
            auto response = rimau::http::text_response(503, "Service Unavailable\n");
            response.headers["retry-after"] = std::to_string(settings.circuit_breaker_cooldown_seconds);
            set_response(std::move(response), rimau::http::BodyMode::include, false);
            return true;
        }

        set_response(rimau::http::text_response(502, "Bad Gateway\n"), rimau::http::BodyMode::include, false);
        return true;
    }

    void process_request(std::string raw_request)
    {
        const auto parsed = rimau::http::parse_request(raw_request);
        if (!parsed) {
            set_response(rimau::http::text_response(400, "Bad Request\n"), rimau::http::BodyMode::include, false);
            return;
        }

        dispatch_request(*parsed.request);
    }

    void dispatch_request(rimau::http::Request request)
    {
        if (security_state_ && !security_state_->allow_request(client_ip_, *config_)) {
            set_response(rimau::http::text_response(429, "Too Many Requests\n"), rimau::http::BodyMode::include, false);
            return;
        }

        if (auto waf_response = inspect_waf(request)) {
            set_response(std::move(*waf_response), rimau::http::BodyMode::include, false);
            return;
        }

        ++requests_served_;
        bool keep_alive = request_should_keep_alive(request);
        if (requests_served_ >= config_->http_keep_alive_max_requests) {
            keep_alive = false;
        }

        rimau::http::VirtualHostHandlerFactory handler_factory(
            config_->document_root,
            config_->virtual_hosts_enabled ? config_->virtual_hosts : "",
            reverse_proxy_settings(),
            static_file_options());
        BufferedResponseSink downstream;
        rimau::http::Transaction transaction(static_cast<std::uint64_t>(requests_served_), std::move(request));
        transaction.dispatch(handler_factory, downstream);

        if (!downstream.sent()) {
            set_response(rimau::http::text_response(500, "Internal Server Error\n"), rimau::http::BodyMode::include, false);
            return;
        }

        log(
            LogLevel::info,
            "worker " + std::to_string(worker_id_) + " "
                + transaction.request().method + " " + transaction.request().target + " -> " + std::to_string(downstream.last_status()));

        const int remaining_requests = std::max(0, config_->http_keep_alive_max_requests - requests_served_);
        set_serialized_response(
            downstream.payload(*config_, keep_alive, config_->http_keep_alive_timeout_seconds, remaining_requests),
            keep_alive);
    }

    bool request_should_keep_alive(const rimau::http::Request& request) const
    {
        if (!config_->http_keep_alive_enabled) {
            return false;
        }

        if (connection_header_has_token(request, "close")) {
            return false;
        }

        if (request.version == "HTTP/1.1") {
            return true;
        }

        return request.version == "HTTP/1.0" && connection_header_has_token(request, "keep-alive");
    }

    void write_response()
    {
        if (ssl_) {
            write_tls_response();
        } else {
            write_plain_response();
        }
    }

    void write_plain_response()
    {
        while (response_offset_ < response_buffer_.size()) {
            const ssize_t sent = send(
                fd_,
                response_buffer_.data() + response_offset_,
                response_buffer_.size() - response_offset_,
                MSG_NOSIGNAL);

            if (sent > 0) {
                response_offset_ += static_cast<std::size_t>(sent);
                continue;
            }

            if (sent < 0 && would_block()) {
                events_ = EPOLLOUT;
                return;
            }

            close_now();
            return;
        }

        finish_response();
    }

    void write_tls_response()
    {
        while (response_offset_ < response_buffer_.size()) {
            const auto remaining = static_cast<int>(std::min<std::size_t>(response_buffer_.size() - response_offset_, 16 * 1024));
            const int sent = SSL_write(ssl_.get(), response_buffer_.data() + response_offset_, remaining);

            if (sent > 0) {
                response_offset_ += static_cast<std::size_t>(sent);
                continue;
            }

            const int error = SSL_get_error(ssl_.get(), sent);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                events_ = event_for_ssl_error(error);
                return;
            }

            log(LogLevel::warning, "TLS write failed: " + openssl_error());
            close_now();
            return;
        }

        finish_response();
    }

    void finish_response()
    {
        if (websocket_after_response_) {
            websocket_after_response_ = false;
            response_buffer_.clear();
            response_offset_ = 0;
            phase_ = ClientPhase::websocket;
            events_ = EPOLLIN;
            last_activity_ = std::chrono::steady_clock::now();
            process_websocket_frames();
            if (!response_buffer_.empty()) {
                flush_websocket_writes();
            }
            return;
        }

        if (websocket_proxy_after_response_) {
            websocket_proxy_after_response_ = false;
            response_buffer_.clear();
            response_offset_ = 0;
            phase_ = ClientPhase::websocket_proxy;
            last_activity_ = std::chrono::steady_clock::now();
            update_websocket_proxy_events();
            if (!websocket_proxy_upstream_to_client_.empty()) {
                flush_websocket_proxy_client();
            }
            if (!closed() && !websocket_proxy_client_to_upstream_.empty()) {
                flush_websocket_proxy_upstream();
            }
            if (!closed()) {
                update_websocket_proxy_events();
            }
            return;
        }

        if (keep_alive_after_response_) {
            keep_alive_after_response_ = false;
            response_buffer_.clear();
            response_offset_ = 0;
            phase_ = ClientPhase::reading;
            events_ = EPOLLIN;
            last_activity_ = std::chrono::steady_clock::now();
            request_started_at_ = request_buffer_.empty() ? std::chrono::steady_clock::time_point {} : last_activity_;
            body_started_at_ = {};
            waiting_for_body_ = false;
            process_buffered_request();
            return;
        }

        if (ssl_) {
            phase_ = ClientPhase::tls_shutdown;
            events_ = EPOLLOUT;
            do_tls_shutdown();
            return;
        }

        close_now();
    }

    void handle_websocket(std::uint32_t revents)
    {
        if ((revents & EPOLLIN) != 0) {
            read_websocket();
        }
        if (!closed() && (!response_buffer_.empty() || (revents & EPOLLOUT) != 0)) {
            flush_websocket_writes();
        }
    }

    void read_websocket()
    {
        if (ssl_) {
            read_tls_websocket();
        } else {
            read_plain_websocket();
        }
    }

    void read_plain_websocket()
    {
        char chunk[4096];
        while (true) {
            const ssize_t received = recv(fd_, chunk, sizeof(chunk), 0);
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                request_buffer_.append(chunk, static_cast<std::size_t>(received));
                process_websocket_frames();
                if (closed()) {
                    return;
                }
                continue;
            }

            if (received == 0) {
                close_now();
                return;
            }

            if (would_block()) {
                events_ = EPOLLIN | (response_buffer_.empty() ? 0U : EPOLLOUT);
                return;
            }

            close_now();
            return;
        }
    }

    void read_tls_websocket()
    {
        char chunk[4096];
        while (true) {
            const int received = SSL_read(ssl_.get(), chunk, static_cast<int>(sizeof(chunk)));
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                request_buffer_.append(chunk, static_cast<std::size_t>(received));
                process_websocket_frames();
                if (closed()) {
                    return;
                }
                continue;
            }

            const int error = SSL_get_error(ssl_.get(), received);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                events_ = event_for_ssl_error(error) | (response_buffer_.empty() ? 0U : EPOLLOUT);
                return;
            }
            if (error == SSL_ERROR_ZERO_RETURN) {
                close_now();
                return;
            }

            close_now();
            return;
        }
    }

    void queue_websocket_frame(std::uint8_t opcode, std::string_view payload)
    {
        response_buffer_ += websocket_frame(opcode, payload);
        events_ = EPOLLIN | EPOLLOUT;
    }

    void process_websocket_frames()
    {
        while (request_buffer_.size() >= 2) {
            const auto first = static_cast<unsigned char>(request_buffer_[0]);
            const auto second = static_cast<unsigned char>(request_buffer_[1]);
            const bool fin = (first & 0x80) != 0;
            const std::uint8_t opcode = first & 0x0f;
            const bool masked = (second & 0x80) != 0;
            std::uint64_t payload_length = second & 0x7f;
            std::size_t cursor = 2;

            if (!fin || !masked) {
                queue_websocket_frame(0x8, {});
                websocket_close_after_write_ = true;
                return;
            }

            if (payload_length == 126) {
                if (request_buffer_.size() < cursor + 2) {
                    return;
                }
                payload_length = (static_cast<std::uint64_t>(static_cast<unsigned char>(request_buffer_[cursor])) << 8)
                    | static_cast<unsigned char>(request_buffer_[cursor + 1]);
                cursor += 2;
            } else if (payload_length == 127) {
                if (request_buffer_.size() < cursor + 8) {
                    return;
                }
                payload_length = 0;
                for (int index = 0; index < 8; ++index) {
                    payload_length = (payload_length << 8) | static_cast<unsigned char>(request_buffer_[cursor + static_cast<std::size_t>(index)]);
                }
                cursor += 8;
            }

            if (payload_length > config_->websocket_max_frame_bytes || request_buffer_.size() < cursor + 4 + payload_length) {
                if (payload_length > config_->websocket_max_frame_bytes || request_buffer_.size() > config_->websocket_max_frame_bytes + 14) {
                    queue_websocket_frame(0x8, {});
                    websocket_close_after_write_ = true;
                }
                return;
            }

            std::array<unsigned char, 4> mask {
                static_cast<unsigned char>(request_buffer_[cursor]),
                static_cast<unsigned char>(request_buffer_[cursor + 1]),
                static_cast<unsigned char>(request_buffer_[cursor + 2]),
                static_cast<unsigned char>(request_buffer_[cursor + 3])
            };
            cursor += 4;

            std::string payload;
            payload.resize(static_cast<std::size_t>(payload_length));
            for (std::size_t index = 0; index < payload.size(); ++index) {
                payload[index] = static_cast<char>(static_cast<unsigned char>(request_buffer_[cursor + index]) ^ mask[index % 4]);
            }
            request_buffer_.erase(0, cursor + static_cast<std::size_t>(payload_length));

            if (opcode == 0x1 || opcode == 0x2) {
                queue_websocket_frame(opcode, payload);
            } else if (opcode == 0x8) {
                queue_websocket_frame(0x8, payload);
                websocket_close_after_write_ = true;
                return;
            } else if (opcode == 0x9) {
                queue_websocket_frame(0xA, payload);
            } else if (opcode == 0xA) {
                continue;
            } else {
                queue_websocket_frame(0x8, {});
                websocket_close_after_write_ = true;
                return;
            }
        }
    }

    void flush_websocket_writes()
    {
        while (response_offset_ < response_buffer_.size()) {
            if (ssl_) {
                const auto remaining = static_cast<int>(std::min<std::size_t>(response_buffer_.size() - response_offset_, 16 * 1024));
                const int sent = SSL_write(ssl_.get(), response_buffer_.data() + response_offset_, remaining);
                if (sent > 0) {
                    response_offset_ += static_cast<std::size_t>(sent);
                    continue;
                }
                const int error = SSL_get_error(ssl_.get(), sent);
                if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                    events_ = event_for_ssl_error(error) | EPOLLIN;
                    return;
                }
                close_now();
                return;
            }

            const ssize_t sent = send(
                fd_,
                response_buffer_.data() + response_offset_,
                response_buffer_.size() - response_offset_,
                MSG_NOSIGNAL);
            if (sent > 0) {
                response_offset_ += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && would_block()) {
                events_ = EPOLLIN | EPOLLOUT;
                return;
            }
            close_now();
            return;
        }

        response_buffer_.clear();
        response_offset_ = 0;
        if (websocket_close_after_write_) {
            close_now();
            return;
        }
        events_ = EPOLLIN;
    }

    void handle_websocket_proxy_client(std::uint32_t revents)
    {
        if ((revents & EPOLLIN) != 0) {
            read_websocket_proxy_client();
        }
        if (!closed() && (!websocket_proxy_upstream_to_client_.empty() || (revents & EPOLLOUT) != 0)) {
            flush_websocket_proxy_client();
        }
        if (!closed()) {
            update_websocket_proxy_events();
        }
    }

    void read_websocket_proxy_client()
    {
        if (ssl_) {
            read_tls_websocket_proxy_client();
        } else {
            read_plain_websocket_proxy_client();
        }
    }

    void read_plain_websocket_proxy_client()
    {
        std::array<char, 8192> chunk {};
        while (true) {
            const ssize_t received = recv(fd_, chunk.data(), chunk.size(), 0);
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                websocket_proxy_client_extra_events_ = 0;
                if (!append_websocket_proxy_buffer(
                        websocket_proxy_client_to_upstream_,
                        websocket_proxy_client_to_upstream_offset_,
                        std::string_view(chunk.data(), static_cast<std::size_t>(received)))) {
                    return;
                }
                flush_websocket_proxy_upstream();
                if (closed()) {
                    return;
                }
                continue;
            }

            if (received == 0) {
                close_now();
                return;
            }

            if (would_block()) {
                websocket_proxy_client_extra_events_ = 0;
                return;
            }

            close_now();
            return;
        }
    }

    void read_tls_websocket_proxy_client()
    {
        std::array<char, 8192> chunk {};
        while (true) {
            const int received = SSL_read(ssl_.get(), chunk.data(), static_cast<int>(chunk.size()));
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                websocket_proxy_client_extra_events_ = 0;
                if (!append_websocket_proxy_buffer(
                        websocket_proxy_client_to_upstream_,
                        websocket_proxy_client_to_upstream_offset_,
                        std::string_view(chunk.data(), static_cast<std::size_t>(received)))) {
                    return;
                }
                flush_websocket_proxy_upstream();
                if (closed()) {
                    return;
                }
                continue;
            }

            const int error = SSL_get_error(ssl_.get(), received);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                websocket_proxy_client_extra_events_ = event_for_ssl_error(error);
                return;
            }
            if (error == SSL_ERROR_ZERO_RETURN) {
                close_now();
                return;
            }

            close_now();
            return;
        }
    }

    void read_websocket_proxy_upstream()
    {
        if (websocket_upstream_tls_) {
            read_tls_websocket_proxy_upstream();
        } else {
            read_plain_websocket_proxy_upstream();
        }
    }

    void read_plain_websocket_proxy_upstream()
    {
        std::array<char, 8192> chunk {};
        while (true) {
            const ssize_t received = recv(websocket_upstream_fd_, chunk.data(), chunk.size(), 0);
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                websocket_proxy_upstream_extra_events_ = 0;
                if (!append_websocket_proxy_buffer(
                        websocket_proxy_upstream_to_client_,
                        websocket_proxy_upstream_to_client_offset_,
                        std::string_view(chunk.data(), static_cast<std::size_t>(received)))) {
                    return;
                }
                flush_websocket_proxy_client();
                if (closed()) {
                    return;
                }
                continue;
            }

            if (received == 0) {
                close_now();
                return;
            }

            if (would_block()) {
                websocket_proxy_upstream_extra_events_ = 0;
                return;
            }

            close_now();
            return;
        }
    }

    void read_tls_websocket_proxy_upstream()
    {
        std::array<char, 8192> chunk {};
        while (true) {
            const int received = SSL_read(websocket_upstream_tls_.get(), chunk.data(), static_cast<int>(chunk.size()));
            if (received > 0) {
                last_activity_ = std::chrono::steady_clock::now();
                websocket_proxy_upstream_extra_events_ = 0;
                if (!append_websocket_proxy_buffer(
                        websocket_proxy_upstream_to_client_,
                        websocket_proxy_upstream_to_client_offset_,
                        std::string_view(chunk.data(), static_cast<std::size_t>(received)))) {
                    return;
                }
                flush_websocket_proxy_client();
                if (closed()) {
                    return;
                }
                continue;
            }

            const int error = SSL_get_error(websocket_upstream_tls_.get(), received);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                websocket_proxy_upstream_extra_events_ = event_for_ssl_error(error);
                return;
            }
            if (error == SSL_ERROR_ZERO_RETURN) {
                close_now();
                return;
            }

            close_now();
            return;
        }
    }

    void flush_websocket_proxy_client()
    {
        while (websocket_proxy_upstream_to_client_offset_ < websocket_proxy_upstream_to_client_.size()) {
            if (ssl_) {
                const auto remaining = static_cast<int>(std::min<std::size_t>(
                    websocket_proxy_upstream_to_client_.size() - websocket_proxy_upstream_to_client_offset_,
                    16 * 1024));
                const int sent = SSL_write(
                    ssl_.get(),
                    websocket_proxy_upstream_to_client_.data() + websocket_proxy_upstream_to_client_offset_,
                    remaining);
                if (sent > 0) {
                    websocket_proxy_upstream_to_client_offset_ += static_cast<std::size_t>(sent);
                    last_activity_ = std::chrono::steady_clock::now();
                    websocket_proxy_client_extra_events_ = 0;
                    continue;
                }

                const int error = SSL_get_error(ssl_.get(), sent);
                if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                    websocket_proxy_client_extra_events_ = event_for_ssl_error(error);
                    return;
                }
                close_now();
                return;
            }

            const ssize_t sent = send(
                fd_,
                websocket_proxy_upstream_to_client_.data() + websocket_proxy_upstream_to_client_offset_,
                websocket_proxy_upstream_to_client_.size() - websocket_proxy_upstream_to_client_offset_,
                MSG_NOSIGNAL);
            if (sent > 0) {
                websocket_proxy_upstream_to_client_offset_ += static_cast<std::size_t>(sent);
                last_activity_ = std::chrono::steady_clock::now();
                websocket_proxy_client_extra_events_ = 0;
                continue;
            }
            if (sent < 0 && would_block()) {
                return;
            }
            close_now();
            return;
        }

        websocket_proxy_upstream_to_client_.clear();
        websocket_proxy_upstream_to_client_offset_ = 0;
    }

    void flush_websocket_proxy_upstream()
    {
        while (websocket_proxy_client_to_upstream_offset_ < websocket_proxy_client_to_upstream_.size()) {
            if (websocket_upstream_tls_) {
                const auto remaining = static_cast<int>(std::min<std::size_t>(
                    websocket_proxy_client_to_upstream_.size() - websocket_proxy_client_to_upstream_offset_,
                    16 * 1024));
                const int sent = SSL_write(
                    websocket_upstream_tls_.get(),
                    websocket_proxy_client_to_upstream_.data() + websocket_proxy_client_to_upstream_offset_,
                    remaining);
                if (sent > 0) {
                    websocket_proxy_client_to_upstream_offset_ += static_cast<std::size_t>(sent);
                    last_activity_ = std::chrono::steady_clock::now();
                    websocket_proxy_upstream_extra_events_ = 0;
                    continue;
                }

                const int error = SSL_get_error(websocket_upstream_tls_.get(), sent);
                if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                    websocket_proxy_upstream_extra_events_ = event_for_ssl_error(error);
                    return;
                }
                close_now();
                return;
            }

            const ssize_t sent = send(
                websocket_upstream_fd_,
                websocket_proxy_client_to_upstream_.data() + websocket_proxy_client_to_upstream_offset_,
                websocket_proxy_client_to_upstream_.size() - websocket_proxy_client_to_upstream_offset_,
                MSG_NOSIGNAL);
            if (sent > 0) {
                websocket_proxy_client_to_upstream_offset_ += static_cast<std::size_t>(sent);
                last_activity_ = std::chrono::steady_clock::now();
                websocket_proxy_upstream_extra_events_ = 0;
                continue;
            }
            if (sent < 0 && would_block()) {
                return;
            }
            close_now();
            return;
        }

        websocket_proxy_client_to_upstream_.clear();
        websocket_proxy_client_to_upstream_offset_ = 0;
    }

    void update_websocket_proxy_events()
    {
        if (phase_ != ClientPhase::websocket_proxy || closed()) {
            return;
        }

        events_ = EPOLLIN
            | websocket_proxy_client_extra_events_
            | (websocket_proxy_upstream_to_client_.empty() ? 0U : EPOLLOUT);

        if (epoll_fd_ >= 0 && fd_ >= 0) {
            epoll_event client_event {};
            client_event.events = events_ | EPOLLRDHUP;
            client_event.data.ptr = token();
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd_, &client_event) < 0) {
                log(LogLevel::warning, std::string("epoll_ctl MOD websocket client failed: ") + std::strerror(errno));
                close_now();
                return;
            }
        }

        if (epoll_fd_ >= 0 && websocket_upstream_fd_ >= 0 && websocket_upstream_registered_) {
            epoll_event upstream_event {};
            upstream_event.events = EPOLLIN
                | EPOLLRDHUP
                | websocket_proxy_upstream_extra_events_
                | (websocket_proxy_client_to_upstream_.empty() ? 0U : EPOLLOUT);
            upstream_event.data.ptr = upstream_token();
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, websocket_upstream_fd_, &upstream_event) < 0) {
                log(LogLevel::warning, std::string("epoll_ctl MOD websocket upstream failed: ") + std::strerror(errno));
                close_now();
            }
        }
    }

    void do_tls_shutdown()
    {
        const int result = SSL_shutdown(ssl_.get());
        if (result >= 0) {
            close_now();
            return;
        }

        const int error = SSL_get_error(ssl_.get(), result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            events_ = event_for_ssl_error(error);
            return;
        }

        close_now();
    }

    int fd_ = -1;
    int epoll_fd_ = -1;
    std::shared_ptr<const ServerConfig> config_;
    std::shared_ptr<const TlsContext> tls_context_holder_;
    std::size_t worker_id_ = 0;
    std::string client_ip_;
    SecurityState* security_state_ = nullptr;
    std::unique_ptr<SSL, SslDeleter> ssl_;
    std::string negotiated_alpn_;
    bool http2_tls_selected_ = false;
    std::unique_ptr<SSL_CTX, SslCtxDeleter> websocket_upstream_tls_context_;
    std::unique_ptr<SSL, SslDeleter> websocket_upstream_tls_;
    ClientPhase phase_ = ClientPhase::closed;
    std::uint32_t events_ = 0;
    EventToken token_;
    EventToken upstream_token_;
    int websocket_upstream_fd_ = -1;
    bool websocket_upstream_registered_ = false;
    std::string request_buffer_;
    std::string response_buffer_;
    std::size_t response_offset_ = 0;
    bool keep_alive_after_response_ = false;
    bool websocket_after_response_ = false;
    bool websocket_proxy_after_response_ = false;
    bool websocket_close_after_write_ = false;
    std::string websocket_proxy_client_to_upstream_;
    std::string websocket_proxy_upstream_to_client_;
    std::size_t websocket_proxy_client_to_upstream_offset_ = 0;
    std::size_t websocket_proxy_upstream_to_client_offset_ = 0;
    std::uint32_t websocket_proxy_client_extra_events_ = 0;
    std::uint32_t websocket_proxy_upstream_extra_events_ = 0;
    bool http2_active_ = false;
    std::uint32_t http2_last_client_stream_id_ = 0;
    std::unordered_map<std::uint32_t, Http2StreamState> http2_streams_;
    std::unique_ptr<Http1BodyStreamState> http1_body_stream_;
    int requests_served_ = 0;
    std::chrono::steady_clock::time_point last_activity_;
    std::chrono::steady_clock::time_point request_started_at_;
    std::chrono::steady_clock::time_point body_started_at_;
    bool waiting_for_body_ = false;
};

class ConnectionPool {
public:
    explicit ConnectionPool(std::size_t max_idle)
        : max_idle_(max_idle)
    {
    }

    std::unique_ptr<ClientConnection> acquire()
    {
        if (idle_.empty()) {
            return std::make_unique<ClientConnection>();
        }

        auto connection = std::move(idle_.back());
        idle_.pop_back();
        return connection;
    }

    void release(std::unique_ptr<ClientConnection> connection)
    {
        connection->prepare_for_pool();
        if (idle_.size() < max_idle_) {
            idle_.push_back(std::move(connection));
        }
    }

private:
    std::size_t max_idle_;
    std::vector<std::unique_ptr<ClientConnection>> idle_;
};

struct RuntimeControl {
    std::atomic_bool shutdown_requested { false };
    std::atomic_bool worker_failed { false };
    std::shared_ptr<const ServerConfig> live_config;
    std::shared_ptr<const TlsContext> live_tls_context;
    std::atomic_uint64_t config_generation { 0 };
    SecurityState security;

    std::shared_ptr<const ServerConfig> config_snapshot() const
    {
        return std::atomic_load_explicit(&live_config, std::memory_order_acquire);
    }

    void publish_config(ServerConfig config)
    {
        std::shared_ptr<const ServerConfig> next_config = std::make_shared<ServerConfig>(std::move(config));
        std::atomic_store_explicit(&live_config, std::move(next_config), std::memory_order_release);
        config_generation.fetch_add(1, std::memory_order_release);
    }

    std::shared_ptr<const TlsContext> tls_context_snapshot() const
    {
        return std::atomic_load_explicit(&live_tls_context, std::memory_order_acquire);
    }

    void publish_tls_context(std::shared_ptr<const TlsContext> tls_context)
    {
        std::atomic_store_explicit(&live_tls_context, std::move(tls_context), std::memory_order_release);
    }
};

class Worker {
public:
    Worker(std::size_t id, ServerConfig config, RuntimeControl& control)
        : id_(id)
        , listener_config_(std::move(config))
        , control_(control)
        , connection_pool_(listener_config_.connection_pool_size)
        , events_(static_cast<std::size_t>(listener_config_.epoll_max_events))
    {
    }

    void run(std::stop_token stop_token)
    {
        epoll_fd_ = UniqueFd(epoll_create1(EPOLL_CLOEXEC));
        if (!epoll_fd_.valid()) {
            throw_errno("epoll_create1 failed");
        }

        listener_ = create_listener();
        add_listener_to_epoll();

        log(LogLevel::info, "worker " + std::to_string(id_) + " started with epoll reactor");

        bool draining = false;
        auto shutdown_deadline = std::chrono::steady_clock::time_point::max();

        while (true) {
            const bool shutdown_requested = control_.shutdown_requested.load(std::memory_order_relaxed) || stop_token.stop_requested();
            if (shutdown_requested && !draining) {
                draining = true;
                const auto config = control_.config_snapshot();
                const int timeout_seconds = config ? config->graceful_shutdown_timeout_seconds : listener_config_.graceful_shutdown_timeout_seconds;
                shutdown_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
                stop_accepting();
                log(LogLevel::info, "worker " + std::to_string(id_) + " draining active connections");
            }

            if (draining && clients_.empty()) {
                break;
            }

            if (draining && std::chrono::steady_clock::now() >= shutdown_deadline) {
                close_all_clients();
                break;
            }

            const int timeout_ms = draining ? 100 : 1000;
            const int ready = epoll_wait(epoll_fd_.get(), events_.data(), static_cast<int>(events_.size()), timeout_ms);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw_errno("epoll_wait failed");
            }

            refresh_client_configs_if_needed();

            for (int index = 0; index < ready; ++index) {
                auto* token = static_cast<EventToken*>(events_[static_cast<std::size_t>(index)].data.ptr);
                if (!token || token->type != EventTokenType::listener || draining) {
                    continue;
                }
                accept_ready_clients();
            }

            std::vector<int> closed_clients;
            for (int index = 0; index < ready; ++index) {
                auto* token = static_cast<EventToken*>(events_[static_cast<std::size_t>(index)].data.ptr);
                if (!token || !token->connection || (token->type != EventTokenType::client && token->type != EventTokenType::upstream)) {
                    continue;
                }

                ClientConnection* connection = token->connection;
                const int fd = connection->fd();
                const auto found = clients_.find(fd);
                if (found == clients_.end() || found->second.get() != connection) {
                    continue;
                }

                if (token->type == EventTokenType::upstream) {
                    connection->handle_upstream(events_[static_cast<std::size_t>(index)].events);
                } else {
                    connection->handle(events_[static_cast<std::size_t>(index)].events);
                }
                if (connection->closed()) {
                    closed_clients.push_back(fd);
                } else {
                    if (token->type == EventTokenType::client) {
                        modify_client_events(*connection);
                        if (connection->closed()) {
                            closed_clients.push_back(fd);
                        }
                    }
                }
            }

            for (const int fd : closed_clients) {
                release_client(fd);
            }

            enforce_client_timeouts();
        }

        close_all_clients();
        stop_accepting();
        log(LogLevel::info, "worker " + std::to_string(id_) + " stopped");
    }

private:
    UniqueFd create_listener()
    {
        const auto bind_address = make_bind_address(listener_config_.host, listener_config_.port);
        UniqueFd listener(socket(bind_address.family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
        if (!listener.valid()) {
            throw_errno("socket failed");
        }

        set_required_socket_option(listener.get(), SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR");

        if (listener_config_.reuse_port_enabled) {
#if defined(SO_REUSEPORT)
            set_required_socket_option(listener.get(), SOL_SOCKET, SO_REUSEPORT, 1, "SO_REUSEPORT");
#else
            throw std::runtime_error("SO_REUSEPORT requested but not available on this platform");
#endif
        }

        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&bind_address.storage), bind_address.length) < 0) {
            throw_errno("bind failed");
        }

        if (listen(listener.get(), listener_config_.listen_backlog) < 0) {
            throw_errno("listen failed");
        }

        return listener;
    }

    void add_listener_to_epoll()
    {
        listener_token_ = EventToken { EventTokenType::listener, nullptr };

        epoll_event event {};
        event.events = EPOLLIN;
        event.data.ptr = &listener_token_;

        if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, listener_.get(), &event) < 0) {
            throw_errno("epoll_ctl ADD listener failed");
        }
    }

    void stop_accepting()
    {
        if (!listener_.valid()) {
            return;
        }

        epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, listener_.get(), nullptr);
        listener_.reset();
    }

    void accept_ready_clients()
    {
        while (true) {
            sockaddr_storage client_address {};
            socklen_t client_length = sizeof(client_address);
            const int client_fd = accept4(
                listener_.get(),
                reinterpret_cast<sockaddr*>(&client_address),
                &client_length,
                SOCK_NONBLOCK | SOCK_CLOEXEC);

            if (client_fd < 0) {
                if (would_block()) {
                    return;
                }
                if (errno == EINTR) {
                    continue;
                }

                log(LogLevel::warning, std::string("accept4 failed: ") + std::strerror(errno));
                return;
            }

            const auto config = control_.config_snapshot();
            if (!config) {
                close(client_fd);
                return;
            }

            const std::string client_ip = render_client_ip(client_address);

            const auto accept_decision = control_.security.try_open_connection(client_ip, *config);
            if (accept_decision != SecurityState::AcceptDecision::allowed) {
                log(LogLevel::warning, "connection rejected from " + client_ip);
                close(client_fd);
                continue;
            }

            configure_client_socket(client_fd, *config);

            auto connection = connection_pool_.acquire();
            try {
                connection->reset(client_fd, epoll_fd_.get(), config, control_.tls_context_snapshot(), id_, client_ip, &control_.security);
                add_client_to_epoll(*connection);
                clients_.emplace(client_fd, std::move(connection));
            } catch (const std::exception& error) {
                control_.security.close_connection(client_ip);
                connection_pool_.release(std::move(connection));
                log(LogLevel::warning, error.what());
            }
        }
    }

    void add_client_to_epoll(ClientConnection& connection)
    {
        epoll_event event {};
        event.events = connection.events() | EPOLLRDHUP;
        event.data.ptr = connection.token();

        if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, connection.fd(), &event) < 0) {
            throw_errno("epoll_ctl ADD client failed");
        }
    }

    void modify_client_events(ClientConnection& connection)
    {
        epoll_event event {};
        event.events = connection.events() | EPOLLRDHUP;
        event.data.ptr = connection.token();

        if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, connection.fd(), &event) < 0) {
            log(LogLevel::warning, std::string("epoll_ctl MOD client failed: ") + std::strerror(errno));
            connection.handle(EPOLLERR);
        }
    }

    void release_client(int fd)
    {
        const auto found = clients_.find(fd);
        if (found == clients_.end()) {
            return;
        }

        epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
        auto connection = std::move(found->second);
        clients_.erase(found);
        control_.security.close_connection(connection->client_ip());
        connection_pool_.release(std::move(connection));
    }

    void close_all_clients()
    {
        std::vector<int> fds;
        fds.reserve(clients_.size());
        for (const auto& [fd, _] : clients_) {
            fds.push_back(fd);
        }
        for (const int fd : fds) {
            release_client(fd);
        }
    }

    void refresh_client_configs_if_needed()
    {
        const std::uint64_t generation = control_.config_generation.load(std::memory_order_acquire);
        if (generation == config_generation_) {
            return;
        }

        const auto config = control_.config_snapshot();
        if (!config) {
            return;
        }

        for (auto& [_, connection] : clients_) {
            connection->update_config(config);
        }
        config_generation_ = generation;
    }

    void enforce_client_timeouts()
    {
        const auto now = std::chrono::steady_clock::now();
        std::vector<int> closed_clients;
        for (auto& [fd, connection] : clients_) {
            if (!connection->expire_if_timed_out(now)) {
                continue;
            }

            if (connection->closed()) {
                closed_clients.push_back(fd);
            } else {
                modify_client_events(*connection);
                if (connection->closed()) {
                    closed_clients.push_back(fd);
                }
            }
        }

        for (const int fd : closed_clients) {
            release_client(fd);
        }
    }

    std::size_t id_;
    ServerConfig listener_config_;
    RuntimeControl& control_;
    std::uint64_t config_generation_ = 0;
    UniqueFd epoll_fd_;
    UniqueFd listener_;
    EventToken listener_token_ { EventTokenType::listener, nullptr };
    ConnectionPool connection_pool_;
    std::unordered_map<int, std::unique_ptr<ClientConnection>> clients_;
    std::vector<epoll_event> events_;
};

} // namespace

Server::Server(ServerConfig config)
    : config_(std::move(config))
{
}

int Server::run()
{
    install_runtime_signal_handlers();
    g_shutdown_signal_requested = 0;
    g_reload_signal_requested = 0;
    g_last_signal = 0;

    const bool http2_transport_enabled = config_.http2_enabled
        && (!config_.tls_enabled || alpn_protocol_configured(config_.tls_alpn_protocols, "h2"));
    if (!config_.http1_enabled && !http2_transport_enabled) {
        throw std::runtime_error("no client-served protocol is enabled: HTTP/1.1 is disabled, HTTP/2 is not available for the configured transport, and HTTP/3 is not implemented yet");
    }

    log_protocol_config_warnings(config_);

    const std::size_t worker_count = resolve_worker_count(config_);
    if (worker_count > 1 && !config_.reuse_port_enabled) {
        throw std::runtime_error("reuse_port_enabled=false cannot run multiple per-worker listeners; set worker_threads=1 or enable SO_REUSEPORT");
    }

    std::shared_ptr<const TlsContext> tls_context;
    if (config_.tls_enabled) {
        tls_context = std::make_shared<TlsContext>(config_);
        log(LogLevel::info, "TLS enabled using certificate: " + config_.tls_certificate_file.string());
    }

    RuntimeControl control;
    control.publish_config(config_);
    control.publish_tls_context(tls_context);

    std::vector<std::jthread> workers;
    workers.reserve(worker_count);

    for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
        workers.emplace_back([&, worker_id](std::stop_token stop_token) {
            try {
                Worker worker(worker_id, config_, control);
                worker.run(stop_token);
            } catch (const std::exception& error) {
                control.worker_failed.store(true, std::memory_order_relaxed);
                control.shutdown_requested.store(true, std::memory_order_relaxed);
                log(LogLevel::error, "worker " + std::to_string(worker_id) + " failed: " + error.what());
            }
        });
    }

    log(
        LogLevel::info,
        "rimau-server listening on "
            + std::string(config_.tls_enabled ? "https://" : "http://")
            + config_.host + ":" + std::to_string(config_.port));
    log(
        LogLevel::info,
        "I/O model: Linux epoll reactor, non-blocking sockets, workers="
            + std::to_string(worker_count)
            + ", SO_REUSEPORT=" + std::string(config_.reuse_port_enabled ? "on" : "off"));
    log(LogLevel::info, "document root: " + config_.document_root.string());

    while (!control.shutdown_requested.load(std::memory_order_relaxed)) {
        if (g_reload_signal_requested != 0) {
            g_reload_signal_requested = 0;
            try {
                auto next_config = load_config_from_database(config_.database_path);
                const auto blockers = live_reload_blockers(config_, next_config);
                if (!blockers.empty()) {
                    log(LogLevel::warning, "SIGHUP reload ignored; changed keys require restart: " + join_keys(blockers));
                } else {
                    if (config_.tls_enabled && tls_runtime_settings_changed(config_, next_config)) {
                        tls_context = std::make_shared<TlsContext>(next_config);
                        control.publish_tls_context(tls_context);
                        log(LogLevel::info, "SIGHUP TLS context reload applied for new connections");
                    }
                    config_ = std::move(next_config);
                    control.publish_config(config_);
                    log(LogLevel::info, "SIGHUP reload applied from SQLite database: " + config_.database_path.string());
                    log(LogLevel::info, "document root: " + config_.document_root.string());
                    log_protocol_config_warnings(config_);
                }
            } catch (const std::exception& error) {
                log(LogLevel::error, std::string("SIGHUP reload failed: ") + error.what());
            }
        }

        if (g_shutdown_signal_requested != 0) {
            const int signal_number = g_last_signal;
            control.shutdown_requested.store(true, std::memory_order_relaxed);
            log(LogLevel::info, "shutdown signal received: " + std::to_string(signal_number));
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& worker : workers) {
        worker.request_stop();
    }

    log(LogLevel::info, "graceful shutdown requested");

    return control.worker_failed.load(std::memory_order_relaxed) ? 1 : 0;
}

} // namespace rimau::core
