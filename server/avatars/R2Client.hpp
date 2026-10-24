#pragma once

#include <string>
#include <string_view>

struct r2_config {
    std::string endpoint;
    std::string bucket;
    std::string access_key_id;
    std::string secret_access_key;
    std::string region = "auto";
};

// Minimal S3-compatible client for Cloudflare R2. Only the operations needed by
// the avatar store are implemented: single-part PUT and DELETE.
class r2_client {
public:
    explicit r2_client(r2_config config);

    r2_client(const r2_client&) = delete;
    r2_client& operator=(const r2_client&) = delete;

    bool put_object(std::string_view key,
                    std::string_view bytes,
                    std::string_view content_type,
                    std::string_view cache_control,
                    std::string& error);

    bool delete_object(std::string_view key, std::string& error);

private:
    bool request(std::string_view method,
                 std::string_view key,
                 std::string_view body,
                 std::string_view content_type,
                 std::string_view cache_control,
                 std::string& error);

    r2_config config_;
};
