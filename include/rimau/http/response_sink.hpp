#pragma once

#include "rimau/http/response.hpp"

#include <vector>

namespace rimau::http {

enum class BodyMode {
    include,
    headers_only
};

class ResponseSink {
public:
    virtual ~ResponseSink() = default;

    virtual void send(Response response, BodyMode body_mode = BodyMode::include) = 0;
    virtual void send_chunked(Response response, std::vector<std::string> chunks, BodyMode body_mode = BodyMode::include);
    virtual bool sent() const noexcept = 0;
};

} // namespace rimau::http
