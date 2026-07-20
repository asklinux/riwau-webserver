#include "rimau/protocol/http2_session.hpp"

#include "rimau/http/parser.hpp"
#include "rimau/protocol/http2_frame.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rimau::protocol::http2 {
namespace {

std::string bytes_to_string(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty()) {
        return {};
    }
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

std::vector<std::uint8_t> u32_payload(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>((value >> 24) & 0xffU),
        static_cast<std::uint8_t>((value >> 16) & 0xffU),
        static_cast<std::uint8_t>((value >> 8) & 0xffU),
        static_cast<std::uint8_t>(value & 0xffU),
    };
}

std::string frame_string(Frame frame)
{
    return bytes_to_string(serialize_frame(frame));
}

std::string settings_frame(bool ack)
{
    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::settings);
    frame.flags = ack ? 0x1 : 0x0;
    frame.stream_id = 0;
    return frame_string(std::move(frame));
}

std::string rst_stream_frame(std::uint32_t stream_id, std::uint32_t error_code)
{
    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::rst_stream);
    frame.stream_id = stream_id;
    frame.payload = u32_payload(error_code);
    return frame_string(std::move(frame));
}

std::string ping_frame(std::vector<std::uint8_t> payload, bool ack)
{
    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::ping);
    frame.flags = ack ? 0x1 : 0x0;
    frame.stream_id = 0;
    frame.payload = std::move(payload);
    return frame_string(std::move(frame));
}

std::string window_update_frame(std::uint32_t stream_id, std::uint32_t increment)
{
    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::window_update);
    frame.stream_id = stream_id;
    frame.payload = u32_payload(increment & 0x7fffffffU);
    return frame_string(std::move(frame));
}

std::string headers_frame(std::uint32_t stream_id, const std::vector<HeaderField>& headers, bool end_stream)
{
    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::headers);
    frame.flags = flag_end_headers | (end_stream ? flag_end_stream : 0);
    frame.stream_id = stream_id;
    frame.payload = hpack_encode_header_block(headers);
    return frame_string(std::move(frame));
}

std::string data_frame(std::uint32_t stream_id, std::string_view payload, bool end_stream)
{
    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::data);
    frame.flags = end_stream ? flag_end_stream : 0;
    frame.stream_id = stream_id;
    frame.payload.assign(payload.begin(), payload.end());
    return frame_string(std::move(frame));
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool valid_header_name(std::string_view name)
{
    if (name.empty()) {
        return false;
    }

    for (const unsigned char ch : name) {
        const bool token_char = std::isalnum(ch) || ch == '!' || ch == '#' || ch == '$' || ch == '%'
            || ch == '&' || ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.'
            || ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~';
        if (!token_char) {
            return false;
        }
    }

    return true;
}

std::optional<std::size_t> parse_decimal_size(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    std::size_t parsed = 0;
    for (const unsigned char ch : value) {
        if (!std::isdigit(ch)) {
            return std::nullopt;
        }
        const auto digit = static_cast<std::size_t>(ch - '0');
        if (parsed > (static_cast<std::size_t>(-1) - digit) / 10U) {
            return std::nullopt;
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

bool header_name_has_uppercase(std::string_view name)
{
    return std::any_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isupper(ch) != 0;
    });
}

bool header_value_has_invalid_control(std::string_view value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch != '\t' && (ch < 0x20 || ch == 0x7f);
    });
}

bool forbidden_request_header(std::string_view name, std::string_view value)
{
    if (name == "te") {
        return lowercase(std::string(value)) != "trailers";
    }

    return name == "connection"
        || name == "keep-alive"
        || name == "proxy-connection"
        || name == "transfer-encoding"
        || name == "upgrade";
}

bool forbidden_response_header(std::string_view name)
{
    return name == "connection"
        || name == "keep-alive"
        || name == "proxy-authenticate"
        || name == "proxy-authorization"
        || name == "te"
        || name == "trailer"
        || name == "transfer-encoding"
        || name == "upgrade";
}

std::string sanitize_header_value(std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch == '\r' || ch == '\n') {
            sanitized.push_back(' ');
        } else if (ch == '\t' || ch >= 0x20) {
            sanitized.push_back(static_cast<char>(ch));
        }
    }
    return sanitized;
}

std::optional<std::vector<std::uint8_t>> header_block_payload(const Frame& frame)
{
    std::size_t cursor = 0;
    std::size_t end = frame.payload.size();

    if ((frame.flags & flag_padded) != 0) {
        if (frame.payload.empty()) {
            return std::nullopt;
        }
        const auto padding = frame.payload[cursor++];
        if (padding > frame.payload.size() - cursor) {
            return std::nullopt;
        }
        end -= padding;
    }

    if ((frame.flags & flag_priority) != 0) {
        if (end - cursor < 5) {
            return std::nullopt;
        }
        cursor += 5;
    }

    if (cursor > end) {
        return std::nullopt;
    }

    return std::vector<std::uint8_t>(frame.payload.begin() + static_cast<std::ptrdiff_t>(cursor), frame.payload.begin() + static_cast<std::ptrdiff_t>(end));
}

std::optional<std::vector<std::uint8_t>> data_payload(const Frame& frame)
{
    std::size_t cursor = 0;
    std::size_t end = frame.payload.size();
    if ((frame.flags & flag_padded) != 0) {
        if (frame.payload.empty()) {
            return std::nullopt;
        }
        const auto padding = frame.payload[cursor++];
        if (padding > frame.payload.size() - cursor) {
            return std::nullopt;
        }
        end -= padding;
    }

    if (cursor > end) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(frame.payload.begin() + static_cast<std::ptrdiff_t>(cursor), frame.payload.begin() + static_cast<std::ptrdiff_t>(end));
}

bool response_may_have_body(int status)
{
    return status >= 200 && status != 204 && status != 304;
}

rimau::http::Response apply_response_defaults(
    const rimau::http::Response& input,
    const rimau::http::Response::SerializationOptions& options)
{
    auto response = input;
    if (response.status != 101) {
        for (const auto& [name, value] : options.default_headers) {
            if (!response.headers.contains(name)) {
                response.headers[name] = value;
            }
        }
    }

    if (response.status != 101 && response.status != 204 && !response.headers.contains("transfer-encoding")) {
        response.headers["content-length"] = std::to_string(response.body.size());
    }

    if (options.server_header_enabled) {
        response.headers["server"] = options.server_name.empty() ? "Rimau Web Server" : options.server_name;
    } else {
        response.headers.erase("server");
    }
    return response;
}

} // namespace

std::string goaway_frame(std::uint32_t error_code, std::string_view debug_data)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(8 + debug_data.size());
    const auto last_stream_id = u32_payload(0);
    const auto error = u32_payload(error_code);
    payload.insert(payload.end(), last_stream_id.begin(), last_stream_id.end());
    payload.insert(payload.end(), error.begin(), error.end());
    payload.insert(payload.end(), debug_data.begin(), debug_data.end());

    Frame frame;
    frame.type = static_cast<std::uint8_t>(FrameType::goaway);
    frame.stream_id = 0;
    frame.payload = std::move(payload);
    return bytes_to_string(serialize_frame(frame));
}

std::string serialize_response(
    std::uint32_t stream_id,
    const rimau::http::Response& input,
    rimau::http::BodyMode body_mode,
    const rimau::http::Response::SerializationOptions& options)
{
    const auto response = apply_response_defaults(input, options);
    const bool include_body = body_mode == rimau::http::BodyMode::include && response_may_have_body(response.status);

    std::vector<HeaderField> headers;
    headers.push_back({ ":status", std::to_string(response.status) });
    for (const auto& [raw_name, raw_value] : response.headers) {
        std::string name = lowercase(raw_name);
        if (!valid_header_name(name) || forbidden_response_header(name)) {
            continue;
        }
        headers.push_back({ std::move(name), sanitize_header_value(raw_value) });
    }

    std::string output;
    output += headers_frame(stream_id, headers, !include_body || response.body.empty());
    if (!include_body || response.body.empty()) {
        return output;
    }

    for (std::size_t offset = 0; offset < response.body.size();) {
        const auto remaining = response.body.size() - offset;
        const auto chunk_size = std::min<std::size_t>(remaining, default_max_frame_size);
        const bool end_stream = offset + chunk_size == response.body.size();
        output += data_frame(stream_id, std::string_view(response.body).substr(offset, chunk_size), end_stream);
        offset += chunk_size;
    }
    return output;
}

void ServerSession::reset()
{
    active_ = false;
    last_client_stream_id_ = 0;
    connection_inbound_window_ = 65535;
    decoder_.reset();
    pending_headers_.reset();
    streams_.clear();
}

bool ServerSession::active() const noexcept
{
    return active_;
}

PrefaceResult ServerSession::accept_preface(
    std::string& input_buffer,
    std::size_t max_request_bytes,
    bool http2_enabled,
    bool request_allowed)
{
    const auto preface = client_connection_preface;
    if (input_buffer.empty()) {
        return {};
    }

    const auto comparable = std::min(input_buffer.size(), preface.size());
    if (std::string_view(input_buffer).substr(0, comparable) != preface.substr(0, comparable)) {
        return {};
    }

    if (input_buffer.size() < preface.size()) {
        return { PrefaceStatus::need_more, {}, true };
    }

    if (input_buffer.size() > max_request_bytes) {
        input_buffer.clear();
        return { PrefaceStatus::rejected, goaway_frame(error_protocol, "HTTP/2 preface exceeded Rimau request limit"), false };
    }

    if (input_buffer.size() < preface.size() + 9) {
        return { PrefaceStatus::need_more, {}, true };
    }

    const auto frame_bytes = std::string_view(input_buffer).substr(preface.size());
    const auto parsed = parse_frame(frame_bytes);
    if (parsed.incomplete) {
        return { PrefaceStatus::need_more, {}, true };
    }
    if (!parsed.ok || parsed.frame.type != static_cast<std::uint8_t>(FrameType::settings) || parsed.frame.stream_id != 0) {
        input_buffer.clear();
        return { PrefaceStatus::rejected, goaway_frame(error_protocol, "Rimau expected an HTTP/2 SETTINGS frame after the preface"), false };
    }

    try {
        (void)parse_settings_payload(parsed.frame.payload);
    } catch (const std::exception& error) {
        input_buffer.clear();
        return { PrefaceStatus::rejected, goaway_frame(error_protocol, error.what()), false };
    }

    input_buffer.erase(0, preface.size() + parsed.consumed);

    if (!http2_enabled) {
        return { PrefaceStatus::rejected, goaway_frame(error_http_1_1_required, "HTTP/2 is disabled in Rimau SQLite config"), false };
    }

    if (!request_allowed) {
        return { PrefaceStatus::rejected, goaway_frame(error_enhance_your_calm, "Rimau HTTP/2 rate limit exceeded"), false };
    }

    active_ = true;
    std::string response;
    response += settings_frame(false);
    response += settings_frame(true);
    return { PrefaceStatus::accepted, std::move(response), true };
}

SessionResult ServerSession::process_input(std::string& input_buffer, std::size_t max_request_bytes)
{
    while (active_ && !input_buffer.empty()) {
        if (input_buffer.size() > max_request_bytes) {
            input_buffer.clear();
            return fail_connection(error_enhance_your_calm, "HTTP/2 frame buffer exceeded Rimau request limit");
        }

        const auto parsed = parse_frame(std::string_view(input_buffer));
        if (parsed.incomplete) {
            return {};
        }
        if (!parsed.ok) {
            input_buffer.clear();
            return fail_connection(error_protocol, parsed.error.empty() ? "invalid HTTP/2 frame" : parsed.error);
        }

        input_buffer.erase(0, parsed.consumed);
        auto result = handle_frame(parsed.frame, max_request_bytes);
        if (!result.output.empty() || result.close_now || result.completed_stream || result.error_response) {
            return result;
        }
    }

    return {};
}

void ServerSession::reset_stream(std::uint32_t stream_id)
{
    streams_.erase(stream_id);
}

SessionResult ServerSession::handle_frame(const Frame& frame, std::size_t max_request_bytes)
{
    const auto type = static_cast<FrameType>(frame.type);
    if (pending_headers_ && type != FrameType::continuation) {
        return fail_connection(error_protocol, "HTTP/2 expected CONTINUATION before any other frame");
    }

    switch (type) {
    case FrameType::settings:
        if ((frame.flags & 0x1) == 0) {
            try {
                const auto settings = parse_settings_payload(frame.payload);
                for (const auto& setting : settings) {
                    if (setting.identifier == SettingIdentifier::header_table_size) {
                        decoder_.set_max_table_size(setting.value);
                    }
                }
            } catch (const std::exception& error) {
                return fail_connection(error_protocol, error.what());
            }
            SessionResult result;
            result.output = settings_frame(true);
            return result;
        }
        return {};

    case FrameType::ping:
        if ((frame.flags & 0x1) == 0) {
            SessionResult result;
            result.output = ping_frame(frame.payload, true);
            return result;
        }
        return {};

    case FrameType::goaway:
        {
            SessionResult result;
            result.keep_connection = false;
            result.close_now = true;
            return result;
        }

    case FrameType::rst_stream:
        streams_.erase(frame.stream_id);
        if (pending_headers_ && pending_headers_->stream_id == frame.stream_id) {
            pending_headers_.reset();
        }
        return {};

    case FrameType::window_update:
    case FrameType::priority:
        return {};

    case FrameType::headers:
        return handle_headers_frame(frame, max_request_bytes);

    case FrameType::data:
        return handle_data_frame(frame, max_request_bytes);

    case FrameType::continuation:
        return handle_continuation_frame(frame, max_request_bytes);

    case FrameType::push_promise:
        return fail_connection(error_protocol, "HTTP/2 clients must not send PUSH_PROMISE");
    }

    return {};
}

SessionResult ServerSession::handle_headers_frame(const Frame& frame, std::size_t max_request_bytes)
{
    if (pending_headers_) {
        return fail_connection(error_protocol, "HTTP/2 HEADERS received before pending CONTINUATION sequence ended");
    }

    if ((frame.stream_id % 2U) == 0) {
        return fail_connection(error_protocol, "HTTP/2 client stream id must be odd");
    }

    const bool known_stream = streams_.contains(frame.stream_id);
    if (!known_stream && frame.stream_id <= last_client_stream_id_) {
        return reset_stream_result(frame.stream_id, error_stream_closed);
    }

    const auto block = header_block_payload(frame);
    if (!block) {
        return reset_stream_result(frame.stream_id, error_protocol);
    }
    if (block->size() > max_request_bytes) {
        return reset_stream_result(frame.stream_id, error_enhance_your_calm);
    }
    if ((frame.flags & flag_end_headers) == 0) {
        pending_headers_ = PendingHeaderBlock {
            frame.stream_id,
            (frame.flags & flag_end_stream) != 0,
            std::move(*block),
        };
        return {};
    }

    return decode_headers(frame.stream_id, (frame.flags & flag_end_stream) != 0, *block);
}

SessionResult ServerSession::handle_continuation_frame(const Frame& frame, std::size_t max_request_bytes)
{
    if (!pending_headers_ || pending_headers_->stream_id != frame.stream_id) {
        return fail_connection(error_protocol, "HTTP/2 CONTINUATION received without matching HEADERS");
    }
    if (pending_headers_->block.size() + frame.payload.size() > max_request_bytes) {
        pending_headers_.reset();
        return reset_stream_result(frame.stream_id, error_enhance_your_calm);
    }

    pending_headers_->block.insert(pending_headers_->block.end(), frame.payload.begin(), frame.payload.end());
    if ((frame.flags & flag_end_headers) == 0) {
        return {};
    }

    auto pending = std::move(*pending_headers_);
    pending_headers_.reset();
    return decode_headers(pending.stream_id, pending.end_stream, std::move(pending.block));
}

SessionResult ServerSession::decode_headers(std::uint32_t stream_id, bool end_stream, std::vector<std::uint8_t> block)
{
    std::vector<HeaderField> headers;
    try {
        headers = decoder_.decode_header_block(block);
    } catch (const std::exception& error) {
        auto result = reset_stream_result(stream_id, error_compression);
        result.warning = std::string("HTTP/2 HPACK decode failed: ") + error.what();
        return result;
    }

    auto [stream_it, inserted] = streams_.emplace(stream_id, StreamState {});
    auto& stream = stream_it->second;
    if (!inserted && stream.headers_received) {
        return reset_stream_result(stream_id, error_protocol);
    }

    stream.id = stream_id;
    stream.headers_received = true;
    stream.headers = std::move(headers);
    stream.lifecycle = end_stream ? StreamLifecycle::half_closed_remote : StreamLifecycle::open;
    if (stream_id > last_client_stream_id_) {
        last_client_stream_id_ = stream_id;
    }

    if (end_stream) {
        return complete_stream(stream_id);
    }

    return {};
}

SessionResult ServerSession::handle_data_frame(const Frame& frame, std::size_t max_request_bytes)
{
    const auto found = streams_.find(frame.stream_id);
    if (found == streams_.end() || !found->second.headers_received) {
        return reset_stream_result(frame.stream_id, error_stream_closed);
    }
    if (found->second.lifecycle == StreamLifecycle::half_closed_remote) {
        return reset_stream_result(frame.stream_id, error_stream_closed);
    }

    const auto payload = data_payload(frame);
    if (!payload) {
        return reset_stream_result(frame.stream_id, error_protocol);
    }

    auto& stream = found->second;
    if (payload->size() > static_cast<std::size_t>(connection_inbound_window_)
        || payload->size() > static_cast<std::size_t>(stream.inbound_window)) {
        return fail_connection(error_flow_control, "HTTP/2 inbound flow-control window exceeded");
    }
    if (stream.body.size() + payload->size() > max_request_bytes) {
        return reset_stream_result(frame.stream_id, error_enhance_your_calm);
    }
    connection_inbound_window_ -= static_cast<std::int32_t>(payload->size());
    stream.inbound_window -= static_cast<std::int32_t>(payload->size());
    stream.body.append(reinterpret_cast<const char*>(payload->data()), payload->size());

    std::string window_updates;
    if (!payload->empty()) {
        const auto increment = static_cast<std::uint32_t>(payload->size());
        connection_inbound_window_ += static_cast<std::int32_t>(increment);
        stream.inbound_window += static_cast<std::int32_t>(increment);
        window_updates += window_update_frame(0, increment);
        window_updates += window_update_frame(frame.stream_id, increment);
    }

    if ((frame.flags & flag_end_stream) != 0) {
        stream.lifecycle = StreamLifecycle::half_closed_remote;
        auto result = complete_stream(frame.stream_id);
        result.output = window_updates + result.output;
        return result;
    }

    SessionResult result;
    result.output = std::move(window_updates);
    return result;
}

SessionResult ServerSession::fail_connection(std::uint32_t error_code, std::string_view debug_data)
{
    streams_.clear();
    pending_headers_.reset();
    active_ = false;
    SessionResult result;
    result.output = goaway_frame(error_code, debug_data);
    result.keep_connection = false;
    return result;
}

SessionResult ServerSession::reset_stream_result(std::uint32_t stream_id, std::uint32_t error_code)
{
    streams_.erase(stream_id);
    SessionResult result;
    result.output = rst_stream_frame(stream_id, error_code);
    return result;
}

ServerSession::RequestBuildResult ServerSession::build_request(const StreamState& stream) const
{
    std::unordered_map<std::string, std::string> pseudo_headers;
    rimau::http::Headers headers;
    bool regular_header_seen = false;
    std::optional<std::size_t> content_length;

    const auto error = [](int status, std::string message) {
        RequestBuildResult result;
        result.error_response = rimau::http::text_response(status, std::move(message));
        return result;
    };

    for (const auto& field : stream.headers) {
        const std::string name = lowercase(field.name);
        if (name.empty() || header_name_has_uppercase(field.name) || header_value_has_invalid_control(field.value)) {
            return error(400, "Bad Request\n");
        }

        if (name.front() == ':') {
            if (name != ":method" && name != ":path" && name != ":scheme" && name != ":authority") {
                return error(400, "Bad Request\n");
            }
            if (regular_header_seen || !pseudo_headers.emplace(name, field.value).second) {
                return error(400, "Bad Request\n");
            }
            continue;
        }

        regular_header_seen = true;
        if (!valid_header_name(name) || forbidden_request_header(name, field.value)) {
            return error(400, "Bad Request\n");
        }

        if (name == "content-length") {
            if (content_length) {
                return error(400, "Bad Request\n");
            }
            const auto parsed = parse_decimal_size(trim(field.value));
            if (!parsed) {
                return error(400, "Bad Request\n");
            }
            content_length = *parsed;
        }

        auto found = headers.find(name);
        if (found == headers.end()) {
            headers[name] = field.value;
        } else if (name == "cookie") {
            found->second += "; ";
            found->second += field.value;
        } else {
            found->second += ", ";
            found->second += field.value;
        }
    }

    const auto method = pseudo_headers.find(":method");
    const auto path = pseudo_headers.find(":path");
    const auto scheme = pseudo_headers.find(":scheme");
    if (method == pseudo_headers.end() || path == pseudo_headers.end() || scheme == pseudo_headers.end()) {
        return error(400, "Bad Request\n");
    }
    if (scheme->second != "http" && scheme->second != "https") {
        return error(400, "Bad Request\n");
    }
    if (content_length && *content_length != stream.body.size()) {
        return error(400, "Bad Request\n");
    }

    const auto authority = pseudo_headers.find(":authority");
    if (authority != pseudo_headers.end()) {
        const auto host = headers.find("host");
        if (host != headers.end() && host->second != authority->second) {
            return error(400, "Bad Request\n");
        }
        headers["host"] = authority->second;
    }

    std::ostringstream raw_request;
    raw_request << method->second << ' ' << path->second << " HTTP/1.1\r\n";
    for (const auto& [name, value] : headers) {
        raw_request << name << ": " << value << "\r\n";
    }
    if (!stream.body.empty() && !content_length) {
        raw_request << "content-length: " << stream.body.size() << "\r\n";
    }
    raw_request << "\r\n";
    raw_request << stream.body;

    auto parsed = rimau::http::parse_request(raw_request.str());
    if (!parsed || !parsed.request) {
        return error(400, "Bad Request\n");
    }
    parsed.request->version = "HTTP/2";
    return RequestBuildResult { std::move(parsed.request), {} };
}

SessionResult ServerSession::complete_stream(std::uint32_t stream_id)
{
    const auto found = streams_.find(stream_id);
    if (found == streams_.end()) {
        return reset_stream_result(stream_id, error_stream_closed);
    }

    auto stream = std::move(found->second);
    streams_.erase(found);

    auto built = build_request(stream);
    if (!built.request) {
        SessionResult result;
        result.error_stream_id = stream_id;
        result.error_response = std::move(built.error_response);
        return result;
    }

    SessionResult result;
    result.completed_stream = CompletedStream { stream_id, std::move(*built.request) };
    return result;
}

} // namespace rimau::protocol::http2
