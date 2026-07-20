#include "rimau/http/parser.hpp"
#include "rimau/http/waf.hpp"

#include <cassert>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

rimau::http::Request parse_raw_request(std::string_view raw_request)
{
    auto parsed = rimau::http::parse_request(raw_request);
    assert(parsed);
    assert(parsed.request);
    return std::move(*parsed.request);
}

struct FalsePositiveCase {
    const char* name;
    std::string_view raw_request;
};

void assert_clean_traffic_allowed(const FalsePositiveCase& corpus_case)
{
    const auto request = parse_raw_request(corpus_case.raw_request);
    const auto result = rimau::http::inspect_request(request, enabled_settings());
    assert(result.inspected);
    assert(result.allowed);
    assert(result.matches.empty());
}

} // namespace

int main()
{
    const std::vector<FalsePositiveCase> false_positive_corpus {
        {
            "curl default accept header",
            "GET / HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "User-Agent: curl/8.5.0\r\n"
            "Accept: */*\r\n"
            "\r\n",
        },
        {
            "browser document navigation",
            "GET /products/widgets?page=2&sort=price-asc HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n"
            "Accept-Language: en-US,en;q=0.9\r\n"
            "Sec-Fetch-Dest: document\r\n"
            "Sec-Fetch-Mode: navigate\r\n"
            "Sec-Fetch-Site: same-origin\r\n"
            "Upgrade-Insecure-Requests: 1\r\n"
            "Cookie: rimau_session=placeholder; theme=dark\r\n"
            "\r\n",
        },
        {
            "browser static asset request",
            "GET /assets/app.css?v=20260720 HTTP/1.1\r\n"
            "Host: static.example.test\r\n"
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 14_5) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Safari/605.1.15\r\n"
            "Accept: text/css,*/*;q=0.1\r\n"
            "Referer: https://example.test/products/widgets\r\n"
            "Sec-Fetch-Dest: style\r\n"
            "Sec-Fetch-Mode: no-cors\r\n"
            "Sec-Fetch-Site: same-origin\r\n"
            "\r\n",
        },
        {
            "normal search query",
            "GET /search?q=unionized+workers+selective+sleepwear&category=articles HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "User-Agent: curl/8.5.0\r\n"
            "Accept: application/json\r\n"
            "\r\n",
        },
        {
            "json api post",
            "POST /api/orders HTTP/1.1\r\n"
            "Host: api.example.test\r\n"
            "User-Agent: RimauClient/1.0\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "Content-Length: 57\r\n"
            "\r\n"
            "{\"item\":\"widget\",\"quantity\":2,\"note\":\"normal order flow\"}",
        },
        {
            "form submission",
            "POST /contact HTTP/1.1\r\n"
            "Host: example.test\r\n"
            "User-Agent: Mozilla/5.0\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Accept: text/html,*/*;q=0.8\r\n"
            "Content-Length: 58\r\n"
            "\r\n"
            "name=Rimau+User&message=Please+send+the+updated+price+list",
        },
        {
            "websocket upgrade",
            "GET /ws/notifications?channel=orders HTTP/1.1\r\n"
            "Host: app.example.test\r\n"
            "User-Agent: Mozilla/5.0\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: chat\r\n"
            "\r\n",
        },
    };

    for (const auto& corpus_case : false_positive_corpus) {
        assert_clean_traffic_allowed(corpus_case);
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
        auto request = make_request("/../../etc/passwd");
        auto settings = enabled_settings();
        settings.disabled_rule_ids.push_back(930100);
        const auto result = rimau::http::inspect_request(request, settings);
        assert(result.inspected);
        assert(result.allowed);
        assert(result.anomaly_score == 0);
        assert(result.matches.empty());
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
