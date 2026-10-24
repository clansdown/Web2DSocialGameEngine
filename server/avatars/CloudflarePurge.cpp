#include "CloudflarePurge.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string_view>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace {

std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

std::string json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

size_t collect_response(char* data, size_t size, size_t count, void* userdata) {
    const size_t bytes = size * count;
    auto* body = static_cast<std::string*>(userdata);
    if (body != nullptr && body->size() < 4096) {
        body->append(data, std::min(bytes, 4096 - body->size()));
    }
    return bytes;
}

} // namespace

std::optional<cloudflare_purge_config> load_cloudflare_purge_config(std::string& error) {
    error.clear();
    cloudflare_purge_config config;
    config.zone_id = env_value("RAVENEST_CLOUDFLARE_ZONE_ID");
    config.api_token = env_value("RAVENEST_CLOUDFLARE_API_TOKEN");
    if (config.zone_id.empty() && config.api_token.empty()) {
        return config;
    }
    if (config.zone_id.empty() || config.api_token.empty()) {
        error = "RAVENEST_CLOUDFLARE_ZONE_ID and RAVENEST_CLOUDFLARE_API_TOKEN "
                "must be set together";
        return std::nullopt;
    }
    return config;
}

bool cloudflare_purge_urls(const cloudflare_purge_config& config,
                           const std::vector<std::string>& urls,
                           std::string& error) {
    error.clear();
    if (!config.configured()) {
        error = "cloudflare purge is not configured";
        return false;
    }
    if (urls.empty()) return true;

    std::string body = "{\"files\":[";
    for (std::size_t i = 0; i < urls.size(); ++i) {
        if (i != 0) body.push_back(',');
        body += "\"" + json_escape(urls[i]) + "\"";
    }
    body += "]}";

    const std::string url =
        "https://api.cloudflare.com/client/v4/zones/" + config.zone_id +
        "/purge_cache";

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(
        headers, ("Authorization: Bearer " + config.api_token).c_str());

    char error_buffer[CURL_ERROR_SIZE] = {};
    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "Cloudflare purge returned HTTP " + std::to_string(status);
        return false;
    }
    try {
        const nlohmann::json parsed = nlohmann::json::parse(response_body);
        if (!parsed.value("success", false)) {
            error = "Cloudflare purge reported success=false";
            return false;
        }
    } catch (const std::exception&) {
        error = "Cloudflare purge returned an invalid JSON response";
        return false;
    }
    return true;
}
