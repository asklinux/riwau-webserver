#pragma once

#include "rimau/http/response.hpp"
#include "rimau/http/response_sink.hpp"
#include "rimau/protocol/http2_frame.hpp"
#include "rimau/protocol/http2_hpack.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rimau::protocol::http2 {

inline constexpr std::uint8_t flag_end_stream = 0x1;
inline constexpr std::uint8_t flag_end_headers = 0x4;
inline constexpr std::uint8_t flag_padded = 0x8;
inline constexpr std::uint8_t flag_priority = 0x20;

inline constexpr std::uint32_t error_no_error = 0x0;
inline constexpr std::uint32_t error_protocol = 0x1;
inline constexpr std::uint32_t error_internal = 0x2;
inline constexpr std::uint32_t error_flow_control = 0x3;
inline constexpr std::uint32_t error_stream_closed = 0x5;
inline constexpr std::uint32_t error_compression = 0x9;
inline constexpr std::uint32_t error_enhance_your_calm = 0xb;
inline constexpr std::uint32_t error_http_1_1_required = 0xd;

struct CompletedStream {
    std::uint32_t stream_id = 0;
    rimau::http::Request request;
};

enum class PrefaceStatus {
    not_http2,
    need_more,
    accepted,
    rejected
};

struct PrefaceResult {
    PrefaceStatus status = PrefaceStatus::not_http2;
    std::string output;
    bool keep_connection = true;
};

struct SessionResult {
    std::string output;
    bool keep_connection = true;
    bool close_now = false;
    std::optional<CompletedStream> completed_stream;
    std::optional<rimau::http::Response> error_response;
    std::uint32_t error_stream_id = 0;
    std::string warning;
};

class ServerSession {
public:
    void reset();
    bool active() const noexcept;

    PrefaceResult accept_preface(
        std::string& input_buffer,
        std::size_t max_request_bytes,
        bool http2_enabled,
        bool request_allowed);

    SessionResult process_input(std::string& input_buffer, std::size_t max_request_bytes);
    void reset_stream(std::uint32_t stream_id);

private:
    enum class StreamLifecycle {
        open,
        half_closed_remote
    };

    struct StreamState {
        std::uint32_t id = 0;
        bool headers_received = false;
        StreamLifecycle lifecycle = StreamLifecycle::open;
        std::int32_t inbound_window = 65535;
        std::vector<HeaderField> headers;
        std::string body;
    };

    struct PendingHeaderBlock {
        std::uint32_t stream_id = 0;
        bool end_stream = false;
        std::vector<std::uint8_t> block;
    };

    struct RequestBuildResult {
        std::optional<rimau::http::Request> request;
        rimau::http::Response error_response;
    };

    SessionResult handle_frame(const Frame& frame, std::size_t max_request_bytes);
    SessionResult handle_headers_frame(const Frame& frame, std::size_t max_request_bytes);
    SessionResult handle_continuation_frame(const Frame& frame, std::size_t max_request_bytes);
    SessionResult decode_headers(std::uint32_t stream_id, bool end_stream, std::vector<std::uint8_t> block);
    SessionResult handle_data_frame(const Frame& frame, std::size_t max_request_bytes);
    SessionResult fail_connection(std::uint32_t error_code, std::string_view debug_data);
    SessionResult reset_stream_result(std::uint32_t stream_id, std::uint32_t error_code);
    RequestBuildResult build_request(const StreamState& stream) const;
    SessionResult complete_stream(std::uint32_t stream_id);

    bool active_ = false;
    std::uint32_t last_client_stream_id_ = 0;
    std::int32_t connection_inbound_window_ = 65535;
    HpackDecoder decoder_;
    std::optional<PendingHeaderBlock> pending_headers_;
    std::unordered_map<std::uint32_t, StreamState> streams_;
};

std::string goaway_frame(std::uint32_t error_code, std::string_view debug_data);
std::string serialize_response(
    std::uint32_t stream_id,
    const rimau::http::Response& response,
    rimau::http::BodyMode body_mode,
    const rimau::http::Response::SerializationOptions& options);

} // namespace rimau::protocol::http2
