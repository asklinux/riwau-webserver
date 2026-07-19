#include "rimau/http/response_sink.hpp"
#include "rimau/http/transaction.hpp"
#include "rimau/http/virtual_host.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

class CapturingSink final : public rimau::http::ResponseSink {
public:
    void send(rimau::http::Response response, rimau::http::BodyMode body_mode = rimau::http::BodyMode::include) override
    {
        response_ = std::move(response);
        body_mode_ = body_mode;
        sent_ = true;
    }

    bool sent() const noexcept override
    {
        return sent_;
    }

    const rimau::http::Response& response() const
    {
        assert(response_.has_value());
        return *response_;
    }

private:
    bool sent_ = false;
    std::optional<rimau::http::Response> response_;
    std::optional<rimau::http::BodyMode> body_mode_;
};

rimau::http::Request make_request(std::string host)
{
    rimau::http::Request request;
    request.method = "GET";
    request.target = "/";
    request.path = "/";
    request.version = "HTTP/1.1";
    request.headers["host"] = std::move(host);
    return request;
}

std::filesystem::path make_root(const std::string& name, const std::string& body)
{
    const auto root = std::filesystem::temp_directory_path() / ("rimau-vhost-test-" + name);
    std::filesystem::create_directories(root);
    std::ofstream index(root / "index.html", std::ios::binary);
    index << body;
    return root;
}

} // namespace

int main()
{
    const auto default_root = make_root("default", "default host");
    const auto site_root = make_root("site", "site host");
    const auto wildcard_root = make_root("wildcard", "wildcard host");

    const std::string rules =
        "site.test=static:" + site_root.string()
        + ";*.wild.test=static:" + wildcard_root.string()
        + ";api.test=proxy:http://127.0.0.1:19090/api,https://backend.test/secure"
        + ";app.test=script:php:" + site_root.string();

    {
        const auto parsed = rimau::http::parse_virtual_host_rules(rules);
        assert(parsed.size() == 4);
        assert(parsed[2].action == rimau::http::VirtualHostAction::reverse_proxy);
        assert(parsed[2].upstreams.size() == 2);
        assert(parsed[2].upstreams[0].scheme == "http");
        assert(parsed[2].upstreams[0].host == "127.0.0.1");
        assert(parsed[2].upstreams[0].port == 19090);
        assert(parsed[2].upstreams[0].base_path == "/api");
        assert(parsed[2].upstreams[1].scheme == "https");
        assert(parsed[2].upstreams[1].host == "backend.test");
        assert(parsed[2].upstreams[1].port == 443);
        assert(parsed[2].upstreams[1].base_path == "/secure");
        assert(rimau::http::reverse_proxy_target_path(parsed[2].upstreams[0], "/users?id=7") == "/api/users?id=7");
        assert(rimau::http::reverse_proxy_target_path(parsed[2].upstreams[1], "status") == "/secure/status");
        assert(parsed[3].action == rimau::http::VirtualHostAction::server_side_script);
        assert(parsed[3].script_runtime == "php");
    }

    rimau::http::ReverseProxySettings proxy_settings;
    rimau::http::VirtualHostHandlerFactory factory(default_root, rules, proxy_settings);

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(1, make_request("site.test"));
        transaction.dispatch(factory, sink);
        assert(sink.response().status == 200);
        assert(sink.response().body == "site host");
    }

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(2, make_request("one.wild.test:8080"));
        transaction.dispatch(factory, sink);
        assert(sink.response().status == 200);
        assert(sink.response().body == "wildcard host");
    }

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(3, make_request("missing.test"));
        transaction.dispatch(factory, sink);
        assert(sink.response().status == 200);
        assert(sink.response().body == "default host");
    }

    {
        CapturingSink sink;
        rimau::http::Transaction transaction(4, make_request("app.test"));
        transaction.dispatch(factory, sink);
        assert(sink.response().status == 501);
        assert(sink.response().headers.at("x-rimau-runtime-status") == "planned");
        assert(sink.response().body.find("\"runtime\":\"php\"") != std::string::npos);
    }

    bool invalid_failed = false;
    try {
        (void)rimau::http::parse_virtual_host_rules("bad-entry");
    } catch (const std::runtime_error&) {
        invalid_failed = true;
    }
    assert(invalid_failed);

    bool invalid_port_failed = false;
    try {
        (void)rimau::http::parse_virtual_host_rules("api.test=proxy:http://127.0.0.1:70000");
    } catch (const std::runtime_error&) {
        invalid_port_failed = true;
    }
    assert(invalid_port_failed);

    bool invalid_scheme_failed = false;
    try {
        (void)rimau::http::parse_virtual_host_rules("api.test=proxy:ftp://127.0.0.1:21");
    } catch (const std::runtime_error&) {
        invalid_scheme_failed = true;
    }
    assert(invalid_scheme_failed);

    {
        auto upstream = rimau::http::parse_reverse_proxy_target("http://203.0.113.77:19555/circuit-test");
        rimau::http::ReverseProxySettings settings;
        settings.circuit_breaker_enabled = true;
        settings.circuit_breaker_failure_threshold = 2;
        settings.circuit_breaker_cooldown_seconds = 60;

        rimau::http::record_reverse_proxy_upstream_success(upstream, settings);
        assert(rimau::http::reverse_proxy_upstream_available(upstream, settings));
        rimau::http::record_reverse_proxy_upstream_failure(upstream, settings);
        assert(rimau::http::reverse_proxy_upstream_available(upstream, settings));
        rimau::http::record_reverse_proxy_upstream_failure(upstream, settings);
        assert(!rimau::http::reverse_proxy_upstream_available(upstream, settings));
        rimau::http::record_reverse_proxy_upstream_success(upstream, settings);
        assert(rimau::http::reverse_proxy_upstream_available(upstream, settings));
    }

    {
        auto upstream = rimau::http::parse_reverse_proxy_target("http://203.0.113.78:19556/circuit-disabled");
        rimau::http::ReverseProxySettings settings;
        settings.circuit_breaker_enabled = false;
        settings.circuit_breaker_failure_threshold = 1;
        settings.circuit_breaker_cooldown_seconds = 60;
        rimau::http::record_reverse_proxy_upstream_failure(upstream, settings);
        assert(rimau::http::reverse_proxy_upstream_available(upstream, settings));
    }

    std::filesystem::remove_all(default_root);
    std::filesystem::remove_all(site_root);
    std::filesystem::remove_all(wildcard_root);
    return 0;
}
