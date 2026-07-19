#pragma once

#include "rimau/core/config.hpp"

#include <string>

namespace rimau::protocol {

std::string http2_status();
std::string http2_status(const rimau::core::ServerConfig& config);

} // namespace rimau::protocol
