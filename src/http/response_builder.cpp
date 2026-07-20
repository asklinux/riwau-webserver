#include "rimau/http/response_builder.hpp"

#include <utility>

namespace rimau::http {

ResponseBuilder::ResponseBuilder(ResponseSink& downstream)
    : downstream_(downstream)
{
}

ResponseBuilder& ResponseBuilder::status(int status)
{
    response_.status = status;
    response_.reason = reason_phrase(status);
    return *this;
}

ResponseBuilder& ResponseBuilder::status(int status, std::string reason)
{
    response_.status = status;
    response_.reason = std::move(reason);
    return *this;
}

ResponseBuilder& ResponseBuilder::header(std::string name, std::string value)
{
    response_.headers[std::move(name)] = std::move(value);
    return *this;
}

ResponseBuilder& ResponseBuilder::body(std::string body)
{
    response_.body = std::move(body);
    return *this;
}

ResponseBuilder& ResponseBuilder::content_type(std::string value)
{
    response_.headers["content-type"] = std::move(value);
    return *this;
}

void ResponseBuilder::send()
{
    downstream_.send(std::move(response_), BodyMode::include);
}

void ResponseBuilder::send_without_body()
{
    downstream_.send(std::move(response_), BodyMode::headers_only);
}

void ResponseBuilder::send_chunked(std::vector<std::string> chunks)
{
    downstream_.send_chunked(std::move(response_), std::move(chunks), BodyMode::include);
}

} // namespace rimau::http
