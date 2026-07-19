#pragma once

#include "rimau/http/request.hpp"
#include "rimau/http/request_handler.hpp"

#include <memory>

namespace rimau::http {

class RequestHandlerFactory {
public:
    virtual ~RequestHandlerFactory() = default;

    virtual void on_server_start() {}
    virtual void on_server_stop() {}
    virtual std::unique_ptr<RequestHandler> create(const Request& request) const = 0;
};

} // namespace rimau::http
