#include "rimau/protocol/http3_gateway.hpp"

namespace rimau::protocol {

std::string http3_status()
{
    return "partial: HTTP/3 QUIC varint, frame, and SETTINGS payload codecs exist; UDP/QUIC transport, QPACK, TLS 1.3 handshake, and ALPN h3 are not served yet";
}

std::string http3_status(const rimau::core::ServerConfig& config)
{
    if (!config.http3_enabled) {
        return "disabled in SQLite config; HTTP/3 wire codec primitives exist but UDP/QUIC live serving is not integrated yet";
    }

    return "enabled in SQLite config but unavailable for clients: HTTP/3 frame codec exists, but UDP listener, QUIC transport, QPACK, TLS 1.3 handshake, and ALPN h3 routing are still pending";
}

} // namespace rimau::protocol
