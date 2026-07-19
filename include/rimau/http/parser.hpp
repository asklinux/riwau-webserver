#pragma once

#include "rimau/http/request.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace rimau::http {

enum class ParseError {
    none,
    empty_request,
    malformed_request_line,
    malformed_header,
    malformed_body,
    unsupported_http_version
};

struct ParseResult {
    std::optional<Request> request;
    ParseError error = ParseError::none;
    std::string message;

    explicit operator bool() const noexcept
    {
        return request.has_value();
    }
};

ParseResult parse_request(std::string_view raw_request);

} // namespace rimau::http
