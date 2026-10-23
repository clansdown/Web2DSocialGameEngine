#include "MissionGenerator.hpp"
#include "RetinueStrength.hpp"
#include "GameConfigCache.hpp"
#include "combat/CombatantStats.hpp"
#include "combat/CombatMapCache.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace mission_gen {

namespace {

uint64_t hash_combine(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

const nlohmann::json& obj_or_empty(const nlohmann::json& parent, const char* key,
                                   nlohmann::json& scratch) {
    if (parent.is_object() && parent.contains(key) && parent[key].is_object()) {
        return parent[key];
    }
    scratch = nlohmann::json::object();
    return scratch;
}

std::string pick_map(offering_rng& rng, GameConfigCache& cache) {
    std::vector<std::string> maps;
    const nlohmann::json& cfg = cache.getCombatOfferings();
    if (cfg.contains("map_pool") && cfg["map_pool"].is_array()) {
        for (const auto& m : cfg["map_pool"]) {
            if (m.is_string() && !m.get<std::string>().empty()) {
                maps.push_back(m.get<std::string>());
            }
        }
    }
    if (maps.empty()) {
        for (const std::string& filename : CombatMapCache::get_instance().list_maps()) {
            maps.push_back(filename.size() > 5 ? filename.substr(0, filename.size() - 5) : filename);
        }
    }
    if (maps.empty()) return "meadow";
    return maps[rng.range(0, static_cast<int>(maps.size()) - 1)];
}

// Per-unit power of every monster class at every authored level. Index 0 is
// unused; index L is level L. The same table supplies the strategy weighting
// (level 1), the overshoot allowance (min over the table) and every level-fit
// check, so the generator never re-resolves a stat mid-assembly.
struct class_power_table {
    std::vector<std::string> classes;
    std::unordered_map<std::string, std::vector<double>> by_class;
};

struct assembly_context {
    GameConfigCache* cache = nullptr;
    const nlohmann::json* enemy_combatants = nullptr;
    retinue_strength::strength_tuning tuning;
    const class_power_table* powers = nullptr;
    double target_power = 0.0;       // T
    double min_monster_power = 0.0;  // p_min
    int max_units = 60;
    int max_depth = 8;
};

double power_of(const assembly_context& ctx, const std::string& unit_class, int level) {
    const auto it = ctx.powers->by_class.find(unit_class);
    if (it == ctx.powers->by_class.end()) return 0.0;
    const std::vector<double>& levels = it->second;
    if (level <= 0 || level >= static_cast<int>(levels.size())) return 0.0;
    return levels[static_cast<size_t>(level)];
}

// rho in [difficulty.band_min, difficulty.band_max].
double draw_difficulty(offering_rng& rng, const nlohmann::json& cfg) {
    nlohmann::json scratch;
    const nlohmann::json& difficulty = obj_or_empty(cfg, "difficulty", scratch);
    double lo = difficulty.value("band_min", 0.7);
    double hi = difficulty.value("band_max", 1.15);
    if (hi < lo) std::swap(lo, hi);
    return lo + (hi - lo) * rng.unit();
}

// e = seed draw + a*(players/players_ref - 1) - b*log2(troops/troops_ref),
// clamped to [min, max]. More players push toward elite (e > 0); more retinue
// units push toward horde (e < 0); normalizing the troop term in log2 keeps raw
// unit counts from swamping the coefficients.
double compute_eliteness(offering_rng& rng, const warband_spec& spec,
                         const nlohmann::json& cfg) {
    nlohmann::json enc_scratch;
    const nlohmann::json& enc = obj_or_empty(cfg, "encounter", enc_scratch);
    nlohmann::json el_scratch;
    const nlohmann::json& el = obj_or_empty(enc, "eliteness", el_scratch);

    double seed_min = el.value("seed_min", -1.0);
    double seed_max = el.value("seed_max", 1.0);
    if (seed_max < seed_min) std::swap(seed_min, seed_max);
    double e = seed_min + (seed_max - seed_min) * rng.unit();

    const double players_ref = std::max(1e-6, el.value("players_ref", 1.0));
    const double troops_ref = std::max(1e-6, el.value("troops_ref", 8.0));
    const double players_coeff = el.value("players_coeff", 0.25);
    const double troops_coeff = el.value("troops_coeff", 0.5);

    e += players_coeff *
         (static_cast<double>(std::max(1, spec.expected_players)) / players_ref - 1.0);
    e -= troops_coeff *
         std::log2(static_cast<double>(std::max(1, spec.party_troop_count)) / troops_ref);

    double lo = el.value("min", -2.0);
    double hi = el.value("max", 2.0);
    if (hi < lo) std::swap(lo, hi);
    return std::clamp(e, lo, hi);
}

// w(c) = (base_power / p_ref)^e. e < 0 favours cheap classes (horde), e > 0
// favours expensive ones (elite).
double class_weight(double base_power, double p_ref, double eliteness) {
    if (base_power <= 0.0 || p_ref <= 0.0) return 0.0;
    return std::pow(base_power / p_ref, eliteness);
}

std::string weighted_pick(const std::vector<std::string>& candidates,
                          const assembly_context& ctx, double p_ref, double eliteness,
                          offering_rng& rng) {
    double total = 0.0;
    for (const std::string& cls : candidates) {
        total += class_weight(power_of(ctx, cls, 1), p_ref, eliteness);
    }
    if (total <= 0.0) {
        return candidates[static_cast<size_t>(rng.range(0, static_cast<int>(candidates.size()) - 1))];
    }
    double r = rng.unit() * total;
    for (const std::string& cls : candidates) {
        r -= class_weight(power_of(ctx, cls, 1), p_ref, eliteness);
        if (r <= 0.0) return cls;
    }
    return candidates.back();
}

// Highest level whose addition keeps the warband strictly under `ceiling`. Scans
// every level (power need not be monotone); returns 0 when nothing fits.
int max_fitting_level(double sum_power, int unit_count, const std::string& unit_class,
                      double ceiling, const assembly_context& ctx) {
    const auto it = ctx.powers->by_class.find(unit_class);
    if (it == ctx.powers->by_class.end()) return 0;
    const int max_level = static_cast<int>(it->second.size()) - 1;
    int best = 0;
    for (int level = 1; level <= max_level; ++level) {
        const double unit_power = power_of(ctx, unit_class, level);
        if (unit_power <= 0.0) continue;   // a stat-less class can never be priced
        const double trial_sum = sum_power + unit_power;
        const double trial_strength =
            retinue_strength::army_strength(trial_sum, unit_count + 1, ctx.tuning);
        if (trial_strength < ceiling) best = level;
    }
    return best;
}

// A generated group's tactic follows its class style: ranged skirmishes, magic
// holds, melee charges (mirrors combat/profiles.json).
std::string tactic_for_class(const nlohmann::json& enemy, const std::string& unit_class) {
    const combat::combat_unit_stats stats =
        combat::resolve_combatant_stats(enemy, unit_class, 1);
    if (stats.attack_type == "ranged") return "skirmish";
    if (stats.attack_type == "magical") return "hold";
    return "charge";
}

// Dependency-ordered closure for `count` units of `unit_class`: unmet
// prerequisites first (recursively), then the requested class. Requirements are
// `[{ "unit_class": ..., "count": ... }]` on the enemy combatant config.
//
// The class is counted as planned before its own requirements are checked, so a
// self-referential requirement is satisfied by the unit itself and mutual cycles
// terminate (the linter still warns about them).
void resolve_requirement_closure(const std::string& unit_class, int count,
                                 const nlohmann::json& enemy,
                                 const std::map<std::string, int>& have,
                                 std::map<std::string, int>& planned,
                                 std::vector<std::string>& out, int depth, int max_depth) {
    if (count <= 0 || depth > max_depth) return;
    planned[unit_class] += count;

    const auto it = enemy.find(unit_class);
    if (it != enemy.end() && it->is_object() && it->contains("requires")) {
        const nlohmann::json& reqs = (*it)["requires"];
        if (reqs.is_array()) {
            for (const auto& req : reqs) {
                if (!req.is_object()) continue;
                const std::string required_class = req.value("unit_class", "");
                const int required_count = req.value("count", 0);
                if (required_class.empty() || required_count <= 0) continue;

                int satisfied = 0;
                const auto have_it = have.find(required_class);
                if (have_it != have.end()) satisfied += have_it->second;
                const auto planned_it = planned.find(required_class);
                if (planned_it != planned.end()) satisfied += planned_it->second;

                const int need = required_count - satisfied;
                if (need > 0) {
                    resolve_requirement_closure(required_class, need, enemy, have, planned, out,
                                                depth + 1, max_depth);
                }
            }
        }
    }

    for (int i = 0; i < count; ++i) out.push_back(unit_class);
}

// One roll: resolve the dependency closure for `unit_class`, then add units in
// dependency order at a level drawn uniformly in [1, L_fit]. The FIRST unit that
// cannot fit ends the round (no skipping), so an unaffordable boss still leaves
// whatever escort could be paid for.
int roll_class(const std::string& unit_class,
               std::vector<combat::warband_spawn_entry>& warband,
               const assembly_context& ctx, offering_rng& rng) {
    std::map<std::string, int> have;
    double sum_power = 0.0;
    int unit_count = 0;
    for (const combat::warband_spawn_entry& entry : warband) {
        if (entry.count <= 0) continue;
        have[entry.unit_class] += entry.count;
        sum_power += power_of(ctx, entry.unit_class, entry.level) *
                     static_cast<double>(entry.count);
        unit_count += entry.count;
    }

    std::map<std::string, int> planned;
    std::vector<std::string> closure;
    resolve_requirement_closure(unit_class, 1, *ctx.enemy_combatants, have, planned, closure, 0,
                                ctx.max_depth);

    const double ceiling = ctx.target_power + ctx.min_monster_power;
    int added = 0;
    for (const std::string& cls : closure) {
        if (unit_count >= ctx.max_units) break;
        const int level_fit = max_fitting_level(sum_power, unit_count, cls, ceiling, ctx);
        if (level_fit < 1) break;

        const int level = rng.range(1, level_fit);
        combat::warband_spawn_entry entry;
        entry.unit_class = cls;
        entry.level = level;
        entry.count = 1;
        entry.group = cls;
        entry.tactic = tactic_for_class(*ctx.enemy_combatants, cls);
        entry.aggro_model = "call_to_arms";
        warband.push_back(std::move(entry));

        sum_power += power_of(ctx, cls, level);
        ++unit_count;
        ++added;
    }
    return added;
}

void merge_warband_entries(std::vector<combat::warband_spawn_entry>& entries) {
    std::vector<combat::warband_spawn_entry> merged;
    merged.reserve(entries.size());
    for (const combat::warband_spawn_entry& entry : entries) {
        bool found = false;
        for (combat::warband_spawn_entry& existing : merged) {
            if (existing.unit_class == entry.unit_class && existing.level == entry.level &&
                existing.group == entry.group && existing.tactic == entry.tactic &&
                existing.aggro_model == entry.aggro_model) {
                existing.count += entry.count;
                found = true;
                break;
            }
        }
        if (!found) merged.push_back(entry);
    }
    entries = std::move(merged);
}

} // namespace

uint64_t offering_rng::next() {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double offering_rng::unit() {
    return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
}

int offering_rng::range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1));
}

offering_rng make_offering_rng(const offering_id_parts& parts) {
    uint64_t h = 0xCBF29CE484222325ULL;
    h = hash_combine(h, static_cast<uint64_t>(parts.player_id));
    h = hash_combine(h, static_cast<uint64_t>(parts.hour_bucket));
    h = hash_combine(h, static_cast<uint64_t>(parts.index));
    h = hash_combine(h, static_cast<uint64_t>(parts.players));
    h = hash_combine(h, static_cast<uint64_t>(parts.troop_count));
    h = hash_combine(h, static_cast<uint64_t>(parts.power_bucket));
    for (char c : parts.tier) h = hash_combine(h, static_cast<uint64_t>(static_cast<unsigned char>(c)));
    offering_rng rng;
    rng.state = h;
    return rng;
}

uint64_t make_warband_seed(const offering_id_parts& parts) {
    // A distinct salt keeps the warband stream independent of the map stream.
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    h = hash_combine(h, static_cast<uint64_t>(parts.player_id));
    h = hash_combine(h, static_cast<uint64_t>(parts.hour_bucket));
    h = hash_combine(h, static_cast<uint64_t>(parts.index));
    h = hash_combine(h, static_cast<uint64_t>(parts.players));
    h = hash_combine(h, static_cast<uint64_t>(parts.troop_count));
    h = hash_combine(h, static_cast<uint64_t>(parts.power_bucket));
    for (char c : parts.tier) h = hash_combine(h, static_cast<uint64_t>(static_cast<unsigned char>(c)));
    return h;
}

std::string encode_offering_id(const offering_id_parts& parts) {
    std::ostringstream os;
    os << parts.player_id << '.' << parts.hour_bucket << '.' << parts.tier << '.'
       << parts.index << '.' << parts.players << '.' << parts.troop_count << '.'
       << parts.power_bucket;
    return os.str();
}

bool decode_offering_id(const std::string& id, offering_id_parts& out) {
    // <player_id>.<hour_bucket>.<tier>.<index>.<players>.<troop_count>.<power_bucket>
    std::vector<std::string> fields;
    std::string current;
    for (char c : id) {
        if (c == '.') {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(current);
    if (fields.size() != 7) return false;
    try {
        out.player_id = std::stoll(fields[0]);
        out.hour_bucket = std::stoll(fields[1]);
        out.tier = fields[2];
        out.index = std::stoi(fields[3]);
        out.players = std::stoi(fields[4]);
        out.troop_count = std::stoi(fields[5]);
        out.power_bucket = static_cast<uint32_t>(std::stoul(fields[6]));
    } catch (const std::exception&) {
        return false;
    }
    return !out.tier.empty() && out.index >= 0 && out.players >= 1 && out.troop_count >= 0;
}

int64_t current_hour_bucket(int64_t now_seconds) {
    return now_seconds / 3600;
}

uint32_t quantize_power(double power, const nlohmann::json& offerings_config) {
    nlohmann::json scratch;
    const nlohmann::json& ps = obj_or_empty(offerings_config, "power_scale", scratch);
    const double base = std::max(1e-6, ps.value("base", 1.0));
    const std::string mode = ps.value("mode", "geometric");
    const double p = std::max(base, power);
    if (mode == "linear") {
        const double step = std::max(1e-6, ps.value("ratio", 1.0));
        return static_cast<uint32_t>(std::max(0.0, (p - base) / step));
    }
    const double ratio = std::max(1.0 + 1e-9, ps.value("ratio", 1.01));
    const double b = std::log(p / base) / std::log(ratio);
    return static_cast<uint32_t>(std::max(0.0, std::round(b)));
}

double dequantize_power(uint32_t bucket, const nlohmann::json& offerings_config) {
    nlohmann::json scratch;
    const nlohmann::json& ps = obj_or_empty(offerings_config, "power_scale", scratch);
    const double base = std::max(1e-6, ps.value("base", 1.0));
    const std::string mode = ps.value("mode", "geometric");
    if (mode == "linear") {
        const double step = std::max(1e-6, ps.value("ratio", 1.0));
        return base + step * static_cast<double>(bucket);
    }
    const double ratio = std::max(1.0 + 1e-9, ps.value("ratio", 1.01));
    return base * std::pow(ratio, static_cast<double>(bucket));
}

std::vector<mission_tier> parse_tiers(const nlohmann::json& offerings_config) {
    std::vector<mission_tier> tiers;
    if (!offerings_config.contains("tiers") || !offerings_config["tiers"].is_array()) return tiers;
    for (const auto& t : offerings_config["tiers"]) {
        if (!t.is_object()) continue;
        mission_tier mt;
        mt.id = t.value("id", "");
        mt.min_players = t.value("min_players", 1);
        mt.max_players = t.value("max_players", 1);
        mt.requires_barony = t.value("requires_barony", false);
        if (!mt.id.empty()) tiers.push_back(std::move(mt));
    }
    return tiers;
}

party_army party_army_for_tier(const mission_tier& tier, const retinue_army_summary& self_army,
                               const std::vector<retinue_army_summary>& barony_armies,
                               GameConfigCache& cache) {
    const retinue_strength::strength_tuning tuning =
        retinue_strength::load_strength_tuning(cache.getCombatOfferings());

    std::vector<retinue_army_summary> sorted = barony_armies;
    std::sort(sorted.begin(), sorted.end(),
              [](const retinue_army_summary& a, const retinue_army_summary& b) {
                  return a.sum_power > b.sum_power;
              });

    const int max_others = std::max(0, tier.max_players - 1);
    const int included = std::min(max_others, static_cast<int>(sorted.size()));

    party_army army;
    army.expected_players = 1 + included;
    double sum_power = self_army.sum_power;
    int troop_count = self_army.unit_count;
    for (int i = 0; i < included; ++i) {
        sum_power += sorted[static_cast<size_t>(i)].sum_power;
        troop_count += sorted[static_cast<size_t>(i)].unit_count;
    }
    army.troop_count = troop_count;
    army.power = retinue_strength::army_strength(sum_power, troop_count, tuning);
    return army;
}

assembled_warband assemble_warband(const warband_spec& spec, GameConfigCache& cache) {
    assembled_warband out;
    const nlohmann::json& cfg = cache.getCombatOfferings();
    const nlohmann::json& enemy = cache.getEnemyCombatants();
    const retinue_strength::strength_tuning tuning =
        retinue_strength::load_strength_tuning(cfg);

    class_power_table table;
    if (enemy.is_object()) {
        for (auto it = enemy.begin(); it != enemy.end(); ++it) {
            if (it.value().is_object()) table.classes.push_back(it.key());
        }
    }
    if (table.classes.empty()) return out;

    double p_min = std::numeric_limits<double>::max();
    for (const std::string& cls : table.classes) {
        const int max_level = std::max(1, combat::combatant_max_level(enemy, cls));
        std::vector<double> levels(static_cast<size_t>(max_level) + 1, 0.0);
        for (int level = 1; level <= max_level; ++level) {
            const double p = retinue_strength::combatant_power(enemy, cls, level, tuning);
            levels[static_cast<size_t>(level)] = p;
            if (p > 0.0) p_min = std::min(p_min, p);
        }
        table.by_class[cls] = std::move(levels);
    }
    if (p_min == std::numeric_limits<double>::max()) p_min = 0.0;

    assembly_context ctx;
    ctx.cache = &cache;
    ctx.enemy_combatants = &enemy;
    ctx.tuning = tuning;
    ctx.powers = &table;
    ctx.min_monster_power = p_min;

    nlohmann::json enc_scratch;
    const nlohmann::json& enc = obj_or_empty(cfg, "encounter", enc_scratch);
    ctx.max_units = std::max(1, enc.value("max_units", 60));
    ctx.max_depth = std::max(0, enc.value("prerequisite_depth", 8));

    // Reference power for the weighting: geometric mean of level-1 class powers.
    double log_sum = 0.0;
    int weighted_classes = 0;
    for (const std::string& cls : table.classes) {
        const double p = power_of(ctx, cls, 1);
        if (p > 0.0) {
            log_sum += std::log(p);
            ++weighted_classes;
        }
    }
    const double p_ref =
        weighted_classes > 0 ? std::exp(log_sum / static_cast<double>(weighted_classes)) : 1.0;

    offering_rng rng;
    rng.state = spec.seed;

    out.difficulty = draw_difficulty(rng, cfg);
    out.target_power = out.difficulty * std::max(0.0, spec.party_power);
    out.eliteness = compute_eliteness(rng, spec, cfg);
    ctx.target_power = out.target_power;

    std::vector<combat::warband_spawn_entry> warband;
    std::set<std::string> attempted;   // classes tried since the last success

    // Defensive bounded loop. The dry pass (every class attempted, none added)
    // is the normal exit; the bound is only a backstop.
    for (int i = 0; i < 10000; ++i) {
        if (static_cast<int>(warband.size()) >= ctx.max_units) break;
        if (attempted.size() >= table.classes.size()) break;

        std::vector<std::string> candidates;
        candidates.reserve(table.classes.size());
        for (const std::string& cls : table.classes) {
            if (attempted.find(cls) == attempted.end()) candidates.push_back(cls);
        }
        if (candidates.empty()) break;

        const std::string chosen = weighted_pick(candidates, ctx, p_ref, out.eliteness, rng);
        const int added = roll_class(chosen, warband, ctx, rng);
        if (added > 0) {
            attempted.clear();
        } else {
            attempted.insert(chosen);
        }
    }

    merge_warband_entries(warband);
    out.entries = std::move(warband);
    out.realized_power = retinue_strength::warband_power(out.entries, cache, tuning);
    return out;
}

std::string danger_band_for(double ratio, const nlohmann::json& offerings_config) {
    if (offerings_config.contains("danger_bands") && offerings_config["danger_bands"].is_array()) {
        for (const auto& b : offerings_config["danger_bands"]) {
            if (!b.is_object()) continue;
            if (!b.contains("max_ratio") || b["max_ratio"].is_null()) {
                return b.value("id", "deadly");
            }
            if (b["max_ratio"].is_number() && ratio <= b["max_ratio"].get<double>()) {
                return b.value("id", "");
            }
        }
    }
    return "fair";
}

double xp_for_power(double warband_power, const nlohmann::json& offerings_config) {
    nlohmann::json scratch;
    const nlohmann::json& xp = obj_or_empty(offerings_config, "xp", scratch);
    const double per_power = xp.value("per_power", 0.4);
    const double min_xp = xp.value("min", 10.0);
    return std::max(min_xp, per_power * warband_power);
}

mission_offering generate_offering(const offering_id_parts& parts, GameConfigCache& cache) {
    const nlohmann::json& cfg = cache.getCombatOfferings();

    mission_offering off;
    off.id = encode_offering_id(parts);
    off.tier = parts.tier;
    off.index = parts.index;
    off.hour_bucket = parts.hour_bucket;
    off.players = parts.players;
    off.troop_count = parts.troop_count;
    off.power_bucket = parts.power_bucket;

    warband_spec spec;
    spec.seed = make_warband_seed(parts);
    spec.expected_players = parts.players;
    spec.party_troop_count = parts.troop_count;
    spec.party_power = dequantize_power(parts.power_bucket, cfg);

    const assembled_warband assembled = assemble_warband(spec, cache);
    off.warband = assembled.entries;
    off.difficulty = assembled.difficulty;
    off.target_power = assembled.target_power;
    off.eliteness = assembled.eliteness;
    off.realized_power = assembled.realized_power;
    off.encounter_power = assembled.target_power;
    off.danger_ratio =
        spec.party_power > 0.0 ? assembled.realized_power / spec.party_power : 1.0;
    off.danger_band = danger_band_for(off.danger_ratio, cfg);
    off.ruleset = cfg.value("ruleset", "skirmish");
    off.total_units = 0;
    for (const auto& e : off.warband) off.total_units += e.count;
    off.xp_base = xp_for_power(assembled.realized_power, cfg);

    offering_rng map_rng = make_offering_rng(parts);
    off.map_id = pick_map(map_rng, cache);
    return off;
}

std::vector<mission_offering> generate_board(
    int64_t character_id, int64_t hour_bucket, const retinue_army_summary& self_army,
    const std::vector<retinue_army_summary>& barony_armies, bool in_barony,
    GameConfigCache& cache) {
    std::vector<mission_offering> board;
    const nlohmann::json& cfg = cache.getCombatOfferings();
    const int per_tier =
        cfg.is_object()
            ? std::max(1, cfg.value("board", nlohmann::json::object()).value("per_tier", 8))
            : 8;

    for (const mission_tier& tier : parse_tiers(cfg)) {
        if (tier.requires_barony && !in_barony) continue;

        const party_army army = party_army_for_tier(tier, self_army, barony_armies, cache);
        const uint32_t power_bucket = quantize_power(army.power, cfg);
        for (int index = 0; index < per_tier; ++index) {
            offering_id_parts parts;
            parts.player_id = character_id;
            parts.hour_bucket = hour_bucket;
            parts.tier = tier.id;
            parts.index = index;
            parts.players = army.expected_players;
            parts.troop_count = army.troop_count;
            parts.power_bucket = power_bucket;
            board.push_back(generate_offering(parts, cache));
        }
    }
    return board;
}

nlohmann::json mission_offering::to_json() const {
    nlohmann::json composition = nlohmann::json::array();
    for (const auto& e : warband) {
        composition.push_back({{"unit_class", e.unit_class}, {"level", e.level},
                               {"count", e.count}, {"group", e.group},
                               {"tactic", e.tactic}, {"aggro_model", e.aggro_model}});
    }
    return nlohmann::json{
        {"id", id},
        {"tier", tier},
        {"index", index},
        {"ruleset", ruleset},
        {"map_id", map_id},
        {"xp_base", xp_base},
        {"danger_band", danger_band},
        {"danger_ratio", danger_ratio},
        {"encounter_power", encounter_power},
        {"target_power", target_power},
        {"realized_power", realized_power},
        {"difficulty", difficulty},
        {"eliteness", eliteness},
        {"warband", composition},
        {"total_units", total_units}
    };
}

} // namespace mission_gen
