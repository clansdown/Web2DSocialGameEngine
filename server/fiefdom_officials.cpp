#include "fiefdom_officials.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace Officials {

static std::string readFileToString(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

static int extrapolateWithMax(const std::vector<int>& array, int level, int maxVal) {
    if (array.empty()) {
        return maxVal > 0 ? std::min(0, maxVal) : 0;
    }

    if (level - 1 < static_cast<int>(array.size())) {
        return array[level - 1];
    }

    int lastIdx = array.size() - 1;
    int secondLastIdx = std::max(0, lastIdx - 1);
    int delta = array[lastIdx] - array[secondLastIdx];

    int extrapolated = array[lastIdx] + (level - static_cast<int>(array.size())) * delta;

    if (maxVal > 0) {
        return std::min(extrapolated, maxVal);
    }

    return extrapolated;
}

int StatArray::getValue(int level) const {
    return extrapolateWithMax(values, level, max);
}

int OfficialTemplate::getIntelligence(int level) const {
    return extrapolateWithMax(intelligence.values, level, intelligence.max);
}

int OfficialTemplate::getCharisma(int level) const {
    return extrapolateWithMax(charisma.values, level, charisma.max);
}

int OfficialTemplate::getWisdom(int level) const {
    return extrapolateWithMax(wisdom.values, level, wisdom.max);
}

int OfficialTemplate::getDiligence(int level) const {
    return extrapolateWithMax(diligence.values, level, diligence.max);
}

static StatArray parseStatArray(const nlohmann::json& json, const std::string& statName) {
    StatArray stat;

    if (json.contains(statName)) {
        for (const auto& val : json[statName]) {
            stat.values.push_back(val.get<int>());
        }
    }

    std::string maxFieldName = statName + "_max";
    if (json.contains(maxFieldName)) {
        stat.max = json[maxFieldName].get<int>();
    }

    return stat;
}

OfficialRegistry& OfficialRegistry::getInstance() {
    static OfficialRegistry instance;
    return instance;
}

bool OfficialRegistry::loadOfficials(const std::string& config_path) {
    std::string content = readFileToString(config_path);
    if (content.empty()) {
        std::cerr << "Failed to open fiefdom officials config: " << config_path << std::endl;
        return false;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(content, nullptr, true, true);

        if (!data.is_object()) {
            std::cerr << "Expected object with official definitions" << std::endl;
            return false;
        }

        for (auto& [officialId, officialJson] : data.items()) {
            OfficialTemplate official;
            official.id = officialId;

            if (officialJson.contains("name")) {
                official.name = officialJson["name"].get<std::string>();
            }

            if (officialJson.contains("max_level")) {
                official.max_level = officialJson["max_level"].get<int>();
            }

            if (officialJson.contains("roles")) {
                for (const auto& role : officialJson["roles"]) {
                    official.eligibleRoles.push_back(role.get<std::string>());
                }
            }

            if (officialJson.contains("morale_boost")) {
                for (auto& boost : officialJson["morale_boost"]) {
                    official.morale_boost.push_back(boost.get<double>());
                }
            }

            if (officialJson.contains("stats")) {
                const auto& statsJson = officialJson["stats"];
                official.intelligence = parseStatArray(statsJson, "intelligence");
                official.charisma = parseStatArray(statsJson, "charisma");
                official.wisdom = parseStatArray(statsJson, "wisdom");
                official.diligence = parseStatArray(statsJson, "diligence");
            }

            if (officialJson.contains("portrait_id")) {
                official.portrait_id = officialJson["portrait_id"].get<int>();
            }

            if (officialJson.contains("description")) {
                official.description = officialJson["description"].get<std::string>();
            }

            officials_[officialId] = std::move(official);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse fiefdom officials: " << e.what() << std::endl;
        return false;
    }
}

std::optional<const OfficialTemplate*> OfficialRegistry::getOfficial(const std::string& id) const {
    auto it = officials_.find(id);
    if (it != officials_.end()) {
        return std::optional<const OfficialTemplate*>(&it->second);
    }
    return std::nullopt;
}

const std::unordered_map<std::string, OfficialTemplate>& OfficialRegistry::getAllOfficials() const {
    return officials_;
}

std::vector<const OfficialTemplate*> OfficialRegistry::getEligibleOfficialsForRole(const std::string& role) const {
    std::vector<const OfficialTemplate*> result;

    for (const auto& [id, official] : officials_) {
        if (std::find(official.eligibleRoles.begin(), official.eligibleRoles.end(), role) != official.eligibleRoles.end()) {
            result.push_back(&official);
        }
    }

    return result;
}

std::vector<const OfficialTemplate*> OfficialRegistry::getEligibleOfficialsForRoles(const std::vector<std::string>& roles) const {
    std::vector<const OfficialTemplate*> result;

    for (const auto& [id, official] : officials_) {
        for (const std::string& role : roles) {
            if (std::find(official.eligibleRoles.begin(), official.eligibleRoles.end(), role) != official.eligibleRoles.end()) {
                result.push_back(&official);
                break;
            }
        }
    }

    return result;
}

} // namespace Officials