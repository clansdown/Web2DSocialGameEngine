#ifndef FIEFDOM_OFFICIALS_HPP
#define FIEFDOM_OFFICIALS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace Officials {

struct StatArray {
    std::vector<int> values;
    int max = 0;

    int getValue(int level) const;
};

struct OfficialTemplate {
    std::string id;
    std::string name;
    int max_level = 1;
    std::vector<std::string> eligibleRoles;

    StatArray intelligence;
    StatArray charisma;
    StatArray wisdom;
    StatArray diligence;

    int portrait_id = 0;
    std::string description;

    int getIntelligence(int level) const;
    int getCharisma(int level) const;
    int getWisdom(int level) const;
    int getDiligence(int level) const;
};

class OfficialRegistry {
public:
    static OfficialRegistry& getInstance();

    bool loadOfficials(const std::string& config_path);

    std::optional<const OfficialTemplate*> getOfficial(const std::string& id) const;
    const std::unordered_map<std::string, OfficialTemplate>& getAllOfficials() const;

    std::vector<const OfficialTemplate*> getEligibleOfficialsForRole(const std::string& role) const;
    std::vector<const OfficialTemplate*> getEligibleOfficialsForRoles(const std::vector<std::string>& roles) const;

private:
    std::unordered_map<std::string, OfficialTemplate> officials_;
};

} // namespace Officials

#endif // FIEFDOM_OFFICIALS_HPP