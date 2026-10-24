#include "AvatarStore.hpp"

#include "AvatarValidation.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace {

std::string env_value(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

std::string lower_ascii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string normalize_prefix(std::string prefix, std::string& error) {
    while (!prefix.empty() && prefix.front() == '/') prefix.erase(prefix.begin());
    while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    if (prefix.empty()) prefix = "avatars";
    if (prefix.find("..") != std::string::npos ||
        prefix.find('?') != std::string::npos ||
        prefix.find('#') != std::string::npos) {
        error = "RAVENEST_AVATAR_R2_PREFIX must be a simple object prefix";
        return {};
    }
    for (char c : prefix) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            error = "RAVENEST_AVATAR_R2_PREFIX must not contain whitespace";
            return {};
        }
    }
    return prefix + "/";
}

std::string normalize_base_url(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

} // namespace

std::string avatar_storage_kind_name(avatar_storage_kind kind) {
    return kind == avatar_storage_kind::r2 ? "r2" : "local";
}

std::optional<avatar_store_config> load_avatar_store_config(std::string& error) {
    error.clear();
    avatar_store_config config;

    const std::string kind = lower_ascii(env_value("RAVENEST_AVATAR_STORAGE"));
    if (kind.empty() || kind == "local") {
        config.kind = avatar_storage_kind::local;
    } else if (kind == "r2") {
        config.kind = avatar_storage_kind::r2;
    } else {
        error = "RAVENEST_AVATAR_STORAGE must be 'local' or 'r2'";
        return std::nullopt;
    }

    const std::string local_root = env_value("RAVENEST_AVATAR_LOCAL_ROOT");
    if (!local_root.empty()) config.local_root = local_root;

    const std::string prefix = env_value("RAVENEST_AVATAR_R2_PREFIX");
    config.r2_prefix = normalize_prefix(prefix.empty() ? "avatars/" : prefix, error);
    if (!error.empty()) return std::nullopt;

    config.public_base_url =
        normalize_base_url(env_value("RAVENEST_AVATAR_PUBLIC_BASE_URL"));

    config.r2.endpoint = env_value("RAVENEST_R2_ENDPOINT");
    config.r2.bucket = env_value("RAVENEST_R2_BUCKET");
    config.r2.access_key_id = env_value("RAVENEST_R2_ACCESS_KEY_ID");
    config.r2.secret_access_key = env_value("RAVENEST_R2_SECRET_ACCESS_KEY");
    const std::string region = env_value("RAVENEST_R2_REGION");
    if (!region.empty()) config.r2.region = region;

    if (config.kind == avatar_storage_kind::r2) {
        if (config.public_base_url.empty()) {
            error = "RAVENEST_AVATAR_PUBLIC_BASE_URL is required when RAVENEST_AVATAR_STORAGE=r2";
            return std::nullopt;
        }
        if (config.r2.endpoint.empty() || config.r2.bucket.empty() ||
            config.r2.access_key_id.empty() || config.r2.secret_access_key.empty()) {
            error = "RAVENEST_R2_ENDPOINT, RAVENEST_R2_BUCKET, "
                    "RAVENEST_R2_ACCESS_KEY_ID, and RAVENEST_R2_SECRET_ACCESS_KEY "
                    "are required when RAVENEST_AVATAR_STORAGE=r2";
            return std::nullopt;
        }
        if (config.r2.endpoint.rfind("https://", 0) != 0 &&
            config.r2.endpoint.rfind("http://", 0) != 0) {
            error = "RAVENEST_R2_ENDPOINT must start with https:// or http://";
            return std::nullopt;
        }
    }

    if (config.public_base_url.rfind("http://", 0) != 0 &&
        config.public_base_url.rfind("https://", 0) != 0) {
        if (!config.public_base_url.empty()) {
            error = "RAVENEST_AVATAR_PUBLIC_BASE_URL must start with http:// or https://";
            return std::nullopt;
        }
    }

    return config;
}

local_avatar_store::local_avatar_store(std::string root)
    : root_(std::move(root)) {}

std::string local_avatar_store::path_for(std::string_view key) const {
    return root_ + "/" + std::string(key);
}

bool local_avatar_store::put(std::string_view key, std::string_view bytes,
                             std::string& error) {
    error.clear();
    if (!avatar_key_is_valid(key)) {
        error = "invalid_avatar_key";
        return false;
    }

    namespace fs = std::filesystem;
    const fs::path path(path_for(key));
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "failed to create avatar directory: " + ec.message();
        return false;
    }

    static std::atomic<unsigned long> temp_counter{0};
    const fs::path temp_path =
        path.string() + ".tmp." + std::to_string(getpid()) + "." +
        std::to_string(temp_counter.fetch_add(1));

    {
        std::ofstream out(temp_path.string(), std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "failed to open avatar file for writing";
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "failed to write avatar file";
            out.close();
            fs::remove(temp_path, ec);
            return false;
        }
    }

    fs::rename(temp_path, path, ec);
    if (ec) {
        // POSIX rename replaces an existing destination; a failure here is a
        // real storage error, so do not leave a partial temp file behind.
        fs::remove(temp_path, ec);
        error = "failed to commit avatar file";
        return false;
    }
    return true;
}

bool local_avatar_store::remove(std::string_view key, std::string& error) {
    error.clear();
    if (!avatar_key_is_valid(key)) {
        error = "invalid_avatar_key";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path_for(key)), ec);
    if (ec) {
        error = "failed to remove avatar file: " + ec.message();
        return false;
    }
    return true;
}

r2_avatar_store::r2_avatar_store(r2_config config, std::string prefix,
                                 std::string public_base_url)
    : client_(std::move(config)),
      prefix_(std::move(prefix)),
      public_base_url_(std::move(public_base_url)) {}

bool r2_avatar_store::put(std::string_view key, std::string_view bytes,
                          std::string_view content_type, std::string& error) {
    if (!avatar_key_is_valid(key)) {
        error = "invalid_avatar_key";
        return false;
    }
    return client_.put_object(prefix_ + std::string(key), bytes, content_type,
                              "public, max-age=31536000, immutable", error);
}

bool r2_avatar_store::remove(std::string_view key, std::string& error) {
    if (!avatar_key_is_valid(key)) {
        error = "invalid_avatar_key";
        return false;
    }
    return client_.delete_object(prefix_ + std::string(key), error);
}

std::string r2_avatar_store::public_url(std::string_view key) const {
    if (public_base_url_.empty()) return "";
    return public_base_url_ + "/" + prefix_ + std::string(key);
}

avatar_store::avatar_store(avatar_store_config config)
    : config_(std::move(config)), local_(config_.local_root) {
    const bool has_r2_config =
        !config_.r2.endpoint.empty() && !config_.r2.bucket.empty() &&
        !config_.r2.access_key_id.empty() && !config_.r2.secret_access_key.empty();
    if (has_r2_config) {
        remote_ = std::make_unique<r2_avatar_store>(
            config_.r2, config_.r2_prefix, config_.public_base_url);
    }
}

bool avatar_store::put(std::string_view key, std::string_view bytes,
                       std::string_view content_type, std::string& error) {
    if (config_.kind == avatar_storage_kind::r2) {
        if (!remote_) {
            error = "avatar_storage_unavailable";
            return false;
        }
        return remote_->put(key, bytes, content_type, error);
    }
    return local_.put(key, bytes, error);
}

bool avatar_store::remove(avatar_storage_kind kind, std::string_view key,
                          std::string& error) {
    if (kind == avatar_storage_kind::r2) {
        if (!remote_) {
            error = "r2 avatar storage is not configured";
            return false;
        }
        return remote_->remove(key, error);
    }
    return local_.remove(key, error);
}

std::string avatar_store::public_url(avatar_storage_kind kind,
                                     std::string_view key) const {
    if (kind == avatar_storage_kind::r2 && remote_) return remote_->public_url(key);
    return "";
}
