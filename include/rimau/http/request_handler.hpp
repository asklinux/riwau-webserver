#pragma once

#include "rimau/http/request.hpp"
#include "rimau/http/response_sink.hpp"

namespace rimau::http {

class RequestHandler {
public:
    virtual ~RequestHandler() = default;

    virtual void on_request(const Request& request, ResponseSink& downstream) = 0;
    virtual void on_complete() {}
    virtual void on_error(const std::string& message, ResponseSink& downstream);
};

} // namespace rimau::http
