#pragma once
#include <nlohmann/json.hpp>
#include <optional>

struct ApiResponse {
    nlohmann::json data;
    std::optional<std::string> error;
    std::optional<std::string> error_code;
    bool needs_auth = false;
    bool auth_failed = false;

    // Transport-only fields. They are never serialized into the JSON body.
    std::optional<std::string> refresh_token;
    bool clear_refresh_cookie = false;
    bool no_store = false;
    int http_status = 0;

    nlohmann::json toJson() const {
        nlohmann::json result;
        result["data"] = data.is_null() ? nlohmann::json::object() : data;
        result["status"] = "ok";
        if (error) result["error"] = *error;
        if (error_code) result["error_code"] = *error_code;
        result["needs_auth"] = needs_auth;
        result["auth_failed"] = auth_failed;
        return result;
    }
};