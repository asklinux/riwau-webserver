#include "rimau/protocol/http2_gateway.hpp"

#include <string_view>

namespace rimau::protocol {
namespace {

bool alpn_contains(std::string_view protocols, std::string_view expected)
{
    std::size_t start = 0;
    while (start <= protocols.size()) {
        const auto comma = protocols.find(',', start);
        const auto end = comma == std::string_view::npos ? protocols.size() : comma;
        auto token = protocols.substr(start, end - start);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
            token.remove_prefix(1);
        }
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
            token.remove_suffix(1);
        }
        if (token == expected) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

} // namespace

std::string http2_status()
{
    return "partial: HTTP/2 frame codec, SETTINGS/PING handling, non-Huffman static-table HPACK basics, cleartext h2c request serving, and TLS ALPN h2 request serving exist; production stream/session flow control is not complete";
}

std::string http2_status(const rimau::core::ServerConfig& config)
{
    if (!config.http2_enabled) {
        return "disabled in SQLite config; HTTP/2 frame codec and h2c request serving exist but are not enabled";
    }

    if (!config.tls_enabled) {
        return "enabled in SQLite config with partial h2c prior-knowledge request serving: Rimau accepts client preface, parses SETTINGS/HEADERS/DATA, and responds with HTTP/2 HEADERS/DATA frames for the shared handler pipeline";
    }

    if (alpn_contains(config.tls_alpn_protocols, "h2")) {
        return "enabled in SQLite config with partial TLS ALPN h2 request serving: Rimau negotiates h2, accepts the client preface, parses SETTINGS/HEADERS/DATA, and responds with HTTP/2 HEADERS/DATA frames for the shared handler pipeline";
    }

    return "enabled in SQLite config, but TLS ALPN h2 is not advertised by tls_alpn_protocols; cleartext h2c request serving exists only when TLS is disabled";
}

} // namespace rimau::protocol
