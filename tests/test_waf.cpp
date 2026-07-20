#include "rimau/http/waf.hpp"

#include <cassert>
#include <string>

namespace {

rimau::http::Request make_request(std::string target)
{
    rimau::http::Request request;
    request.method = "GET";
    request.target = std::move(target);
    const auto query = request.target.find('?');
    request.path = query == std::string::npos ? request.target : request.target.substr(0, query);
    request.query_string = query == std::string::npos ? "" : request.target.substr(query + 1);
    request.version = "HTTP/1.1";
    request.headers["host"] = "example.test";
    return request;
}

rimau::http::WafSettings enabled_settings()
{
    rimau::http::WafSettings settings;
    settings.enabled = true;
    settings.owasp_crs_enabled = true;
    settings.blocking_enabled = true;
    settings.anomaly_threshold = 5;
    settings.max_inspection_bytes = 128 * 1024;
    return settings;
}

bool has_rule(const rimau::http::WafResult& result, int rule_id)
{
    for (const auto& match : result.matches) {
        if (match.rule_id == rule_id) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    {
        auto request = make_request("/");
        request.headers["accept"] = "*/*";
        request.headers["user-agent"] = "curl/8.5.0";
        const auto result = rimau::http::inspect_request(request, enabled_settings());
        assert(result.inspected);
        assert(result.allowed);
        assert(result.matches.empty());
    }

    {
        auto request = make_request("/../../etc/passwd");
        rimau::http::WafSettings settings;
        const auto result = rimau::http::inspect_request(request, settings);
        assert(!result.inspected);
        assert(result.allowed);
        assert(result.matches.empty());
    }

    {
        auto request = make_request("/../../etc/passwd");
        const auto result = rimau::http::inspect_request(request, enabled_settings());
        assert(result.inspected);
        assert(!result.allowed);
        assert(result.anomaly_score >= 5);
        assert(has_rule(result, 930100));
    }

    {
        auto request = make_request("/search?q=%27%20or%201=1--");
        const auto result = rimau::http::inspect_request(request, enabled_settings());
        assert(!result.allowed);
        assert(has_rule(result, 942100));
    }

    {
        auto request = make_request("/index.html");
        request.headers["user-agent"] = "sqlmap/1.8";
        const auto result = rimau::http::inspect_request(request, enabled_settings());
        assert(!result.allowed);
        assert(has_rule(result, 913100));
    }

    {
        auto request = make_request("/search?q=<script>alert(1)</script>");
        auto settings = enabled_settings();
        settings.blocking_enabled = false;
        const auto result = rimau::http::inspect_request(request, settings);
        assert(result.inspected);
        assert(result.allowed);
        assert(!result.matches.empty());
        assert(has_rule(result, 941100));
    }

    {
        auto response = rimau::http::waf_block_response(rimau::http::inspect_request(make_request("/../../etc/passwd"), enabled_settings()));
        assert(response.status == 403);
        assert(response.headers["x-rimau-waf"] == "blocked");
        assert(response.headers["x-rimau-waf-engine"] == "rimau-modsecurity-compatible");
        assert(response.headers["x-rimau-waf-ruleset"] == "builtin-owasp-crs-subset");
        assert(response.headers["x-rimau-waf-rule-id"] == "930100");
    }

    return 0;
}
