#include "rimau/http/virtual_host.hpp"

#include "rimau/http/response.hpp"
#include "rimau/http/static_file_handler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <utility>

#include <openssl/ssl.h>

namespace rimau::http {
namespace {

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

struct AddrInfoDeleter {
    void operator()(addrinfo* value) const noexcept
    {
        if (value) {
            freeaddrinfo(value);
        }
    }
};

using AddrInfoPtr = std::unique_ptr<addrinfo, AddrInfoDeleter>;

struct SslCtxDeleter {
    void operator()(SSL_CTX* value) const noexcept
    {
        if (value) {
            SSL_CTX_free(value);
        }
    }
};

struct SslDeleter {
    void operator()(SSL* value) const noexcept
    {
        if (value) {
            SSL_free(value);
        }
    }
};

using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;
using SslPtr = std::unique_ptr<SSL, SslDeleter>;

struct UpstreamConnection {
    UniqueFd fd;
    SslCtxPtr tls_context;
    SslPtr tls;
};

struct CircuitBreakerEntry {
    std::size_t failure_count = 0;
    std::chrono::steady_clock::time_point opened_until {};
};

std::atomic<std::size_t> next_proxy_upstream { 0 };
std::mutex circuit_breaker_mutex;
std::unordered_map<std::string, CircuitBreakerEntry> circuit_breakers;

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

bool contains_control_character(std::string_view value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7f;
    });
}

bool valid_runtime_name(std::string_view value)
{
    if (value.empty()) {
        return false;
    }

    for (const unsigned char ch : value) {
        const bool ok = std::isalnum(ch) || ch == '_' || ch == '-' || ch == '+' || ch == '.';
        if (!ok) {
            return false;
        }
    }

    return true;
}

bool valid_host_pattern(std::string_view value)
{
    if (value.empty() || contains_control_character(value)) {
        return false;
    }

    if (value.starts_with("*.")) {
        value.remove_prefix(2);
        if (value.empty()) {
            return false;
        }
    }

    for (const unsigned char ch : value) {
        const bool ok = std::isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '[' || ch == ']' || ch == ':';
        if (!ok) {
            return false;
        }
    }

    return true;
}

std::uint16_t parse_upstream_port(std::string_view value)
{
    if (value.empty()) {
        throw std::runtime_error("reverse proxy upstream port is invalid");
    }

    std::uint32_t parsed = 0;
    for (const unsigned char ch : value) {
        if (!std::isdigit(ch)) {
            throw std::runtime_error("reverse proxy upstream port is invalid");
        }
        parsed = parsed * 10 + static_cast<std::uint32_t>(ch - '0');
        if (parsed > std::numeric_limits<std::uint16_t>::max()) {
            throw std::runtime_error("reverse proxy upstream port is out of range");
        }
    }

    if (parsed == 0) {
        throw std::runtime_error("reverse proxy upstream port is invalid");
    }

    return static_cast<std::uint16_t>(parsed);
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

bool is_hop_by_hop_header(const std::string& name)
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

int timeout_ms(int seconds)
{
    if (seconds <= 0) {
        return 1;
    }
    if (seconds > std::numeric_limits<int>::max() / 1000) {
        return std::numeric_limits<int>::max();
    }
    return seconds * 1000;
}

bool wait_fd(int fd, short events, int timeout_seconds)
{
    pollfd descriptor {};
    descriptor.fd = fd;
    descriptor.events = events;

    while (true) {
        const int ready = poll(&descriptor, 1, timeout_ms(timeout_seconds));
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

void set_nonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error(std::string("fcntl(F_GETFL) failed: ") + std::strerror(errno));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error(std::string("fcntl(F_SETFL) failed: ") + std::strerror(errno));
    }
}

UniqueFd connect_tcp_upstream(const ReverseProxyTarget& upstream, int timeout_seconds)
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* raw_results = nullptr;
    const std::string port = std::to_string(upstream.port);
    const int rc = getaddrinfo(upstream.host.c_str(), port.c_str(), &hints, &raw_results);
    if (rc != 0) {
        throw std::runtime_error(std::string("upstream address resolution failed: ") + gai_strerror(rc));
    }

    AddrInfoPtr results(raw_results);
    std::string last_error = "no upstream address";
    for (addrinfo* item = results.get(); item; item = item->ai_next) {
        UniqueFd fd(socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC, item->ai_protocol));
        if (!fd.valid()) {
            last_error = std::strerror(errno);
            continue;
        }

        set_nonblocking(fd.get());
        if (connect(fd.get(), item->ai_addr, item->ai_addrlen) == 0) {
            return fd;
        }

        if (errno != EINPROGRESS) {
            last_error = std::strerror(errno);
            continue;
        }

        if (!wait_fd(fd.get(), POLLOUT, timeout_seconds)) {
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

    throw std::runtime_error("upstream connect failed: " + last_error);
}

bool should_send_sni(std::string_view host)
{
    if (host.empty() || host.find(':') != std::string_view::npos) {
        return false;
    }

    return std::any_of(host.begin(), host.end(), [](unsigned char ch) {
        return std::isalpha(ch) != 0;
    });
}

short ssl_wait_event(SSL* ssl, int result)
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

SslCtxPtr create_upstream_tls_context(bool verify_upstream)
{
    SslCtxPtr context(SSL_CTX_new(TLS_client_method()));
    if (!context) {
        throw std::runtime_error("cannot create upstream TLS context");
    }

    SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION);
    SSL_CTX_set_options(context.get(), SSL_OP_NO_COMPRESSION | SSL_OP_IGNORE_UNEXPECTED_EOF);
    if (verify_upstream) {
        SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(context.get()) != 1) {
            throw std::runtime_error("cannot load default upstream TLS verify paths");
        }
    } else {
        SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
    }
    return context;
}

void configure_upstream_identity_verification(SSL* ssl, const ReverseProxyTarget& upstream)
{
    if (upstream.host.find(':') != std::string::npos || upstream.host.find_first_not_of("0123456789.") == std::string::npos) {
        if (SSL_set1_ipaddr(ssl, upstream.host.c_str()) != 1) {
            throw std::runtime_error("cannot set upstream TLS IP verification target");
        }
    } else if (SSL_set1_dnsname(ssl, upstream.host.c_str()) != 1) {
        throw std::runtime_error("cannot set upstream TLS hostname verification target");
    }
}

void connect_tls_upstream(UpstreamConnection& connection, const ReverseProxyTarget& upstream, int timeout_seconds, bool verify_upstream)
{
    connection.tls_context = create_upstream_tls_context(verify_upstream);
    connection.tls.reset(SSL_new(connection.tls_context.get()));
    if (!connection.tls) {
        throw std::runtime_error("cannot create upstream TLS connection");
    }
    if (SSL_set_fd(connection.tls.get(), connection.fd.get()) != 1) {
        throw std::runtime_error("cannot attach upstream TLS socket");
    }
    if (should_send_sni(upstream.host)) {
        SSL_set_tlsext_host_name(connection.tls.get(), upstream.host.c_str());
    }
    if (verify_upstream) {
        configure_upstream_identity_verification(connection.tls.get(), upstream);
    }
    SSL_set_connect_state(connection.tls.get());

    while (true) {
        const int result = SSL_connect(connection.tls.get());
        if (result == 1) {
            if (verify_upstream && SSL_get_verify_result(connection.tls.get()) != X509_V_OK) {
                throw std::runtime_error("upstream TLS certificate verification failed");
            }
            return;
        }

        const short event = ssl_wait_event(connection.tls.get(), result);
        if (event == 0 || !wait_fd(connection.fd.get(), event, timeout_seconds)) {
            throw std::runtime_error("upstream TLS handshake failed");
        }
    }
}

UpstreamConnection connect_upstream(const ReverseProxyTarget& upstream, int timeout_seconds, bool verify_tls_upstream)
{
    UpstreamConnection connection;
    connection.fd = connect_tcp_upstream(upstream, timeout_seconds);
    if (upstream.scheme == "https") {
        connect_tls_upstream(connection, upstream, timeout_seconds, verify_tls_upstream);
    }
    return connection;
}

void write_all(int fd, std::string_view payload, int timeout_seconds)
{
    std::size_t sent_total = 0;
    while (sent_total < payload.size()) {
        const ssize_t sent = send(fd, payload.data() + sent_total, payload.size() - sent_total, MSG_NOSIGNAL);
        if (sent > 0) {
            sent_total += static_cast<std::size_t>(sent);
            continue;
        }

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_fd(fd, POLLOUT, timeout_seconds)) {
                throw std::runtime_error("upstream write timeout");
            }
            continue;
        }

        throw std::runtime_error(std::string("upstream write failed: ") + std::strerror(errno));
    }
}

void write_all_tls(SSL* ssl, int fd, std::string_view payload, int timeout_seconds)
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

        const short event = ssl_wait_event(ssl, sent);
        if (event != 0 && wait_fd(fd, event, timeout_seconds)) {
            continue;
        }

        throw std::runtime_error("upstream TLS write failed");
    }
}

void write_all(UpstreamConnection& connection, std::string_view payload, int timeout_seconds)
{
    if (connection.tls) {
        write_all_tls(connection.tls.get(), connection.fd.get(), payload, timeout_seconds);
        return;
    }
    write_all(connection.fd.get(), payload, timeout_seconds);
}

std::string read_until_close(int fd, int timeout_seconds, std::size_t max_bytes)
{
    std::string response;
    std::array<char, 8192> buffer {};

    while (true) {
        const ssize_t received = recv(fd, buffer.data(), buffer.size(), 0);
        if (received > 0) {
            response.append(buffer.data(), static_cast<std::size_t>(received));
            if (response.size() > max_bytes) {
                throw std::runtime_error("upstream response too large");
            }
            continue;
        }

        if (received == 0) {
            return response;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!wait_fd(fd, POLLIN, timeout_seconds)) {
                throw std::runtime_error("upstream read timeout");
            }
            continue;
        }

        throw std::runtime_error(std::string("upstream read failed: ") + std::strerror(errno));
    }
}

std::string read_tls_until_close(SSL* ssl, int fd, int timeout_seconds, std::size_t max_bytes)
{
    std::string response;
    std::array<char, 8192> buffer {};

    while (true) {
        const int received = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
        if (received > 0) {
            response.append(buffer.data(), static_cast<std::size_t>(received));
            if (response.size() > max_bytes) {
                throw std::runtime_error("upstream response too large");
            }
            continue;
        }

        const int error = SSL_get_error(ssl, received);
        if (error == SSL_ERROR_ZERO_RETURN) {
            return response;
        }

        const short event = error == SSL_ERROR_WANT_READ ? POLLIN : (error == SSL_ERROR_WANT_WRITE ? POLLOUT : 0);
        if (event != 0 && wait_fd(fd, event, timeout_seconds)) {
            continue;
        }

        if ((error == SSL_ERROR_SYSCALL || error == SSL_ERROR_SSL) && !response.empty()) {
            return response;
        }

        throw std::runtime_error("upstream TLS read failed");
    }
}

std::string read_until_close(UpstreamConnection& connection, int timeout_seconds, std::size_t max_bytes)
{
    if (connection.tls) {
        return read_tls_until_close(connection.tls.get(), connection.fd.get(), timeout_seconds, max_bytes);
    }
    return read_until_close(connection.fd.get(), timeout_seconds, max_bytes);
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

std::optional<std::string> decode_chunked_body(std::string_view encoded)
{
    std::string body;
    std::size_t cursor = 0;

    while (true) {
        const auto line_end = encoded.find("\r\n", cursor);
        if (line_end == std::string_view::npos) {
            return std::nullopt;
        }

        auto size_line = encoded.substr(cursor, line_end - cursor);
        const auto extension = size_line.find(';');
        if (extension != std::string_view::npos) {
            size_line = size_line.substr(0, extension);
        }

        const auto chunk_size = parse_hex_size(trim(std::string(size_line)));
        if (!chunk_size) {
            return std::nullopt;
        }

        cursor = line_end + 2;
        if (*chunk_size == 0) {
            return body;
        }

        if (encoded.size() < cursor + *chunk_size + 2) {
            return std::nullopt;
        }

        body.append(encoded.substr(cursor, *chunk_size));
        cursor += *chunk_size;
        if (encoded.substr(cursor, 2) != "\r\n") {
            return std::nullopt;
        }
        cursor += 2;
    }
}

std::string upstream_circuit_key(const ReverseProxyTarget& upstream)
{
    std::ostringstream output;
    output << upstream.scheme << "://" << upstream.authority << upstream.base_path;
    return output.str();
}

std::size_t bounded_proxy_attempts(const std::vector<ReverseProxyTarget>& upstreams, std::size_t retry_count)
{
    constexpr std::size_t max_proxy_attempts = 16;
    const auto requested_attempts = std::min(
        retry_count == std::numeric_limits<std::size_t>::max() ? retry_count : retry_count + 1,
        max_proxy_attempts);
    return upstreams.size() == 1 ? requested_attempts : std::min(upstreams.size(), requested_attempts);
}

std::size_t stable_hash_start(const Request& request, std::size_t upstream_count)
{
    std::string key;
    if (const auto host = request.header("host")) {
        key += *host;
    }
    key.push_back('\n');
    key += request.target;

    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : key) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash % upstream_count);
}

} // namespace

std::string build_reverse_proxy_upstream_request(const Request& request, const ReverseProxyTarget& upstream)
{
    std::ostringstream output;
    output << request.method << ' ' << reverse_proxy_target_path(upstream, request.target) << " HTTP/1.1\r\n";
    output << "host: " << upstream.authority << "\r\n";
    output << "connection: close\r\n";

    if (const auto original_host = request.header("host")) {
        output << "x-forwarded-host: " << *original_host << "\r\n";
    }

    for (const auto& [name, value] : request.headers) {
        const std::string normalized_name = lowercase(name);
        if (normalized_name == "host" || normalized_name == "content-length" || is_hop_by_hop_header(normalized_name)) {
            continue;
        }
        output << normalized_name << ": " << value << "\r\n";
    }

    const auto body = request.body_text();
    output << "content-length: " << body.size() << "\r\n";
    output << "\r\n";
    output << body;
    return output.str();
}

Response parse_reverse_proxy_upstream_response(std::string raw_response)
{
    const auto header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("upstream response missing headers");
    }

    std::istringstream headers_input(raw_response.substr(0, header_end));
    std::string status_line;
    if (!std::getline(headers_input, status_line)) {
        throw std::runtime_error("upstream response missing status line");
    }
    if (!status_line.empty() && status_line.back() == '\r') {
        status_line.pop_back();
    }

    std::istringstream status(status_line);
    std::string version;
    int status_code = 0;
    if (!(status >> version >> status_code) || !version.starts_with("HTTP/")) {
        throw std::runtime_error("upstream response status line is invalid");
    }

    std::string reason;
    std::getline(status, reason);
    reason = trim(std::move(reason));
    if (reason.empty()) {
        reason = reason_phrase(status_code);
    }

    Response response;
    response.status = status_code;
    response.reason = reason;

    bool chunked = false;
    std::string line;
    while (std::getline(headers_input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }

        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        std::string name = lowercase(trim(line.substr(0, separator)));
        std::string value = trim(line.substr(separator + 1));
        if (name == "transfer-encoding" && lowercase(value) == "chunked") {
            chunked = true;
            continue;
        }
        if (name == "content-length" || is_hop_by_hop_header(name)) {
            continue;
        }
        response.headers[std::move(name)] = std::move(value);
    }

    const std::string_view raw_body(raw_response.data() + header_end + 4, raw_response.size() - header_end - 4);
    if (chunked) {
        const auto decoded = decode_chunked_body(raw_body);
        if (!decoded) {
            throw std::runtime_error("upstream chunked body is invalid");
        }
        response.body = *decoded;
    } else {
        response.body = std::string(raw_body);
    }

    return response;
}

ReverseProxyLoadBalancingPolicy parse_reverse_proxy_load_balancing_policy(std::string_view value)
{
    auto normalized = lowercase(trim(std::string(value)));
    if (normalized == "round_robin" || normalized == "round-robin") {
        return ReverseProxyLoadBalancingPolicy::round_robin;
    }
    if (normalized == "failover") {
        return ReverseProxyLoadBalancingPolicy::failover;
    }
    if (normalized == "stable_hash" || normalized == "stable-hash") {
        return ReverseProxyLoadBalancingPolicy::stable_hash;
    }
    throw std::runtime_error("reverse_proxy_load_balancing_policy must be round_robin, failover, or stable_hash");
}

std::string_view reverse_proxy_load_balancing_policy_name(ReverseProxyLoadBalancingPolicy policy)
{
    switch (policy) {
    case ReverseProxyLoadBalancingPolicy::round_robin:
        return "round_robin";
    case ReverseProxyLoadBalancingPolicy::failover:
        return "failover";
    case ReverseProxyLoadBalancingPolicy::stable_hash:
        return "stable_hash";
    }
    return "round_robin";
}

std::vector<ReverseProxyTarget> ordered_reverse_proxy_upstreams(
    const Request& request,
    const std::vector<ReverseProxyTarget>& upstreams,
    const ReverseProxySettings& settings)
{
    if (upstreams.empty()) {
        return {};
    }

    std::size_t start = 0;
    if (settings.load_balancing_policy == ReverseProxyLoadBalancingPolicy::round_robin) {
        start = next_proxy_upstream.fetch_add(1, std::memory_order_relaxed) % upstreams.size();
    } else if (settings.load_balancing_policy == ReverseProxyLoadBalancingPolicy::stable_hash) {
        start = stable_hash_start(request, upstreams.size());
    }

    std::vector<ReverseProxyTarget> ordered;
    const auto attempts = bounded_proxy_attempts(upstreams, settings.retry_count);

    if (upstreams.size() == 1) {
        ordered.assign(attempts, upstreams.front());
        return ordered;
    }

    ordered.reserve(attempts);
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        ordered.push_back(upstreams[(start + attempt) % upstreams.size()]);
    }
    return ordered;
}

namespace {

Response reverse_proxy_response(const Request& request, const std::vector<ReverseProxyTarget>& upstreams, const ReverseProxySettings& settings)
{
    bool skipped_open_circuit = false;
    bool attempted_upstream = false;

    for (const auto& upstream : ordered_reverse_proxy_upstreams(request, upstreams, settings)) {
        if (!reverse_proxy_upstream_available(upstream, settings)) {
            skipped_open_circuit = true;
            continue;
        }

        attempted_upstream = true;
        try {
            auto connection = connect_upstream(upstream, settings.connect_timeout_seconds, settings.verify_tls_upstream);
            write_all(connection, build_reverse_proxy_upstream_request(request, upstream), settings.connect_timeout_seconds);
            auto response = parse_reverse_proxy_upstream_response(read_until_close(connection, settings.read_timeout_seconds, settings.max_response_bytes));
            record_reverse_proxy_upstream_success(upstream, settings);
            return response;
        } catch (const std::exception&) {
            record_reverse_proxy_upstream_failure(upstream, settings);
            continue;
        }
    }

    if (!attempted_upstream && skipped_open_circuit) {
        auto response = text_response(503, "Service Unavailable\n");
        response.headers["retry-after"] = std::to_string(settings.circuit_breaker_cooldown_seconds);
        return response;
    }

    return text_response(502, "Bad Gateway\n");
}

class ReverseProxyHandler final : public RequestHandler {
public:
    ReverseProxyHandler(std::vector<ReverseProxyTarget> upstreams, ReverseProxySettings settings)
        : upstreams_(std::move(upstreams))
        , settings_(settings)
    {
    }

    void on_request(const Request& request, ResponseSink& downstream) override
    {
        const auto body_mode = request.method == "HEAD" ? BodyMode::headers_only : BodyMode::include;
        downstream.send(reverse_proxy_response(request, upstreams_, settings_), body_mode);
    }

private:
    std::vector<ReverseProxyTarget> upstreams_;
    ReverseProxySettings settings_;
};

class ServerSideScriptHandler final : public RequestHandler {
public:
    explicit ServerSideScriptHandler(VirtualHostRule rule)
        : rule_(std::move(rule))
    {
    }

    void on_request(const Request&, ResponseSink& downstream) override
    {
        std::ostringstream output;
        output << "{"
               << "\"runtime\":\"" << json_escape(rule_.script_runtime) << "\","
               << "\"script_root\":\"" << json_escape(rule_.script_root.string()) << "\","
               << "\"implemented\":false,"
               << "\"message\":\"Bundled server-side runtime execution is not implemented yet\""
               << "}\n";

        auto response = json_response(501, output.str());
        response.headers["x-rimau-runtime-status"] = "planned";
        downstream.send(std::move(response));
    }

private:
    VirtualHostRule rule_;
};

} // namespace

std::string normalize_host(std::string_view host_header)
{
    std::string host = trim(std::string(host_header));
    if (host.empty()) {
        return {};
    }

    if (host.front() == '[') {
        const auto close = host.find(']');
        if (close != std::string::npos) {
            return lowercase(host.substr(0, close + 1));
        }
    }

    const auto colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon) {
        host.erase(colon);
    }

    return lowercase(host);
}

ReverseProxyTarget parse_reverse_proxy_target(std::string_view value)
{
    std::string text = trim(std::string(value));
    if (contains_control_character(text)) {
        throw std::runtime_error("reverse proxy upstream cannot contain control characters");
    }

    std::string scheme;
    std::uint16_t default_port = 0;
    if (text.starts_with("http://")) {
        scheme = "http";
        default_port = 80;
        text.erase(0, 7);
    } else if (text.starts_with("https://")) {
        scheme = "https";
        default_port = 443;
        text.erase(0, 8);
    } else {
        throw std::runtime_error("reverse proxy upstream must start with http:// or https://");
    }

    const auto path_start = text.find('/');
    const std::string authority = path_start == std::string::npos ? text : text.substr(0, path_start);
    std::string base_path = path_start == std::string::npos ? "/" : text.substr(path_start);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        throw std::runtime_error("reverse proxy upstream authority is invalid");
    }

    std::string host;
    std::uint16_t port = default_port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            throw std::runtime_error("reverse proxy IPv6 upstream host is invalid");
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                throw std::runtime_error("reverse proxy upstream authority is invalid");
            }
            port = parse_upstream_port(std::string_view(authority).substr(close + 2));
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon == std::string::npos) {
            host = authority;
        } else {
            host = authority.substr(0, colon);
            port = parse_upstream_port(std::string_view(authority).substr(colon + 1));
        }
    }

    if (host.empty() || port == 0) {
        throw std::runtime_error("reverse proxy upstream host or port is invalid");
    }
    if (base_path.empty() || base_path.front() != '/') {
        throw std::runtime_error("reverse proxy upstream path must start with /");
    }

    ReverseProxyTarget target;
    target.scheme = std::move(scheme);
    target.host = std::move(host);
    target.port = port;
    target.base_path = std::move(base_path);
    target.authority = authority;
    return target;
}

std::vector<ReverseProxyTarget> parse_reverse_proxy_targets(std::string_view value)
{
    std::vector<ReverseProxyTarget> targets;
    for (const auto& target : split_delimited(value, ',')) {
        targets.push_back(parse_reverse_proxy_target(target));
    }

    if (targets.empty()) {
        throw std::runtime_error("proxy virtual host must include at least one upstream");
    }

    return targets;
}

std::string reverse_proxy_target_path(const ReverseProxyTarget& upstream, std::string_view request_target)
{
    if (upstream.base_path.empty() || upstream.base_path == "/") {
        return std::string(request_target.empty() ? "/" : request_target);
    }

    std::string base = upstream.base_path;
    while (base.size() > 1 && base.back() == '/') {
        base.pop_back();
    }

    if (request_target.empty()) {
        return base;
    }
    if (request_target.front() == '/') {
        return base + std::string(request_target);
    }
    return base + "/" + std::string(request_target);
}

std::vector<VirtualHostRule> parse_virtual_host_rules(std::string_view value)
{
    std::vector<VirtualHostRule> rules;
    for (const auto& entry : split_delimited(value, ';')) {
        const auto equals = entry.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("virtual_hosts entries must use host=action:target");
        }

        VirtualHostRule rule;
        rule.host_pattern = lowercase(trim(entry.substr(0, equals)));
        if (!valid_host_pattern(rule.host_pattern)) {
            throw std::runtime_error("virtual host pattern is invalid: " + rule.host_pattern);
        }

        const std::string action = trim(entry.substr(equals + 1));
        if (action.starts_with("static:")) {
            rule.action = VirtualHostAction::static_files;
            rule.document_root = trim(action.substr(7));
            if (rule.document_root.empty()) {
                throw std::runtime_error("static virtual host document root cannot be empty");
            }
        } else if (action.starts_with("proxy:")) {
            rule.action = VirtualHostAction::reverse_proxy;
            rule.upstreams = parse_reverse_proxy_targets(action.substr(6));
        } else if (action.starts_with("script:")) {
            const auto rest = action.substr(7);
            const auto separator = rest.find(':');
            if (separator == std::string::npos) {
                throw std::runtime_error("script virtual host must use script:runtime:script-root");
            }
            rule.action = VirtualHostAction::server_side_script;
            rule.script_runtime = lowercase(trim(rest.substr(0, separator)));
            rule.script_root = trim(rest.substr(separator + 1));
            if (!valid_runtime_name(rule.script_runtime) || rule.script_root.empty()) {
                throw std::runtime_error("script virtual host runtime or root is invalid");
            }
        } else {
            throw std::runtime_error("virtual host action must be static, proxy, or script");
        }

        rules.push_back(std::move(rule));
    }

    return rules;
}

bool parse_waf_override_bool(const std::string& value, std::string_view key)
{
    const auto normalized = lowercase(trim(value));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    throw std::runtime_error("virtual_host_waf_overrides " + std::string(key) + " must be a boolean");
}

std::size_t parse_waf_override_size(const std::string& value, std::string_view key)
{
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size() || parsed == 0) {
            throw std::runtime_error("virtual_host_waf_overrides " + std::string(key) + " must be greater than zero");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("virtual_host_waf_overrides " + std::string(key) + " must be a number");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("virtual_host_waf_overrides " + std::string(key) + " is out of range");
    }
}

int parse_waf_rule_id(const std::string& value)
{
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size() || parsed <= 0) {
            throw std::runtime_error("virtual_host_waf_overrides rule_exceptions must contain positive rule ids");
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("virtual_host_waf_overrides rule_exceptions must contain numeric rule ids");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("virtual_host_waf_overrides rule_exceptions rule id is out of range");
    }
}

std::vector<VirtualHostWafOverride> parse_virtual_host_waf_overrides(std::string_view value)
{
    std::vector<VirtualHostWafOverride> overrides;
    for (const auto& entry : split_delimited(value, ';')) {
        const auto equals = entry.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("virtual_host_waf_overrides entries must use host=option:value");
        }

        VirtualHostWafOverride override;
        override.host_pattern = lowercase(trim(entry.substr(0, equals)));
        if (!valid_host_pattern(override.host_pattern)) {
            throw std::runtime_error("virtual_host_waf_overrides host pattern is invalid: " + override.host_pattern);
        }

        const auto options = split_delimited(entry.substr(equals + 1), ',');
        if (options.empty()) {
            throw std::runtime_error("virtual_host_waf_overrides entry must include at least one option");
        }

        for (const auto& option : options) {
            const auto separator = option.find(':');
            if (separator == std::string::npos) {
                throw std::runtime_error("virtual_host_waf_overrides options must use key:value");
            }

            const auto key = lowercase(trim(option.substr(0, separator)));
            const auto option_value = trim(option.substr(separator + 1));
            if (option_value.empty()) {
                throw std::runtime_error("virtual_host_waf_overrides option value cannot be empty");
            }

            if (key == "enabled") {
                override.enabled = parse_waf_override_bool(option_value, key);
            } else if (key == "owasp_crs") {
                override.owasp_crs_enabled = parse_waf_override_bool(option_value, key);
            } else if (key == "blocking") {
                override.blocking_enabled = parse_waf_override_bool(option_value, key);
            } else if (key == "threshold") {
                override.anomaly_threshold = parse_waf_override_size(option_value, key);
            } else if (key == "rule_exceptions") {
                override.rule_exceptions.clear();
                for (const auto& id : split_delimited(option_value, '|')) {
                    override.rule_exceptions.push_back(parse_waf_rule_id(id));
                }
                if (override.rule_exceptions.empty()) {
                    throw std::runtime_error("virtual_host_waf_overrides rule_exceptions cannot be empty");
                }
            } else {
                throw std::runtime_error("virtual_host_waf_overrides unknown option: " + key);
            }
        }

        overrides.push_back(std::move(override));
    }

    return overrides;
}

const VirtualHostRule* select_virtual_host_rule(const Request& request, const std::vector<VirtualHostRule>& rules)
{
    const auto host_header = request.header("host");
    if (!host_header) {
        return nullptr;
    }

    const auto host = normalize_host(*host_header);
    if (host.empty()) {
        return nullptr;
    }

    for (const auto& rule : rules) {
        if (!rule.host_pattern.starts_with("*.") && hostname_matches_pattern(host, rule.host_pattern)) {
            return &rule;
        }
    }

    for (const auto& rule : rules) {
        if (rule.host_pattern.starts_with("*.") && hostname_matches_pattern(host, rule.host_pattern)) {
            return &rule;
        }
    }

    return nullptr;
}

const VirtualHostWafOverride* select_virtual_host_waf_override(const Request& request, const std::vector<VirtualHostWafOverride>& overrides)
{
    const auto host_header = request.header("host");
    if (!host_header) {
        return nullptr;
    }

    const auto host = normalize_host(*host_header);
    if (host.empty()) {
        return nullptr;
    }

    for (const auto& override : overrides) {
        if (!override.host_pattern.starts_with("*.") && hostname_matches_pattern(host, override.host_pattern)) {
            return &override;
        }
    }

    for (const auto& override : overrides) {
        if (override.host_pattern.starts_with("*.") && hostname_matches_pattern(host, override.host_pattern)) {
            return &override;
        }
    }

    return nullptr;
}

WafSettings apply_virtual_host_waf_override(WafSettings settings, const VirtualHostWafOverride* override)
{
    if (!override) {
        return settings;
    }

    if (override->enabled) {
        settings.enabled = *override->enabled;
    }
    if (override->owasp_crs_enabled) {
        settings.owasp_crs_enabled = *override->owasp_crs_enabled;
    }
    if (override->blocking_enabled) {
        settings.blocking_enabled = *override->blocking_enabled;
    }
    if (override->anomaly_threshold) {
        settings.anomaly_threshold = *override->anomaly_threshold;
    }
    settings.disabled_rule_ids.insert(settings.disabled_rule_ids.end(), override->rule_exceptions.begin(), override->rule_exceptions.end());
    return settings;
}

bool reverse_proxy_upstream_available(const ReverseProxyTarget& upstream, const ReverseProxySettings& settings)
{
    if (!settings.circuit_breaker_enabled) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(circuit_breaker_mutex);
    const auto found = circuit_breakers.find(upstream_circuit_key(upstream));
    if (found == circuit_breakers.end()) {
        return true;
    }

    if (found->second.opened_until.time_since_epoch().count() == 0) {
        return true;
    }

    if (now < found->second.opened_until) {
        return false;
    }

    circuit_breakers.erase(found);
    return true;
}

void record_reverse_proxy_upstream_success(const ReverseProxyTarget& upstream, const ReverseProxySettings& settings)
{
    if (!settings.circuit_breaker_enabled) {
        return;
    }

    std::lock_guard lock(circuit_breaker_mutex);
    circuit_breakers.erase(upstream_circuit_key(upstream));
}

void record_reverse_proxy_upstream_failure(const ReverseProxyTarget& upstream, const ReverseProxySettings& settings)
{
    if (!settings.circuit_breaker_enabled) {
        return;
    }

    const std::size_t threshold = std::max<std::size_t>(settings.circuit_breaker_failure_threshold, 1);
    std::lock_guard lock(circuit_breaker_mutex);
    auto& entry = circuit_breakers[upstream_circuit_key(upstream)];
    ++entry.failure_count;
    if (entry.failure_count >= threshold) {
        entry.opened_until = std::chrono::steady_clock::now() + std::chrono::seconds(settings.circuit_breaker_cooldown_seconds);
    }
}

VirtualHostHandlerFactory::VirtualHostHandlerFactory(
    std::filesystem::path default_document_root,
    std::string virtual_hosts_config,
    ReverseProxySettings proxy_settings,
    StaticFileOptions static_file_options)
    : default_document_root_(std::move(default_document_root))
    , rules_(parse_virtual_host_rules(virtual_hosts_config))
    , proxy_settings_(proxy_settings)
    , static_file_options_(std::move(static_file_options))
{
}

std::unique_ptr<RequestHandler> VirtualHostHandlerFactory::create(const Request& request) const
{
    const auto* rule = select_virtual_host_rule(request, rules_);
    if (!rule) {
        return std::make_unique<StaticFileHandler>(default_document_root_, static_file_options_);
    }

    if (rule->action == VirtualHostAction::static_files) {
        return std::make_unique<StaticFileHandler>(rule->document_root, static_file_options_);
    }

    if (rule->action == VirtualHostAction::reverse_proxy) {
        return std::make_unique<ReverseProxyHandler>(rule->upstreams, proxy_settings_);
    }

    return std::make_unique<ServerSideScriptHandler>(*rule);
}

} // namespace rimau::http
