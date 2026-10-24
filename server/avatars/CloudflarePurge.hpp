#pragma once

#include <optional>
#include <string>
#include <vector>

struct cloudflare_purge_config {
    std::string zone_id;
    std::string api_token;

    bool configured() const {
        return !zone_id.empty() && !api_token.empty();
    }
};

// Load the optional purge credentials. Missing credentials are not an error:
// the server can still upload and delete origin objects, it just cannot purge
// an already-cached edge URL.
std::optional<cloudflare_purge_config> load_cloudflare_purge_config(std::string& error);

// Purge exact asset URLs through the Cloudflare cache-purge API.
bool cloudflare_purge_urls(const cloudflare_purge_config& config,
                           const std::vector<std::string>& urls,
                           std::string& error);
