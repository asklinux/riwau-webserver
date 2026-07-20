#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rimau::http {

using Headers = std::unordered_map<std::string, std::string>;
using QueryParams = std::unordered_map<std::string, std::vector<std::string>>;

struct RequestBodyFile {
    explicit RequestBodyFile(std::filesystem::path path);
    RequestBodyFile(const RequestBodyFile&) = delete;
    RequestBodyFile& operator=(const RequestBodyFile&) = delete;
    ~RequestBodyFile();

    std::filesystem::path path;
};

struct Request;

class RequestBodyReader {
public:
    explicit RequestBodyReader(const Request& request);

    std::string read_chunk(std::size_t max_bytes);
    bool eof() const noexcept;
    std::size_t bytes_read() const noexcept;

private:
    const Request& request_;
    std::ifstream file_;
    std::size_t offset_ = 0;
    bool exhausted_ = false;
};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query_string;
    std::string version;
    Headers headers;
    QueryParams query_params;
    std::string body;
    std::size_t body_size_bytes = 0;
    std::shared_ptr<RequestBodyFile> body_file;

    std::optional<std::string> header(std::string_view name) const;
    std::optional<std::string> query(std::string_view name) const;
    bool content_type_contains(std::string_view token) const;
    bool is_json() const;
    std::size_t body_size() const noexcept;
    bool body_spooled_to_file() const noexcept;
    std::string body_text(std::size_t max_bytes = 0) const;
    RequestBodyReader open_body_reader() const;
};

} // namespace rimau::http
