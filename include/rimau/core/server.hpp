#pragma once

#include "rimau/core/config.hpp"

namespace rimau::core {

class Server {
public:
    explicit Server(ServerConfig config);

    int run();

private:
    ServerConfig config_;
};

} // namespace rimau::core
