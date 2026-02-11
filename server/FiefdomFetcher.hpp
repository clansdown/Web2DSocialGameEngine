#pragma once

#include "FiefdomData.hpp"
#include <vector>
#include <string>
#include <optional>

namespace FiefdomFetcher {

std::optional<FiefdomData> fetchFiefdomById(
    int fiefdom_id,
    bool include_buildings = false,
    bool include_officials = false,
    bool include_heroes = false,
    bool include_combatants = false
);

std::vector<FiefdomData> fetchFiefdomsByOwnerId(int owner_id);

std::vector<BuildingData> fetchFiefdomBuildings(int fiefdom_id);

std::vector<WallData> fetchFiefdomWalls(int fiefdom_id);

std::vector<OfficialData> fetchFiefdomOfficials(int fiefdom_id);

std::optional<OfficialData> fetchOfficialById(int official_id);

bool createBuilding(int fiefdom_id, const std::string& name, int level,
                    int64_t construction_start_ts, int64_t action_start_ts,
                    const std::string& action_tag, int x, int y);

bool updateBuildingLevel(int building_id, int new_level, int64_t timestamp);

bool updateBuildingConstructionStart(int building_id, int64_t construction_start_ts, int64_t timestamp);

bool createWall(int fiefdom_id, int generation, int level, int hp, int64_t construction_start_ts);

bool updateWallLevel(int wall_id, int new_level, int new_hp, int64_t timestamp);

bool updateWallHP(int wall_id, int new_hp);

bool deleteWall(int wall_id);

bool createOfficial(int fiefdom_id, fiefdom::OfficialRole role, const std::string& template_id,
                    int portrait_id, const std::string& name, int level,
                    uint8_t intelligence, uint8_t charisma, uint8_t wisdom, uint8_t diligence);

struct FiefdomResources {
    int gold = 0;
    int grain = 0;
    int wood = 0;
    int steel = 0;
    int bronze = 0;
    int stone = 0;
    int leather = 0;
    int mana = 0;
};

bool updateFiefdomResources(int fiefdom_id, const FiefdomResources& resources);

bool updateFiefdomPeasants(int fiefdom_id, int peasants);

bool updateFiefdomWallCount(int fiefdom_id, int wall_count);

bool updateFiefdomMorale(int fiefdom_id, double morale);

std::vector<FiefdomHero> fetchFiefdomHeroes(int fiefdom_id);
std::vector<StationedCombatant> fetchStationedCombatants(int fiefdom_id);

bool createFiefdomHero(int fiefdom_id, const std::string& hero_config_id, int level);
bool createStationedCombatant(int fiefdom_id, const std::string& combatant_config_id, int level);

} // namespace FiefdomFetcher