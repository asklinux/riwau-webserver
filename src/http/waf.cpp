#include "rimau/http/waf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace rimau::http {
namespace {

struct InspectionTarget {
    const char* variable;
    std::string value;
};

struct Rule {
    int id;
    const char* severity;
    std::size_t score;
    const char* tag;
    const char* message;
};

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string percent_decode_once(std::string_view value)
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

        decoded.push_back(ch == '+' ? ' ' : ch);
    }

    return decoded;
}

std::string inspection_value(std::string_view value, std::size_t limit)
{
    if (limit == 0) {
        return {};
    }

    std::string normalized(value.substr(0, std::min(value.size(), limit)));
    normalized = percent_decode_once(normalized);
    normalized = percent_decode_once(normalized);
    return lowercase(std::move(normalized));
}

std::vector<InspectionTarget> collect_targets(const Request& request, std::size_t limit)
{
    std::vector<InspectionTarget> targets;
    targets.reserve(6 + request.headers.size() + request.query_params.size());
    targets.push_back({ "REQUEST_METHOD", inspection_value(request.method, limit) });
    targets.push_back({ "REQUEST_TARGET", inspection_value(request.target, limit) });
    targets.push_back({ "REQUEST_PATH", inspection_value(request.path, limit) });
    targets.push_back({ "QUERY_STRING", inspection_value(request.query_string, limit) });
    targets.push_back({ "REQUEST_BODY", inspection_value(request.body_text(limit), limit) });

    for (const auto& [name, values] : request.query_params) {
        for (const auto& value : values) {
            targets.push_back({ "ARGS", inspection_value(name + "=" + value, limit) });
        }
    }

    for (const auto& [name, value] : request.headers) {
        targets.push_back({ "REQUEST_HEADERS", inspection_value(name + ": " + value, limit) });
    }

    return targets;
}

void add_match(WafResult& result, const Rule& rule, const char* variable, std::string_view evidence)
{
    result.anomaly_score += rule.score;
    result.matches.push_back(WafMatch {
        rule.id,
        rule.severity,
        rule.score,
        rule.tag,
        rule.message,
        variable,
        std::string(evidence)
    });
}

template <std::size_t N>
bool match_rule(WafResult& result, const Rule& rule, const std::vector<InspectionTarget>& targets, const std::array<std::string_view, N>& needles)
{
    for (const auto& target : targets) {
        for (const auto needle : needles) {
            if (!needle.empty() && target.value.find(needle) != std::string::npos) {
                add_match(result, rule, target.variable, needle);
                return true;
            }
        }
    }

    return false;
}

template <std::size_t N>
bool match_header_rule(
    WafResult& result,
    const Request& request,
    const Rule& rule,
    std::string_view header_name,
    std::size_t limit,
    const std::array<std::string_view, N>& needles)
{
    const auto header = request.header(header_name);
    if (!header) {
        return false;
    }

    const auto normalized = inspection_value(*header, limit);
    for (const auto needle : needles) {
        if (!needle.empty() && normalized.find(needle) != std::string::npos) {
            add_match(result, rule, "REQUEST_HEADERS", needle);
            return true;
        }
    }

    return false;
}

} // namespace

std::optional<int> WafResult::first_rule_id() const
{
    if (matches.empty()) {
        return std::nullopt;
    }
    return matches.front().rule_id;
}

WafResult inspect_request(const Request& request, const WafSettings& settings)
{
    WafResult result;
    if (!settings.enabled) {
        return result;
    }

    result.inspected = true;
    if (!settings.owasp_crs_enabled) {
        return result;
    }

    const auto limit = settings.max_inspection_bytes == 0 ? static_cast<std::size_t>(1) : settings.max_inspection_bytes;
    const auto targets = collect_targets(request, limit);

    match_header_rule(
        result,
        request,
        Rule { 913100, "critical", 5, "application-multi/scanner", "Scanner or vulnerability tool user agent detected" },
        "user-agent",
        limit,
        std::array<std::string_view, 9> { "sqlmap", "nikto", "nmap", "masscan", "acunetix", "nessus", "wpscan", "zgrab", "dirbuster" });

    match_rule(
        result,
        Rule { 920271, "critical", 5, "protocol/request-splitting", "Encoded CRLF sequence or newline in request data" },
        targets,
        std::array<std::string_view, 4> { "%0d", "%0a", "\r", "\n" });

    match_rule(
        result,
        Rule { 930100, "critical", 5, "application-multi/path-traversal", "Path traversal or local file probing pattern detected" },
        targets,
        std::array<std::string_view, 8> { "../", "..\\", "%2e%2e", "%252e%252e", "/etc/passwd", "boot.ini", "win.ini", "/proc/self" });

    match_rule(
        result,
        Rule { 941100, "critical", 5, "application-multi/xss", "Cross-site scripting pattern detected" },
        targets,
        std::array<std::string_view, 8> { "<script", "javascript:", "onerror=", "onload=", "document.cookie", "<svg", "<iframe", "srcdoc=" });

    match_rule(
        result,
        Rule { 942100, "critical", 5, "application-multi/sqli", "SQL injection pattern detected" },
        targets,
        std::array<std::string_view, 8> { "union select", " or 1=1", "' or '1'='1", "\" or \"1\"=\"1", "information_schema", "sleep(", "benchmark(", "xp_cmdshell" });

    match_rule(
        result,
        Rule { 932100, "critical", 5, "application-multi/rce", "Remote command execution pattern detected" },
        targets,
        std::array<std::string_view, 10> { ";cat ", "|id", "&&", "$(", "`", "/bin/sh", "/bin/bash", "curl http", "wget http", "powershell" });

    match_rule(
        result,
        Rule { 933100, "critical", 5, "application-multi/php-injection", "PHP injection or stream wrapper pattern detected" },
        targets,
        std::array<std::string_view, 7> { "<?php", "php://", "data://", "expect://", "auto_prepend_file", "allow_url_include", "phar://" });

    match_rule(
        result,
        Rule { 944100, "critical", 5, "application-multi/java-jndi", "Java/JNDI exploitation pattern detected" },
        targets,
        std::array<std::string_view, 5> { "${jndi:", "jndi:ldap", "jndi:rmi", "class.module.classloader", "ldap://" });

    if (settings.blocking_enabled && result.anomaly_score >= settings.anomaly_threshold) {
        result.allowed = false;
    }

    return result;
}

Response waf_block_response(const WafResult& result)
{
    auto response = text_response(403, "Forbidden by Rimau ModSecurity compatible WAF\n");
    response.headers["x-rimau-waf"] = "blocked";
    response.headers["x-rimau-waf-engine"] = result.engine;
    response.headers["x-rimau-waf-ruleset"] = result.ruleset;
    response.headers["x-rimau-waf-score"] = std::to_string(result.anomaly_score);
    if (const auto rule_id = result.first_rule_id()) {
        response.headers["x-rimau-waf-rule-id"] = std::to_string(*rule_id);
    }
    return response;
}

} // namespace rimau::http
