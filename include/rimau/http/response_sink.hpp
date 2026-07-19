#pragma once

#include "rimau/http/response.hpp"

namespace rimau::http {

enum class BodyMode {
    include,
    headers_only
};

class ResponseSink {
public:
    virtual ~ResponseSink() = default;

    virtual void send(Response response, BodyMode body_mode = BodyMode::include) = 0;
    virtual bool sent() const noexcept = 0;
};

} // namespace rimau::http
