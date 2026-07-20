#include "rimau/http/response_sink.hpp"

#include <utility>

namespace rimau::http {

void ResponseSink::send_chunked(Response response, std::vector<std::string> chunks, BodyMode body_mode)
{
    for (auto& chunk : chunks) {
        response.body += chunk;
    }
    send(std::move(response), body_mode);
}

} // namespace rimau::http
