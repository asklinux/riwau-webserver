#include "rimau/core/config.hpp"
#include "rimau/protocol/http2_gateway.hpp"
#include "rimau/protocol/http3_gateway.hpp"
#include "rimau/protocol/protocol.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace {

const rimau::protocol::ProtocolCapability& find_capability(
    const std::vector<rimau::protocol::ProtocolCapability>& capabilities,
    rimau::protocol::ProtocolVersion version)
{
    const auto found = std::find_if(capabilities.begin(), capabilities.end(), [&](const auto& capability) {
        return capability.version == version;
    });
    assert(found != capabilities.end());
    return *found;
}

} // namespace

int main()
{
    rimau::core::ServerConfig config;
    const auto defaults = rimau::protocol::protocol_capabilities(config);

    const auto& http1 = find_capability(defaults, rimau::protocol::ProtocolVersion::http_1_1);
    assert(http1.implemented);
    assert(http1.enabled);
    assert(http1.transport == "TCP");
    assert(http1.notes.find("chunked decode") != std::string::npos);
    assert(http1.notes.find("WebSocket") != std::string::npos);
    assert(http1.notes.find("reverse proxy tunneling") != std::string::npos);
    assert(http1.notes.find("security controls") != std::string::npos);

    const auto& http2 = find_capability(defaults, rimau::protocol::ProtocolVersion::http_2);
    assert(!http2.implemented);
    assert(!http2.enabled);
    assert(http2.transport.find("h2c") != std::string::npos);
    assert(http2.transport.find("partial") != std::string::npos);
    assert(rimau::protocol::http2_status(config).find("disabled in SQLite config") != std::string::npos);

    const auto& http3 = find_capability(defaults, rimau::protocol::ProtocolVersion::http_3);
    assert(!http3.implemented);
    assert(!http3.enabled);
    assert(http3.transport.find("ALPN h3") != std::string::npos);
    assert(http3.transport.find("wire codec partial") != std::string::npos);
    assert(rimau::protocol::http3_status(config).find("disabled in SQLite config") != std::string::npos);

    config.tls_enabled = true;
    config.http2_enabled = true;
    config.http3_enabled = true;
    config.tls_alpn_protocols = "h2,http/1.1";

    const auto enabled = rimau::protocol::protocol_capabilities(config);
    const auto& enabled_http1 = find_capability(enabled, rimau::protocol::ProtocolVersion::http_1_1);
    assert(enabled_http1.transport == "TCP with TLS");

    const auto& enabled_http2 = find_capability(enabled, rimau::protocol::ProtocolVersion::http_2);
    assert(!enabled_http2.implemented);
    assert(enabled_http2.enabled);
    assert(enabled_http2.notes.find("partial") != std::string::npos);
    assert(enabled_http2.notes.find("ALPN h2") != std::string::npos);

    const auto& enabled_http3 = find_capability(enabled, rimau::protocol::ProtocolVersion::http_3);
    assert(!enabled_http3.implemented);
    assert(enabled_http3.enabled);
    assert(enabled_http3.notes.find("UDP listener") != std::string::npos);

    return 0;
}
