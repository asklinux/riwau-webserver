#pragma once

#include "rimau/http/request.hpp"
#include "rimau/http/request_handler_factory.hpp"
#include "rimau/http/response.hpp"
#include "rimau/http/waf.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rimau::http {

enum class VirtualHostAction {
    static_files,
    reverse_proxy,
    server_side_script
};

enum class ReverseProxyLoadBalancingPolicy {
    round_robin,
    failover,
    stable_hash
};

struct ReverseProxyTarget {
    std::string scheme = "http";
    std::string host;
    std::uint16_t port = 80;
    std::string base_path;
    std::string authority;
};

struct ReverseProxySettings {
    int connect_timeout_seconds = 5;
    int read_timeout_seconds = 30;
    std::size_t max_response_bytes = 1024 * 1024;
    std::size_t retry_count = 1;
    bool verify_tls_upstream = false;
    bool circuit_breaker_enabled = true;
    std::size_t circuit_breaker_failure_threshold = 3;
    int circuit_breaker_cooldown_seconds = 10;
    ReverseProxyLoadBalancingPolicy load_balancing_policy = ReverseProxyLoadBalancingPolicy::round_robin;
};

struct VirtualHostRule {
    std::string host_pattern;
    VirtualHostAction action = VirtualHostAction::static_files;
    std::filesystem::path document_root;
    std::vector<ReverseProxyTarget> upstreams;
    std::string script_runtime;
    std::filesystem::path script_root;
};

struct VirtualHostWafOverride {
    std::string host_pattern;
    std::optional<bool> enabled;
    std::optional<bool> owasp_crs_enabled;
    std::optional<bool> blocking_enabled;
    std::optional<std::size_t> anomaly_threshold;
    std::vector<int> rule_exceptions;
};

std::string normalize_host(std::string_view host_header);
ReverseProxyTarget parse_reverse_proxy_target(std::string_view value);
std::vector<ReverseProxyTarget> parse_reverse_proxy_targets(std::string_view value);
ReverseProxyLoadBalancingPolicy parse_reverse_proxy_load_balancing_policy(std::string_view value);
std::string_view reverse_proxy_load_balancing_policy_name(ReverseProxyLoadBalancingPolicy policy);
std::string reverse_proxy_target_path(const ReverseProxyTarget& upstream, std::string_view request_target);
std::string build_reverse_proxy_upstream_request(const Request& request, const ReverseProxyTarget& upstream);
Response parse_reverse_proxy_upstream_response(std::string raw_response);
std::vector<ReverseProxyTarget> ordered_reverse_proxy_upstreams(
    const Request& request,
    const std::vector<ReverseProxyTarget>& upstreams,
    const ReverseProxySettings& settings);
std::vector<VirtualHostRule> parse_virtual_host_rules(std::string_view value);
const VirtualHostRule* select_virtual_host_rule(const Request& request, const std::vector<VirtualHostRule>& rules);
std::vector<VirtualHostWafOverride> parse_virtual_host_waf_overrides(std::string_view value);
const VirtualHostWafOverride* select_virtual_host_waf_override(const Request& request, const std::vector<VirtualHostWafOverride>& overrides);
WafSettings apply_virtual_host_waf_override(WafSettings settings, const VirtualHostWafOverride* override);
bool reverse_proxy_upstream_available(const ReverseProxyTarget& upstream, const ReverseProxySettings& settings);
void record_reverse_proxy_upstream_success(const ReverseProxyTarget& upstream, const ReverseProxySettings& settings);
void record_reverse_proxy_upstream_failure(const ReverseProxyTarget& upstream, const ReverseProxySettings& settings);

class VirtualHostHandlerFactory final : public RequestHandlerFactory {
public:
    VirtualHostHandlerFactory(
        std::filesystem::path default_document_root,
        std::string virtual_hosts_config,
        ReverseProxySettings proxy_settings,
        StaticFileOptions static_file_options = {});

    std::unique_ptr<RequestHandler> create(const Request& request) const override;

private:
    std::filesystem::path default_document_root_;
    std::vector<VirtualHostRule> rules_;
    ReverseProxySettings proxy_settings_;
    StaticFileOptions static_file_options_;
};

} // namespace rimau::http
