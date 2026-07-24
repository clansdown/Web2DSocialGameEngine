#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cmath>

using json = nlohmann::json;

bool pointInCircle(double px, double py, double cx, double cy, double r);

bool pointInPolygon(double px, double py, const std::vector<std::pair<double, double>>& verts);

struct PathBufferCircle {
    double cx;
    double cy;
    double r;
};

std::vector<PathBufferCircle> generatePathBuffers(const json& path, double bufferR);

struct PlacementValidationResult {
    bool valid;
    std::string error;
};

PlacementValidationResult validateTDPlacement(
    double x,
    double y,
    const json& map_metadata,
    const json& client_placements,
    const std::string& exclude_id,
    double placing_radius);

double getPlacementCost(const json& towers, const json& units, const std::string& config_id);