#pragma once

#include "rimau/http/request.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rimau::http {

struct Response {
    int status = 200;
    std::string reason = "OK";
    Headers headers;
    std::string body;

    struct SerializationOptions {
        bool server_header_enabled = true;
        std::string server_name = "Rimau Web Server";
        Headers default_headers;
    };

    std::string to_http_string(bool include_body = true) const;
    std::string to_http_string(bool include_body, const SerializationOptions& options) const;
    std::string to_http_chunked_string(const std::vector<std::string>& chunks, bool include_body = true) const;
    std::string to_http_chunked_string(const std::vector<std::string>& chunks, bool include_body, const SerializationOptions& options) const;
};

struct StaticFileOptions {
    std::string directory_index = "index.html";
    std::filesystem::path error_page;
};

std::string encode_chunked_body(const std::vector<std::string>& chunks);
std::string reason_phrase(int status);
Response text_response(int status, std::string body, std::string content_type = "text/plain; charset=utf-8");
Response json_response(int status, std::string json_body);
std::string json_escape(std::string_view value);
Response file_response(const Request& request, const std::filesystem::path& document_root);
Response file_response(const Request& request, const std::filesystem::path& document_root, const StaticFileOptions& options);

} // namespace rimau::http
