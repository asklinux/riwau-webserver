#pragma once

#include "rimau/http/request.hpp"
#include "rimau/http/response.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rimau::http {

struct WafSettings {
    bool enabled = false;
    bool owasp_crs_enabled = true;
    bool blocking_enabled = true;
    std::size_t anomaly_threshold = 5;
    std::size_t max_inspection_bytes = 128 * 1024;
};

struct WafMatch {
    int rule_id = 0;
    std::string severity;
    std::size_t score = 0;
    std::string tag;
    std::string message;
    std::string variable;
    std::string evidence;
};

struct WafResult {
    bool inspected = false;
    bool allowed = true;
    std::size_t anomaly_score = 0;
    std::vector<WafMatch> matches;
    std::string engine = "rimau-modsecurity-compatible";
    std::string ruleset = "builtin-owasp-crs-subset";

    std::optional<int> first_rule_id() const;
};

WafResult inspect_request(const Request& request, const WafSettings& settings);
Response waf_block_response(const WafResult& result);

} // namespace rimau::http
