#include "rimau/http/http1_session.hpp"

#include "rimau/http/parser.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rimau::http {
namespace {

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> split_csv(std::string_view value)
{
    std::vector<std::string> items;
    std::istringstream input { std::string(value) };
    std::string item;
    while (std::getline(input, item, ',')) {
        item = trim(std::move(item));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
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
    for (const char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
        const auto digit = static_cast<std::size_t>(ch - '0');
        if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        parsed = parsed * 10 + digit;
    }
    return parsed;
}

std::optional<std::size_t> parse_hex_size(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    std::size_t parsed = 0;
    for (const char ch : value) {
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = 10 + ch - 'a';
        } else if (ch >= 'A' && ch <= 'F') {
            digit = 10 + ch - 'A';
        } else {
            return std::nullopt;
        }

        if (parsed > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(digit)) / 16) {
            return std::nullopt;
        }
        parsed = parsed * 16 + static_cast<std::size_t>(digit);
    }

    return parsed;
}

struct HeaderFrameAnalysis {
    bool ok = true;
    bool chunked = false;
    std::optional<std::size_t> content_length;
    int status = 400;
    std::string message = "Bad Request\n";
};

HeaderFrameAnalysis bad_header_frame(int status, std::string message)
{
    HeaderFrameAnalysis analysis;
    analysis.ok = false;
    analysis.status = status;
    analysis.message = std::move(message);
    return analysis;
}

HeaderFrameAnalysis analyze_header_frame(std::string_view raw_header)
{
    HeaderFrameAnalysis analysis;
    std::vector<std::string> content_lengths;
    std::vector<std::string> transfer_encodings;

    for (std::size_t index = 0; index < raw_header.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(raw_header[index]);
        if (ch == '\n' && (index == 0 || raw_header[index - 1] != '\r')) {
            return bad_header_frame(400, "Bad Request\n");
        }
        if (ch == '\r' && (index + 1 >= raw_header.size() || raw_header[index + 1] != '\n')) {
            return bad_header_frame(400, "Bad Request\n");
        }
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            continue;
        }
        if (ch < 0x20 || ch == 0x7f) {
            return bad_header_frame(400, "Bad Request\n");
        }
    }

    std::size_t line_start = 0;
    bool first_line = true;
    while (line_start < raw_header.size()) {
        const auto line_end = raw_header.find("\r\n", line_start);
        if (line_end == std::string_view::npos) {
            return bad_header_frame(400, "Bad Request\n");
        }

        const auto line = raw_header.substr(line_start, line_end - line_start);
        line_start = line_end + 2;

        if (line.empty()) {
            break;
        }

        if (first_line) {
            first_line = false;
            continue;
        }

        if (line.front() == ' ' || line.front() == '\t') {
            return bad_header_frame(400, "Bad Request\n");
        }

        const auto separator = line.find(':');
        if (separator == std::string_view::npos) {
            return bad_header_frame(400, "Bad Request\n");
        }

        const auto raw_name = line.substr(0, separator);
        if (!valid_header_name(raw_name)) {
            return bad_header_frame(400, "Bad Request\n");
        }

        std::string name = lowercase(std::string(raw_name));
        std::string value = trim(std::string(line.substr(separator + 1)));
        for (const unsigned char ch : value) {
            if (ch != '\t' && (ch < 0x20 || ch == 0x7f)) {
                return bad_header_frame(400, "Bad Request\n");
            }
        }

        if (name == "content-length") {
            content_lengths.push_back(std::move(value));
        } else if (name == "transfer-encoding") {
            transfer_encodings.push_back(std::move(value));
        }
    }

    if (content_lengths.size() > 1) {
        return bad_header_frame(400, "Bad Request\n");
    }

    if (!content_lengths.empty()) {
        const auto length = parse_decimal_size(trim(content_lengths.front()));
        if (!length) {
            return bad_header_frame(400, "Bad Request\n");
        }
        analysis.content_length = *length;
    }

    if (transfer_encodings.size() > 1) {
        return bad_header_frame(400, "Bad Request\n");
    }

    if (!transfer_encodings.empty()) {
        const auto encodings = split_csv(transfer_encodings.front());
        if (encodings.size() != 1 || lowercase(encodings.front()) != "chunked") {
            return bad_header_frame(400, "Bad Request\n");
        }
        if (analysis.content_length) {
            return bad_header_frame(400, "Bad Request\n");
        }
        analysis.chunked = true;
    }

    return analysis;
}

enum class ChunkedDecodeState {
    incomplete,
    complete,
    malformed
};

struct ChunkedDecodeResult {
    ChunkedDecodeState state = ChunkedDecodeState::incomplete;
    std::string body;
    std::size_t consumed = 0;
};

ChunkedDecodeResult decode_chunked_body(std::string_view encoded)
{
    ChunkedDecodeResult result;
    std::size_t cursor = 0;

    while (true) {
        const auto line_end = encoded.find("\r\n", cursor);
        if (line_end == std::string_view::npos) {
            return result;
        }

        auto size_line = encoded.substr(cursor, line_end - cursor);
        const auto extension = size_line.find(';');
        if (extension != std::string_view::npos) {
            size_line = size_line.substr(0, extension);
        }

        const auto chunk_size = parse_hex_size(trim(std::string(size_line)));
        if (!chunk_size) {
            result.state = ChunkedDecodeState::malformed;
            return result;
        }

        cursor = line_end + 2;
        if (*chunk_size == 0) {
            while (true) {
                const auto trailer_end = encoded.find("\r\n", cursor);
                if (trailer_end == std::string_view::npos) {
                    result.state = ChunkedDecodeState::incomplete;
                    return result;
                }
                if (trailer_end == cursor) {
                    result.state = ChunkedDecodeState::complete;
                    result.consumed = trailer_end + 2;
                    return result;
                }
                cursor = trailer_end + 2;
            }
        }

        if (encoded.size() < cursor + *chunk_size + 2) {
            return result;
        }

        result.body.append(encoded.substr(cursor, *chunk_size));
        cursor += *chunk_size;
        if (encoded.substr(cursor, 2) != "\r\n") {
            result.state = ChunkedDecodeState::malformed;
            return result;
        }
        cursor += 2;
    }
}

Http1FrameResult incomplete_frame(bool waiting_for_body = false)
{
    Http1FrameResult result;
    result.state = Http1FrameState::incomplete;
    result.waiting_for_body = waiting_for_body;
    return result;
}

Http1FrameResult error_frame(int status, std::string message, std::size_t consumed = 0, bool discard_buffer = false)
{
    Http1FrameResult result;
    result.state = Http1FrameState::error;
    result.error_status = status;
    result.error_message = std::move(message);
    result.consumed = consumed;
    result.discard_buffer = discard_buffer;
    return result;
}

} // namespace

Http1FrameResult next_http1_request_frame(std::string_view buffered, const Http1FrameOptions& options)
{
    const auto header_end = buffered.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        if (buffered.size() > options.max_request_bytes) {
            return error_frame(431, "Request Header Fields Too Large\n");
        }
        return incomplete_frame();
    }

    const std::size_t body_start = header_end + 4;
    const auto frame_analysis = analyze_header_frame(buffered.substr(0, body_start));
    if (!frame_analysis.ok) {
        return error_frame(frame_analysis.status, frame_analysis.message, body_start);
    }

    if (body_start > options.max_request_bytes) {
        return error_frame(431, "Request Header Fields Too Large\n", body_start);
    }

    const auto header_parse = parse_request(buffered.substr(0, body_start));
    if (!header_parse) {
        return error_frame(400, "Bad Request\n", body_start);
    }

    if (frame_analysis.chunked) {
        const auto decoded = decode_chunked_body(buffered.substr(body_start));
        if (decoded.state == ChunkedDecodeState::incomplete) {
            if (buffered.size() > options.max_request_bytes) {
                return error_frame(413, "Content Too Large\n", 0, true);
            }
            Http1FrameResult result = incomplete_frame(true);
            result.state = Http1FrameState::header_complete;
            result.request = header_parse.request;
            result.consumed = body_start;
            return result;
        }
        const bool chunked_too_large = decoded.consumed > options.max_request_bytes || body_start > options.max_request_bytes - decoded.consumed;
        if (decoded.state == ChunkedDecodeState::malformed || chunked_too_large) {
            const std::size_t consumed = chunked_too_large ? buffered.size() : std::min(buffered.size(), body_start + decoded.consumed);
            return error_frame(400, "Bad Request\n", consumed);
        }

        Http1FrameResult result;
        result.state = Http1FrameState::complete;
        result.request = header_parse.request;
        result.raw_request = std::string(buffered.substr(0, body_start));
        result.raw_request += decoded.body;
        result.consumed = body_start + decoded.consumed;
        return result;
    }

    const std::size_t body_size = frame_analysis.content_length.value_or(0);
    if (body_size > options.max_request_bytes || body_start > options.max_request_bytes - body_size) {
        return error_frame(413, "Content Too Large\n", 0, true);
    }

    const std::size_t total_size = body_start + body_size;
    if (buffered.size() < total_size) {
        Http1FrameResult result = incomplete_frame(body_size > 0);
        result.state = Http1FrameState::header_complete;
        result.request = header_parse.request;
        result.consumed = body_start;
        return result;
    }

    Http1FrameResult result;
    result.state = Http1FrameState::complete;
    result.request = header_parse.request;
    result.raw_request = std::string(buffered.substr(0, total_size));
    result.consumed = total_size;
    return result;
}

} // namespace rimau::http
