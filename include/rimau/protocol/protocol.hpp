#pragma once

#include "rimau/core/config.hpp"

#include <string>
#include <vector>

namespace rimau::protocol {

enum class ProtocolVersion {
    http_1_1,
    http_2,
    http_3
};

struct ProtocolCapability {
    ProtocolVersion version;
    std::string name;
    bool implemented;
    bool enabled;
    std::string transport;
    std::string notes;
};

std::vector<ProtocolCapability> protocol_capabilities();
std::vector<ProtocolCapability> protocol_capabilities(const rimau::core::ServerConfig& config);

} // namespace rimau::protocol
