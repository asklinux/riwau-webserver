#include "rimau/http/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace rimau::http {
namespace {

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };

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

ParseResult error(ParseError error, std::string message)
{
    return ParseResult { std::nullopt, error, std::move(message) };
}

std::string percent_decode(std::string_view value, bool plus_as_space)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '%' && index + 2 < value.size()) {
            const std::string hex { value.substr(index + 1, 2) };
            char* end = nullptr;
            const long parsed = std::strtol(hex.c_str(), &end, 16);
            if (end == hex.c_str() + 2) {
                decoded.push_back(static_cast<char>(parsed));
                index += 2;
                continue;
            }
        }

        if (ch == '+' && plus_as_space) {
            decoded.push_back(' ');
        } else {
            decoded.push_back(ch);
        }
    }

    return decoded;
}

void parse_target(Request& request)
{
    const auto query_start = request.target.find('?');
    const auto fragment_start = request.target.find('#');
    const auto path_end = std::min(
        query_start == std::string::npos ? request.target.size() : query_start,
        fragment_start == std::string::npos ? request.target.size() : fragment_start);

    request.path = percent_decode(std::string_view(request.target).substr(0, path_end), false);

    if (query_start == std::string::npos || (fragment_start != std::string::npos && fragment_start < query_start)) {
        request.query_string.clear();
        return;
    }

    const auto query_end = fragment_start == std::string::npos ? request.target.size() : fragment_start;
    request.query_string = std::string(std::string_view(request.target).substr(query_start + 1, query_end - query_start - 1));

    std::size_t start = 0;
    while (start <= request.query_string.size()) {
        const auto separator = request.query_string.find('&', start);
        const auto end = separator == std::string::npos ? request.query_string.size() : separator;
        const auto pair = std::string_view(request.query_string).substr(start, end - start);

        if (!pair.empty()) {
            const auto equals = pair.find('=');
            const auto raw_name = equals == std::string_view::npos ? pair : pair.substr(0, equals);
            const auto raw_value = equals == std::string_view::npos ? std::string_view {} : pair.substr(equals + 1);
            request.query_params[percent_decode(raw_name, true)].push_back(percent_decode(raw_value, true));
        }

        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
}

} // namespace

RequestBodyFile::RequestBodyFile(std::filesystem::path path_value)
    : path(std::move(path_value))
{
}

RequestBodyFile::~RequestBodyFile()
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

RequestBodyReader::RequestBodyReader(const Request& request)
    : request_(request)
{
    if (request_.body_file) {
        file_.open(request_.body_file->path, std::ios::binary);
    }
}

std::string RequestBodyReader::read_chunk(std::size_t max_bytes)
{
    if (max_bytes == 0 || eof()) {
        return {};
    }

    if (!request_.body_file) {
        const auto remaining = request_.body.size() - offset_;
        const auto bytes_to_read = std::min(max_bytes, remaining);
        auto chunk = request_.body.substr(offset_, bytes_to_read);
        offset_ += chunk.size();
        exhausted_ = offset_ >= request_.body.size();
        return chunk;
    }

    std::string chunk;
    chunk.resize(max_bytes);
    file_.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<std::size_t>(file_.gcount()));
    offset_ += chunk.size();
    exhausted_ = chunk.empty() || offset_ >= request_.body_size();
    return chunk;
}

bool RequestBodyReader::eof() const noexcept
{
    if (exhausted_) {
        return true;
    }
    if (!request_.body_file) {
        return offset_ >= request_.body.size();
    }
    return !file_.is_open() || offset_ >= request_.body_size();
}

std::size_t RequestBodyReader::bytes_read() const noexcept
{
    return offset_;
}

std::optional<std::string> Request::header(std::string_view name) const
{
    std::string key(name);
    key = lowercase(std::move(key));

    const auto found = headers.find(key);
    if (found == headers.end()) {
        return std::nullopt;
    }

    return found->second;
}

std::optional<std::string> Request::query(std::string_view name) const
{
    const auto found = query_params.find(std::string(name));
    if (found == query_params.end() || found->second.empty()) {
        return std::nullopt;
    }

    return found->second.front();
}

bool Request::content_type_contains(std::string_view token) const
{
    const auto value = header("content-type");
    if (!value) {
        return false;
    }

    return lowercase(*value).find(lowercase(std::string(token))) != std::string::npos;
}

bool Request::is_json() const
{
    return content_type_contains("application/json") || content_type_contains("+json");
}

std::size_t Request::body_size() const noexcept
{
    return body_size_bytes == 0 && !body_spooled_to_file() ? body.size() : body_size_bytes;
}

bool Request::body_spooled_to_file() const noexcept
{
    return static_cast<bool>(body_file);
}

std::string Request::body_text(std::size_t max_bytes) const
{
    const auto bytes_to_read = max_bytes == 0 ? body_size() : std::min(max_bytes, body_size());
    std::string output;
    output.reserve(bytes_to_read);

    auto reader = open_body_reader();
    while (output.size() < bytes_to_read && !reader.eof()) {
        auto chunk = reader.read_chunk(std::min<std::size_t>(8192, bytes_to_read - output.size()));
        if (chunk.empty()) {
            break;
        }
        output += std::move(chunk);
    }

    return output;
}

RequestBodyReader Request::open_body_reader() const
{
    return RequestBodyReader(*this);
}

ParseResult parse_request(std::string_view raw_request)
{
    if (raw_request.empty()) {
        return error(ParseError::empty_request, "request is empty");
    }

    std::istringstream input { std::string(raw_request) };
    std::string line;
    if (!std::getline(input, line)) {
        return error(ParseError::empty_request, "request line is empty");
    }

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::istringstream request_line(line);
    Request request;
    std::string extra;
    if (!(request_line >> request.method >> request.target >> request.version) || (request_line >> extra)) {
        return error(ParseError::malformed_request_line, "request line must be: METHOD TARGET HTTP/VERSION");
    }

    if (request.version != "HTTP/1.1" && request.version != "HTTP/1.0") {
        return error(ParseError::unsupported_http_version, "only HTTP/1.0 and HTTP/1.1 parsing is implemented");
    }

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            break;
        }

        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            return error(ParseError::malformed_header, "header is missing ':'");
        }

        std::string name = lowercase(trim(line.substr(0, separator)));
        std::string value = trim(line.substr(separator + 1));
        if (name.empty()) {
            return error(ParseError::malformed_header, "header name is empty");
        }

        request.headers[name] = value;
    }

    std::ostringstream body;
    body << input.rdbuf();
    request.body = body.str();
    request.body_size_bytes = request.body.size();
    parse_target(request);

    return ParseResult { std::move(request), ParseError::none, "" };
}

} // namespace rimau::http
