#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rimau::http {

using Headers = std::unordered_map<std::string, std::string>;
using QueryParams = std::unordered_map<std::string, std::vector<std::string>>;

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query_string;
    std::string version;
    Headers headers;
    QueryParams query_params;
    std::string body;

    std::optional<std::string> header(std::string_view name) const;
    std::optional<std::string> query(std::string_view name) const;
    bool content_type_contains(std::string_view token) const;
    bool is_json() const;
};

} // namespace rimau::http
