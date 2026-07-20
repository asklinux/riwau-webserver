#include "rimau/protocol/protocol.hpp"

#include "rimau/protocol/http2_gateway.hpp"
#include "rimau/protocol/http3_gateway.hpp"

namespace rimau::protocol {

std::vector<ProtocolCapability> protocol_capabilities()
{
    return protocol_capabilities(rimau::core::ServerConfig {});
}

std::vector<ProtocolCapability> protocol_capabilities(const rimau::core::ServerConfig& config)
{
    const std::string http1_transport = config.tls_enabled ? "TCP with TLS" : "TCP";
    return {
        {
            ProtocolVersion::http_1_1,
            "HTTP/1.1",
            true,
            config.http1_enabled,
            http1_transport,
            "HTTP/1.1 supports GET/HEAD static files, OPTIONS, POST/PUT/PATCH/DELETE JSON scaffold, Content-Length, chunked decode, file-backed large-body spooling, keep-alive, basic pipelining, single range, gzip, basic WebSocket echo, WebSocket reverse proxy tunneling, and baseline timeout/rate-limit/security controls."
        },
        {
            ProtocolVersion::http_2,
            "HTTP/2",
            false,
            config.http2_enabled,
            "TCP h2c prior-knowledge partial; TLS over TCP with ALPN h2 partial",
            http2_status(config)
        },
        {
            ProtocolVersion::http_3,
            "HTTP/3",
            false,
            config.http3_enabled,
            "QUIC over UDP with ALPN h3; wire codec partial",
            http3_status(config)
        },
    };
}

} // namespace rimau::protocol
