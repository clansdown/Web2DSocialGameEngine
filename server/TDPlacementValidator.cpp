#include "TDPlacementValidator.hpp"
#include <nlohmann/json.hpp>
#include <vector>
#include <cmath>
#include <iostream>

using json = nlohmann::json;

bool pointInCircle(double px, double py, double cx, double cy, double r) {
    double dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

bool pointInPolygon(double px, double py, const std::vector<std::pair<double, double>>& verts) {
    bool inside = false;
    for (size_t i = 0, j = verts.size() - 1; i < verts.size(); j = i++) {
        double xi = verts[i].first, yi = verts[i].second;
        double xj = verts[j].first, yj = verts[j].second;
        if ((yi > py) != (yj > py) &&
            px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<PathBufferCircle> generatePathBuffers(const json& path, double bufferR) {
    std::vector<PathBufferCircle> zones;
    auto wps = path["waypoints"];
    for (auto& wp : wps) {
        double x = wp["x"].get<double>();
        double y = wp["y"].get<double>();
        zones.push_back({x, y, bufferR});
    }
    for (size_t i = 1; i < wps.size(); i++) {
        double px = wps[i - 1]["x"].get<double>();
        double py = wps[i - 1]["y"].get<double>();
        double cx = wps[i]["x"].get<double>();
        double cy = wps[i]["y"].get<double>();
        double segLen = std::hypot(cx - px, cy - py);
        int steps = static_cast<int>(std::ceil(segLen / (bufferR * 0.6)));
        for (int s = 1; s < steps; s++) {
            double t = static_cast<double>(s) / steps;
            zones.push_back({px + (cx - px) * t, py + (cy - py) * t, bufferR});
        }
    }
    return zones;
}

static void parseVertices(const json& zone, std::vector<std::pair<double, double>>& verts) {
    verts.clear();
    for (auto& v : zone["vertices"]) {
        verts.emplace_back(v["x"].get<double>(), v["y"].get<double>());
    }
}

PlacementValidationResult validateTDPlacement(
    double x,
    double y,
    const json& map_metadata,
    const json& client_placements,
    const std::string& exclude_id,
    double placing_radius)
{
    // 1. Check map exclusion zones
    if (map_metadata.contains("exclusion_zones") && map_metadata["exclusion_zones"].is_array()) {
        for (auto& zone : map_metadata["exclusion_zones"]) {
            std::string type = zone.value("type", "");
            if (type == "circle") {
                double cx = zone["center_x"].get<double>();
                double cy = zone["center_y"].get<double>();
                double r = zone["radius"].get<double>();
                if (pointInCircle(x, y, cx, cy, r)) {
                    return {false, "Placement inside exclusion zone"};
                }
            } else if (type == "polygon" && zone.contains("vertices")) {
                std::vector<std::pair<double, double>> verts;
                parseVertices(zone, verts);
                if (pointInPolygon(x, y, verts)) {
                    return {false, "Placement inside exclusion zone"};
                }
            }
        }
    }

    // 2. Check path buffers
    const double bufferR = 30.0;
    if (map_metadata.contains("paths") && map_metadata["paths"].is_array()) {
        for (auto& path : map_metadata["paths"]) {
            auto buffers = generatePathBuffers(path, bufferR);
            for (auto& pb : buffers) {
                double effectiveR = std::max(pb.r, placing_radius / 2.0);
                if (pointInCircle(x, y, pb.cx, pb.cy, effectiveR)) {
                    return {false, "Placement blocked by path buffer"};
                }
            }
        }
    }

    // 3. Check overlap with other placed combatants
    for (auto& p : client_placements) {
        std::string pid = p.value("id", "");
        if (pid == exclude_id) continue;
        double px = p["x"].get<double>();
        double py = p["y"].get<double>();
        double otherR = p.value("exclusion_radius", 0.0);
        double combinedR = std::max(otherR, placing_radius);
        if (combinedR > 0 && std::hypot(px - x, py - y) < combinedR) {
            return {false, "Placement overlaps existing unit"};
        }
    }

    return {true, ""};
}

double getPlacementCost(const json& towers, const json& units, const std::string& config_id) {
    if (towers.contains("towers") && towers["towers"].contains(config_id)) {
        auto& entry = towers["towers"][config_id];
        if (entry.contains("cost") && entry["cost"].is_array() && !entry["cost"].empty()) {
            return entry["cost"][0].value("gold", 0);
        }
    }
    if (units.contains("units") && units["units"].contains(config_id)) {
        auto& entry = units["units"][config_id];
        if (entry.contains("cost") && entry["cost"].is_array() && !entry["cost"].empty()) {
            return entry["cost"][0].value("gold", 0);
        }
    }
    return 0.0;
}
