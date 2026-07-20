#pragma once

#include "rimau/http/request.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rimau::http {

enum class Http1FrameState {
    incomplete,
    header_complete,
    complete,
    error
};

struct Http1FrameOptions {
    std::size_t max_request_bytes = 64 * 1024;
};

struct Http1FrameResult {
    Http1FrameState state = Http1FrameState::incomplete;
    std::optional<Request> request;
    std::string raw_request;
    std::size_t consumed = 0;
    bool waiting_for_body = false;
    bool discard_buffer = false;
    int error_status = 400;
    std::string error_message = "Bad Request\n";
};

Http1FrameResult next_http1_request_frame(std::string_view buffered, const Http1FrameOptions& options);

} // namespace rimau::http
