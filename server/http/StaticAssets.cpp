#include "http/StaticAssets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace http_assets {

namespace {

constexpr std::uintmax_t kMaxAssetBytes = 8u * 1024u * 1024u;

std::string lower_ascii(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

asset_response not_found() {
    asset_response response;
    response.status = 404;
    return response;
}

bool is_within_root(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::string root_text = root.generic_string();
    const std::string candidate_text = candidate.generic_string();
    if (candidate_text == root_text) return true;
    if (candidate_text.size() <= root_text.size()) return false;
    if (candidate_text.compare(0, root_text.size(), root_text) != 0) return false;
    return candidate_text[root_text.size()] == '/';
}

}  // namespace

std::string content_type_for_path(const std::string& path) {
    const std::string lower = lower_ascii(path);
    if (ends_with(lower, ".png")) return "image/png";
    if (ends_with(lower, ".jpg") || ends_with(lower, ".jpeg")) return "image/jpeg";
    if (ends_with(lower, ".webp")) return "image/webp";
    if (ends_with(lower, ".avif")) return "image/avif";
    if (ends_with(lower, ".gif")) return "image/gif";
    if (ends_with(lower, ".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

asset_response load_static_asset(const std::string& root,
                                 const std::string& relative) {
    if (root.empty() || relative.empty()) return not_found();
    if (relative.find('\0') != std::string::npos) return not_found();
    if (relative.front() == '/' || relative.front() == '\\') return not_found();
    if (relative.find('\\') != std::string::npos) return not_found();

    std::error_code error;
    const std::filesystem::path root_path =
        std::filesystem::weakly_canonical(std::filesystem::path(root), error);
    if (error) return not_found();
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(root_path / relative, error);
    if (error || !is_within_root(root_path, resolved)) return not_found();
    if (!std::filesystem::is_regular_file(resolved, error) || error) return not_found();

    const std::uintmax_t size = std::filesystem::file_size(resolved, error);
    if (error) return not_found();
    if (size > kMaxAssetBytes) {
        asset_response response;
        response.status = 413;
        return response;
    }

    std::ifstream file(resolved, std::ios::binary);
    if (!file) return not_found();
    asset_response response;
    response.status = 200;
    response.content_type = content_type_for_path(relative);
    response.body.assign(std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof()) return not_found();
    return response;
}

}  // namespace http_assets
