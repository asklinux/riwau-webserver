#pragma once

#include "rimau/http/response.hpp"
#include "rimau/http/response_sink.hpp"

#include <string>

namespace rimau::http {

class ResponseBuilder {
public:
    explicit ResponseBuilder(ResponseSink& downstream);

    ResponseBuilder& status(int status);
    ResponseBuilder& status(int status, std::string reason);
    ResponseBuilder& header(std::string name, std::string value);
    ResponseBuilder& body(std::string body);
    ResponseBuilder& content_type(std::string value);

    void send();
    void send_without_body();

private:
    ResponseSink& downstream_;
    Response response_;
};

} // namespace rimau::http
