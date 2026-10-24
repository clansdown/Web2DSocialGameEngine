#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "avatars/R2Client.hpp"

enum class avatar_storage_kind {
    local,
    r2,
};

struct avatar_store_config {
    avatar_storage_kind kind = avatar_storage_kind::local;
    std::string local_root = "uploads/avatars";
    std::string r2_prefix = "avatars/";
    std::string public_base_url;
    r2_config r2;
};

// Load the avatar backend configuration from the server environment. Returns
// nullopt with a human-readable error when an r2 configuration is incomplete.
std::optional<avatar_store_config> load_avatar_store_config(std::string& error);

std::string avatar_storage_kind_name(avatar_storage_kind kind);

class local_avatar_store {
public:
    explicit local_avatar_store(std::string root);

    bool put(std::string_view key, std::string_view bytes, std::string& error);
    bool remove(std::string_view key, std::string& error);
    std::string path_for(std::string_view key) const;

private:
    std::string root_;
};

class r2_avatar_store {
public:
    r2_avatar_store(r2_config config, std::string prefix, std::string public_base_url);

    bool put(std::string_view key, std::string_view bytes,
             std::string_view content_type, std::string& error);
    bool remove(std::string_view key, std::string& error);
    std::string public_url(std::string_view key) const;

private:
    r2_client client_;
    std::string prefix_;
    std::string public_base_url_;
};

// Facade over both backends. The active backend handles new writes; removal can
// target either backend so an avatar written before a storage-mode change can
// still be cleaned up.
class avatar_store {
public:
    explicit avatar_store(avatar_store_config config);

    bool put(std::string_view key, std::string_view bytes,
             std::string_view content_type, std::string& error);
    bool remove(avatar_storage_kind kind, std::string_view key, std::string& error);
    std::string public_url(avatar_storage_kind kind, std::string_view key) const;

    avatar_storage_kind active_kind() const { return config_.kind; }
    const avatar_store_config& config() const { return config_; }

private:
    avatar_store_config config_;
    local_avatar_store local_;
    std::unique_ptr<r2_avatar_store> remote_;
};
