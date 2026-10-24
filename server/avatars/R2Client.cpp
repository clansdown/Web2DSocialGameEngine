#include "R2Client.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "Hash.hpp"

namespace {

struct response_buffer {
    std::string body;
};

size_t write_response(char* data, size_t size, size_t count, void* userdata) {
    const size_t bytes = size * count;
    auto* buffer = static_cast<response_buffer*>(userdata);
    if (buffer != nullptr && buffer->body.size() < 2048) {
        const size_t remaining = 2048 - buffer->body.size();
        buffer->body.append(data, std::min(bytes, remaining));
    }
    return bytes;
}

std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string aws_uri_encode(std::string_view value, bool encode_slash) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == '/' && !encode_slash) {
            out.push_back('/');
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string utc_timestamp(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), format, &utc);
    return buffer;
}

bool parse_endpoint(std::string_view endpoint, std::string& origin,
                    std::string& host, std::string& error) {
    const std::string value(endpoint);
    const std::string https_prefix = "https://";
    const std::string http_prefix = "http://";
    std::string scheme;
    if (value.rfind(https_prefix, 0) == 0) {
        scheme = https_prefix;
    } else if (value.rfind(http_prefix, 0) == 0) {
        scheme = http_prefix;
    } else {
        error = "R2 endpoint must start with https:// or http://";
        return false;
    }

    std::string rest = value.substr(scheme.size());
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    if (rest.empty() || rest.find('/') != std::string::npos ||
        rest.find('?') != std::string::npos || rest.find('#') != std::string::npos) {
        error = "R2 endpoint must be an origin with no path, query, or fragment";
        return false;
    }
    origin = scheme + rest;
    host = rest;
    return true;
}

struct http_result {
    bool ok = false;
    long status = 0;
    std::string error;
};

http_result perform_request(const std::string& method,
                            const std::string& url,
                            const std::vector<std::string>& headers,
                            std::string_view body,
                            bool has_body) {
    http_result result;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "curl_easy_init failed";
        return result;
    }

    curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        header_list = curl_slist_append(header_list, header.c_str());
    }

    response_buffer response;
    char error_buffer[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PATH_AS_IS, 1L);
    if (has_body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
        if (result.status >= 200 && result.status < 300) {
            result.ok = true;
        } else {
            result.error = "R2 returned HTTP " + std::to_string(result.status);
            if (!response.body.empty()) {
                result.error += ": " + response.body.substr(0, 512);
            }
        }
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return result;
}

} // namespace

r2_client::r2_client(r2_config config) : config_(std::move(config)) {}

bool r2_client::put_object(std::string_view key,
                           std::string_view bytes,
                           std::string_view content_type,
                           std::string_view cache_control,
                           std::string& error) {
    return request("PUT", key, bytes, content_type, cache_control, error);
}

bool r2_client::delete_object(std::string_view key, std::string& error) {
    return request("DELETE", key, "", "", "", error);
}

bool r2_client::request(std::string_view method,
                        std::string_view key,
                        std::string_view body,
                        std::string_view content_type,
                        std::string_view cache_control,
                        std::string& error) {
    error.clear();
    if (config_.endpoint.empty() || config_.bucket.empty() ||
        config_.access_key_id.empty() || config_.secret_access_key.empty()) {
        error = "R2 credentials are incomplete";
        return false;
    }
    if (key.empty()) {
        error = "R2 object key is empty";
        return false;
    }

    std::string origin;
    std::string host;
    if (!parse_endpoint(config_.endpoint, origin, host, error)) return false;

    const std::string encoded_bucket = aws_uri_encode(config_.bucket, true);
    const std::string encoded_key = aws_uri_encode(key, false);
    const std::string canonical_uri = "/" + encoded_bucket + "/" + encoded_key;
    const std::string url = origin + canonical_uri;
    const std::string payload_hash = sha256_hex(body);

    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string amz_date = utc_timestamp("%Y%m%dT%H%M%SZ");
        const std::string date_stamp = utc_timestamp("%Y%m%d");

        std::map<std::string, std::string> canonical_headers;
        canonical_headers["host"] = host;
        canonical_headers["x-amz-content-sha256"] = payload_hash;
        canonical_headers["x-amz-date"] = amz_date;
        if (!content_type.empty()) {
            canonical_headers["content-type"] = std::string(content_type);
        }
        if (!cache_control.empty()) {
            canonical_headers["cache-control"] = std::string(cache_control);
        }

        std::string canonical_header_text;
        std::string signed_headers;
        for (const auto& [name, value] : canonical_headers) {
            canonical_header_text += name + ":" + trim_ascii(value) + "\n";
            if (!signed_headers.empty()) signed_headers += ";";
            signed_headers += name;
        }

        const std::string canonical_request =
            std::string(method) + "\n" + canonical_uri + "\n\n" +
            canonical_header_text + "\n" + signed_headers + "\n" + payload_hash;
        const std::string credential_scope =
            date_stamp + "/" + config_.region + "/s3/aws4_request";
        const std::string string_to_sign =
            "AWS4-HMAC-SHA256\n" + amz_date + "\n" + credential_scope + "\n" +
            sha256_hex(canonical_request);

        const std::string date_key =
            hmac_sha256_raw("AWS4" + config_.secret_access_key, date_stamp);
        const std::string region_key = hmac_sha256_raw(date_key, config_.region);
        const std::string service_key = hmac_sha256_raw(region_key, "s3");
        const std::string signing_key = hmac_sha256_raw(service_key, "aws4_request");
        const std::string signature = hmac_sha256_hex(signing_key, string_to_sign);

        const std::string authorization =
            "AWS4-HMAC-SHA256 Credential=" + config_.access_key_id + "/" +
            credential_scope + ", SignedHeaders=" + signed_headers +
            ", Signature=" + signature;

        std::vector<std::string> headers;
        headers.push_back("Host: " + host);
        headers.push_back("x-amz-content-sha256: " + payload_hash);
        headers.push_back("x-amz-date: " + amz_date);
        if (!content_type.empty()) {
            headers.push_back("Content-Type: " + std::string(content_type));
        }
        if (!cache_control.empty()) {
            headers.push_back("Cache-Control: " + std::string(cache_control));
        }
        headers.push_back("Authorization: " + authorization);
        headers.push_back("Expect:");

        http_result result = perform_request(
            std::string(method), url, headers, body, method == "PUT");
        if (result.ok) return true;

        const bool retryable = result.status == 0 || result.status == 429 ||
                               result.status >= 500;
        if (!retryable || attempt == 2) {
            error = result.error;
            return false;
        }
    }

    error = "R2 request failed";
    return false;
}
