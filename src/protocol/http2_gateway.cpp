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
    return "implemented: native HTTP/2 h2c and TLS ALPN h2 request serving with frame codec, SETTINGS/PING/RST/GOAWAY handling, HPACK Huffman and dynamic table decoding, CONTINUATION assembly, stream lifecycle basics, inbound flow-control accounting, and real-client curl coverage";
}

std::string http2_status(const rimau::core::ServerConfig& config)
{
    if (!config.http2_enabled) {
        return "disabled in SQLite config; native HTTP/2 h2c and TLS ALPN h2 request serving exist but are not enabled";
    }

    if (!config.tls_enabled) {
        return "enabled in SQLite config with native h2c prior-knowledge request serving: Rimau accepts client preface, parses SETTINGS/HEADERS/CONTINUATION/DATA, decodes HPACK Huffman/dynamic table entries, applies inbound flow-control accounting, and responds with HTTP/2 HEADERS/DATA frames through the shared handler pipeline";
    }

    if (alpn_contains(config.tls_alpn_protocols, "h2")) {
        return "enabled in SQLite config with native TLS ALPN h2 request serving: Rimau negotiates h2, accepts the client preface, parses SETTINGS/HEADERS/CONTINUATION/DATA, decodes HPACK Huffman/dynamic table entries, applies inbound flow-control accounting, and responds with HTTP/2 HEADERS/DATA frames through the shared handler pipeline";
    }

    return "enabled in SQLite config, but TLS ALPN h2 is not advertised by tls_alpn_protocols; cleartext h2c request serving exists only when TLS is disabled";
}

} // namespace rimau::protocol
