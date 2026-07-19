#include "rimau/http/response.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <zlib.h>

namespace rimau::http {
namespace {

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string mime_type(const std::filesystem::path& path)
{
    const std::string extension = lowercase(path.extension().string());

    if (extension == ".html" || extension == ".htm") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (extension == ".json") {
        return "application/json; charset=utf-8";
    }
    if (extension == ".txt") {
        return "text/plain; charset=utf-8";
    }
    if (extension == ".xml") {
        return "application/xml; charset=utf-8";
    }
    if (extension == ".wasm") {
        return "application/wasm";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    if (extension == ".ico") {
        return "image/x-icon";
    }
    if (extension == ".mp4" || extension == ".m4v") {
        return "video/mp4";
    }
    if (extension == ".webm") {
        return "video/webm";
    }
    if (extension == ".mov") {
        return "video/quicktime";
    }
    if (extension == ".pdf") {
        return "application/pdf";
    }

    return "application/octet-stream";
}

bool header_has_token(const Request& request, std::string_view header_name, std::string_view token)
{
    const auto header = request.header(header_name);
    if (!header) {
        return false;
    }

    const std::string expected = lowercase(std::string(token));
    std::istringstream input(*header);
    std::string item;
    while (std::getline(input, item, ',')) {
        item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), item.end());
        if (lowercase(item) == expected) {
            return true;
        }
    }
    return false;
}

bool is_compressible(std::string_view content_type)
{
    return content_type.starts_with("text/")
        || content_type.find("json") != std::string_view::npos
        || content_type.find("javascript") != std::string_view::npos
        || content_type.find("xml") != std::string_view::npos
        || content_type.find("wasm") != std::string_view::npos;
}

bool is_valid_header_name(std::string_view name)
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

std::string gzip_compress(std::string_view input)
{
    z_stream stream {};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    std::string output;
    output.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    const int rc = deflate(&stream, Z_FINISH);
    if (rc != Z_STREAM_END) {
        deflateEnd(&stream);
        return {};
    }

    output.resize(stream.total_out);
    deflateEnd(&stream);
    return output;
}

bool starts_with_path(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    auto child_it = child.begin();
    auto parent_it = parent.begin();

    for (; parent_it != parent.end(); ++parent_it, ++child_it) {
        if (child_it == child.end() || *child_it != *parent_it) {
            return false;
        }
    }

    return true;
}

std::string percent_decode_path(std::string value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const auto hex = value.substr(index + 1, 2);
            char* end = nullptr;
            const long parsed = std::strtol(hex.c_str(), &end, 16);
            if (end != hex.c_str() + 2) {
                decoded.push_back(value[index]);
                continue;
            }
            decoded.push_back(static_cast<char>(parsed));
            index += 2;
        } else if (value[index] == '+') {
            decoded.push_back('+');
        } else {
            decoded.push_back(value[index]);
        }
    }

    return decoded;
}

std::string strip_query(std::string target)
{
    const auto query = target.find_first_of("?#");
    if (query != std::string::npos) {
        target.erase(query);
    }
    return target;
}

struct RangeSelection {
    bool requested = false;
    bool satisfiable = false;
    std::uintmax_t start = 0;
    std::uintmax_t end = 0;
};

bool parse_uint(std::string_view value, std::uintmax_t& output)
{
    if (value.empty()) {
        return false;
    }

    std::uintmax_t parsed = 0;
    for (const char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        const auto digit = static_cast<std::uintmax_t>(ch - '0');
        if (parsed > (std::numeric_limits<std::uintmax_t>::max() - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    output = parsed;
    return true;
}

RangeSelection parse_range(const Request& request, std::uintmax_t file_size)
{
    RangeSelection selection;
    const auto range = request.header("range");
    if (!range) {
        return selection;
    }

    selection.requested = true;
    if (!range->starts_with("bytes=") || range->find(',') != std::string::npos || file_size == 0) {
        return selection;
    }

    const std::string_view spec(*range);
    const auto dash = spec.find('-', 6);
    if (dash == std::string_view::npos) {
        return selection;
    }

    const auto first = spec.substr(6, dash - 6);
    const auto last = spec.substr(dash + 1);

    std::uintmax_t start = 0;
    std::uintmax_t end = 0;
    if (first.empty()) {
        std::uintmax_t suffix_length = 0;
        if (!parse_uint(last, suffix_length) || suffix_length == 0) {
            return selection;
        }
        if (suffix_length >= file_size) {
            start = 0;
        } else {
            start = file_size - suffix_length;
        }
        end = file_size - 1;
    } else {
        if (!parse_uint(first, start)) {
            return selection;
        }
        if (last.empty()) {
            end = file_size - 1;
        } else if (!parse_uint(last, end)) {
            return selection;
        }
    }

    if (start >= file_size || end < start) {
        return selection;
    }
    if (end >= file_size) {
        end = file_size - 1;
    }

    selection.satisfiable = true;
    selection.start = start;
    selection.end = end;
    return selection;
}

std::string read_file_range(const std::filesystem::path& path, std::uintmax_t start, std::uintmax_t length)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    file.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    std::string body;
    body.resize(static_cast<std::size_t>(length));
    file.read(body.data(), static_cast<std::streamsize>(body.size()));
    body.resize(static_cast<std::size_t>(file.gcount()));
    return body;
}

} // namespace

std::string reason_phrase(int status)
{
    switch (status) {
    case 101:
        return "Switching Protocols";
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 206:
        return "Partial Content";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 414:
        return "URI Too Long";
    case 416:
        return "Range Not Satisfiable";
    case 429:
        return "Too Many Requests";
    case 431:
        return "Request Header Fields Too Large";
    case 413:
        return "Content Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "Unknown";
    }
}

std::string Response::to_http_string(bool include_body) const
{
    return to_http_string(include_body, SerializationOptions {});
}

std::string Response::to_http_string(bool include_body, const SerializationOptions& options) const
{
    std::ostringstream output;
    output << "HTTP/1.1 " << status << ' ' << reason << "\r\n";

    Headers final_headers = headers;
    if (status != 101) {
        for (const auto& [name, value] : options.default_headers) {
            if (!final_headers.contains(name)) {
                final_headers[name] = value;
            }
        }
    }
    if (status != 101 && status != 204 && !final_headers.contains("transfer-encoding")) {
        final_headers["content-length"] = std::to_string(body.size());
    }
    if (!final_headers.contains("connection")) {
        final_headers["connection"] = "close";
    }
    if (options.server_header_enabled) {
        final_headers["server"] = options.server_name.empty() ? "Rimau Web Server" : options.server_name;
    } else {
        final_headers.erase("server");
    }

    for (const auto& [name, value] : final_headers) {
        if (!is_valid_header_name(name)) {
            continue;
        }
        output << name << ": " << sanitize_header_value(value) << "\r\n";
    }

    output << "\r\n";
    if (include_body) {
        output << body;
    }

    return output.str();
}

Response text_response(int status, std::string body, std::string content_type)
{
    Response response;
    response.status = status;
    response.reason = reason_phrase(status);
    response.headers["content-type"] = std::move(content_type);
    response.body = std::move(body);
    return response;
}

Response json_response(int status, std::string json_body)
{
    return text_response(status, std::move(json_body), "application/json; charset=utf-8");
}

std::string json_escape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (const unsigned char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20) {
                const char* hex = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(hex[(ch >> 4) & 0x0f]);
                escaped.push_back(hex[ch & 0x0f]);
            } else {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    return escaped;
}

Response file_response(const Request& request, const std::filesystem::path& document_root)
{
    if (request.method != "GET" && request.method != "HEAD") {
        auto response = text_response(405, "Method Not Allowed\n");
        response.headers["allow"] = "GET, HEAD";
        return response;
    }

    std::string target = request.path.empty() ? percent_decode_path(strip_query(request.target)) : request.path;
    if (target.empty() || target.front() != '/' || target.find('\0') != std::string::npos) {
        return text_response(400, "Bad Request\n");
    }

    if (target.size() > 4096) {
        return text_response(414, "URI Too Long\n");
    }

    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(document_root, error);
    if (error) {
        return text_response(500, "Document root is not available\n");
    }

    auto relative = std::filesystem::path(target.substr(1)).lexically_normal();
    if (relative.empty() || relative == ".") {
        relative = "index.html";
    }

    auto candidate = std::filesystem::weakly_canonical(root / relative, error);
    if (error) {
        candidate = (root / relative).lexically_normal();
    }

    if (!starts_with_path(candidate, root)) {
        return text_response(403, "Forbidden\n");
    }

    if (std::filesystem::is_directory(candidate, error)) {
        candidate /= "index.html";
        candidate = std::filesystem::weakly_canonical(candidate, error);
        if (error) {
            candidate = candidate.lexically_normal();
        }
        if (!starts_with_path(candidate, root)) {
            return text_response(403, "Forbidden\n");
        }
    }

    if (!std::filesystem::exists(candidate, error)) {
        return text_response(404, "Not Found\n");
    }

    if (!std::filesystem::is_regular_file(candidate, error)) {
        return text_response(403, "Forbidden\n");
    }

    const auto file_size = std::filesystem::file_size(candidate, error);
    if (error) {
        return text_response(403, "Forbidden\n");
    }

    Response response;
    const std::string content_type = mime_type(candidate);
    response.headers["content-type"] = content_type;
    response.headers["accept-ranges"] = "bytes";

    const auto range = parse_range(request, file_size);
    if (range.requested && !range.satisfiable) {
        response.status = 416;
        response.reason = reason_phrase(response.status);
        response.headers["content-range"] = "bytes */" + std::to_string(file_size);
        response.body = "Range Not Satisfiable\n";
        return response;
    }

    if (range.satisfiable) {
        const auto length = range.end - range.start + 1;
        response.status = 206;
        response.reason = reason_phrase(response.status);
        response.headers["content-range"] = "bytes " + std::to_string(range.start) + "-" + std::to_string(range.end) + "/" + std::to_string(file_size);
        response.body = read_file_range(candidate, range.start, length);
        return response;
    }

    response.status = 200;
    response.reason = reason_phrase(response.status);
    response.body = read_file_range(candidate, 0, file_size);

    if (response.body.size() > 256 && header_has_token(request, "accept-encoding", "gzip") && is_compressible(content_type)) {
        const auto compressed = gzip_compress(response.body);
        if (!compressed.empty() && compressed.size() < response.body.size()) {
            response.body = compressed;
            response.headers["content-encoding"] = "gzip";
            response.headers["vary"] = "Accept-Encoding";
        }
    }

    return response;
}

} // namespace rimau::http
