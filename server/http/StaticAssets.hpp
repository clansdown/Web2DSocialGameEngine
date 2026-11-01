#pragma once
// Off-loop static asset loading shared by /images/* and the local avatar
// stream. Path validation and the whole-file read run on the dedicated file
// pool; the uWS loop only writes the resulting headers/body.
#include <string>

namespace http_assets {

struct asset_response {
    int status = 200;
    std::string content_type;
    std::string body;
    std::string location;
};

// Resolves `relative` under `root`, rejects traversal/absolute/NUL paths, and
// reads the file. Missing/unreadable -> 404; oversized -> 413.
asset_response load_static_asset(const std::string& root,
                                 const std::string& relative);

std::string content_type_for_path(const std::string& path);

}  // namespace http_assets
