#include "rimau/http/static_file_handler.hpp"

#include "rimau/http/response.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace rimau::http {
namespace {

constexpr const char* allowed_methods = "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS";

bool is_supported_method(const std::string& method)
{
    return method == "GET" || method == "HEAD" || method == "POST" || method == "PUT"
        || method == "PATCH" || method == "DELETE" || method == "OPTIONS";
}

std::string query_params_json(const Request& request)
{
    std::ostringstream output;
    output << "{";

    bool first_name = true;
    for (const auto& [name, values] : request.query_params) {
        if (!first_name) {
            output << ",";
        }
        first_name = false;
        output << "\"" << json_escape(name) << "\":[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index > 0) {
                output << ",";
            }
            output << "\"" << json_escape(values[index]) << "\"";
        }
        output << "]";
    }

    output << "}";
    return output.str();
}

Response method_json_response(const Request& request)
{
    std::string body;
    body.reserve(std::min<std::size_t>(request.body_size(), 64 * 1024));
    auto reader = request.open_body_reader();
    while (body.size() < 64 * 1024 && !reader.eof()) {
        auto chunk = reader.read_chunk(std::min<std::size_t>(8192, 64 * 1024 - body.size()));
        if (chunk.empty()) {
            break;
        }
        body += std::move(chunk);
    }

    std::ostringstream output;
    output << "{"
           << "\"method\":\"" << json_escape(request.method) << "\","
           << "\"target\":\"" << json_escape(request.target) << "\","
           << "\"path\":\"" << json_escape(request.path) << "\","
           << "\"query\":" << query_params_json(request) << ","
           << "\"body_size\":" << request.body_size() << ","
           << "\"json_request\":" << (request.is_json() ? "true" : "false") << ",";

    output << "\"body\":\"" << json_escape(body) << "\","
           << "\"body_truncated\":" << (request.body_size() > body.size() ? "true" : "false") << ","
           << "\"body_spooled\":" << (request.body_spooled_to_file() ? "true" : "false");
    output << "}\n";

    auto response = json_response(200, output.str());
    response.headers["allow"] = allowed_methods;
    return response;
}

} // namespace

StaticFileHandler::StaticFileHandler(std::filesystem::path document_root, StaticFileOptions options)
    : document_root_(std::move(document_root))
    , options_(std::move(options))
{
}

void StaticFileHandler::on_request(const Request& request, ResponseSink& downstream)
{
    if (!is_supported_method(request.method)) {
        auto response = text_response(405, "Method Not Allowed\n");
        response.headers["allow"] = allowed_methods;
        downstream.send(std::move(response));
        return;
    }

    if (request.method == "OPTIONS") {
        Response response;
        response.status = 204;
        response.reason = reason_phrase(response.status);
        response.headers["allow"] = allowed_methods;
        response.headers["access-control-allow-methods"] = allowed_methods;
        response.headers["access-control-allow-headers"] = "Content-Type, Range, Upgrade, Connection, Sec-WebSocket-Key, Sec-WebSocket-Version";
        downstream.send(std::move(response), BodyMode::headers_only);
        return;
    }

    if (request.method == "GET" || request.method == "HEAD") {
        const auto body_mode = request.method == "HEAD" ? BodyMode::headers_only : BodyMode::include;
        downstream.send(file_response(request, document_root_, options_), body_mode);
        return;
    }

    downstream.send(method_json_response(request));
}

StaticFileHandlerFactory::StaticFileHandlerFactory(std::filesystem::path document_root, StaticFileOptions options)
    : document_root_(std::move(document_root))
    , options_(std::move(options))
{
}

std::unique_ptr<RequestHandler> StaticFileHandlerFactory::create(const Request&) const
{
    return std::make_unique<StaticFileHandler>(document_root_, options_);
}

} // namespace rimau::http
