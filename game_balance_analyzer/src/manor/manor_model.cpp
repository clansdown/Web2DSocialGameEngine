#include "manor/manor_model.hpp"
#include "money.hpp"

#include <algorithm>

using json = nlohmann::json;

double amount_at_level(const json& amount, int level) {
    if (amount.is_number()) {
        return amount.get<double>();
    }
    if (amount.is_object()) {
        // Single-entry {amount: X} or a money object.
        if (amount.contains("amount")) {
            return amount_at_level(amount["amount"], level);
        }
        return money::money_object_to_gold(amount);
    }
    if (amount.is_array()) {
        if (amount.empty()) {
            return 0.0;
        }
        int idx = level - 1;
        if (idx < static_cast<int>(amount.size())) {
            return amount_at_level(amount[static_cast<size_t>(idx)], level);
        }
        // Linear extrapolation beyond the array.
        size_t last = amount.size() - 1;
        double last_val = amount_at_level(amount[last], level);
        if (last == 0) {
            return last_val;
        }
        double prev_val = amount_at_level(amount[last - 1], level - 1);
        return last_val + (idx - static_cast<int>(last)) * (last_val - prev_val);
    }
    return 0.0;
}

building_type::building_type(const std::string& id, const json& config)
    : id_(id), raw_(config) {
    display_name_ = config.value("display_name", id);
    max_level_ = config.value("max_level", 1);
    if (config.contains("built_from")) {
        built_from_ = config["built_from"].get<std::string>();
    }
    is_water_powered_ = config.value("water_powered", false);
    if (config.contains("max_per_fiefdom")) {
        max_per_fiefdom_ = config["max_per_fiefdom"].get<int>();
    }
    arable_acres_ = config.value("arable_acres", 0);
    forest_acres_ = config.value("forest_acres", 0);
    class_ = config.value("class", "");
}

double building_type::cost(const std::string& resource) const {
    const std::string key = resource + "_cost";
    if (!raw_.contains(key)) {
        return 0.0;
    }
    const auto& c = raw_[key];
    if (c.is_number()) {
        return c.get<double>();
    }
    if (c.is_array() && !c.empty()) {
        return amount_at_level(c, 1);
    }
    if (c.is_object()) {
        return money::money_object_to_gold(c);
    }
    return 0.0;
}

double building_type::cost_at(const std::string& resource, int level) const {
    const std::string key = resource + "_cost";
    if (!raw_.contains(key)) {
        return 0.0;
    }
    const auto& c = raw_[key];
    if (c.is_number()) {
        return c.get<double>();
    }
    if (c.is_array()) {
        if (c.empty()) return 0.0;
        if (level < 1) level = 1;
        size_t idx = static_cast<size_t>(level - 1);
        if (idx >= c.size()) idx = c.size() - 1;
        if (c[idx].is_number()) return c[idx].get<double>();
        return 0.0;
    }
    if (c.is_object()) {
        return money::money_object_to_gold(c);
    }
    return 0.0;
}

double building_type::gold_normalized_cost(
    const std::unordered_map<std::string, double>& import_prices_gold) const {
    static const char* resources[] = {
        "gold", "silver_pence", "wood", "steel", "bronze", "grain", "leather",
        "mana", "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards",
    };
    double total = 0.0;
    for (const char* res : resources) {
        double amount = cost(res);
        if (amount == 0.0) {
            continue;
        }
        if (std::string(res) == "gold") {
            total += amount;
            continue;
        }
        if (std::string(res) == "silver_pence") {
            // Silver pence is the currency itself: 240 pence = 1 gold.
            total += amount / money::pence_per_gold;
            continue;
        }
        // Penny-market resources pay in silver_pence; convert at the gold rate
        // implied by 240 pence/gold.
        auto it = import_prices_gold.find(res);
        double price_gold = (it != import_prices_gold.end()) ? it->second : 0.0;
        total += amount * price_gold;
    }
    return total;
}

double building_type::production(const std::string& resource, int level) const {
    if (raw_.contains(resource)) {
        const auto& v = raw_[resource];
        if (v.is_object() && v.contains("amount")) {
            return amount_at_level(v["amount"], level);
        }
        return amount_at_level(v, level);
    }
    // Check the "outputs" array.
    if (raw_.contains("outputs") && raw_["outputs"].is_array()) {
        for (const auto& out : raw_["outputs"]) {
            if (out.value("resource", "") == resource) {
                int min_level = out.value("min_level", 1);
                if (level < min_level) {
                    return 0.0;
                }
                return amount_at_level(out["amount"], level);
            }
        }
    }
    return 0.0;
}

std::vector<std::pair<std::string, double>> building_type::outputs_at(int level) const {
    std::vector<std::pair<std::string, double>> result;
    static const char* resources[] = {
        "gold", "wood", "steel", "bronze", "grain", "leather", "mana",
        "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards",
    };
    for (const char* res : resources) {
        double p = production(res, level);
        if (p != 0.0) {
            result.emplace_back(res, p);
        }
    }
    return result;
}

std::unordered_map<std::string, double> building_type::daily_cost() const {
    std::unordered_map<std::string, double> result;
    if (!raw_.contains("daily_cost") || !raw_["daily_cost"].is_object()) {
        return result;
    }
    for (auto it = raw_["daily_cost"].begin(); it != raw_["daily_cost"].end(); ++it) {
        result[it.key()] = it.value().get<double>();
    }
    return result;
}

std::unordered_map<std::string, double> building_type::inputs_at(int level) const {
    // The `outputs` array is the canonical production schema: each output
    // declares its own inputs, and a building-level `inputs` map is not used.
    // Inputs are consumed once per building per day (per output).
    std::unordered_map<std::string, double> result;
    if (raw_.contains("outputs") && raw_["outputs"].is_array()) {
        for (const auto& out : raw_["outputs"]) {
            int min_level = out.value("min_level", 1);
            if (level < min_level) {
                continue;  // output (and its inputs) not yet unlocked
            }
            if (out.contains("inputs") && out["inputs"].is_object()) {
                for (auto it = out["inputs"].begin(); it != out["inputs"].end(); ++it) {
                    result[it.key()] += amount_at_level(it.value(), level);
                }
            }
        }
    }
    return result;
}

double building_type::construction_time(int level) const {
    if (!raw_.contains("construction_times") || !raw_["construction_times"].is_array() ||
        raw_["construction_times"].empty()) {
        return 0.0;
    }
    return amount_at_level(raw_["construction_times"], level);
}

int building_type::manor_level_requirement() const {
    if (!raw_.contains("prerequisites") || !raw_["prerequisites"].is_array()) {
        return 1;
    }
    for (const auto& prereq : raw_["prerequisites"]) {
        if (prereq.is_object() && prereq.contains("manor_level")) {
            return prereq["manor_level"].get<int>();
        }
    }
    return 1;
}

void building_registry::load(const json& types_array) {
    buildings_.clear();
    index_.clear();
    if (!types_array.is_array()) {
        return;
    }
    for (const auto& entry : types_array) {
        if (!entry.is_object() || entry.empty()) {
            continue;
        }
        const auto& key = entry.begin().key();
        buildings_.emplace_back(key, entry.begin().value());
    }
    for (size_t i = 0; i < buildings_.size(); ++i) {
        index_[buildings_[i].id()] = i;
    }
    // Resolve successors (reverse built_from) in a second pass.
    std::unordered_map<std::string, std::string> next;
    for (auto& b : buildings_) {
        if (b.built_from()) {
            next[*b.built_from()] = b.id();
        }
    }
    for (auto& b : buildings_) {
        auto it = next.find(b.id());
        if (it != next.end()) {
            b.successor_ = it->second;
        }
    }
}

const building_type* building_registry::find(const std::string& id) const {
    auto it = index_.find(id);
    if (it == index_.end()) {
        return nullptr;
    }
    return &buildings_[it->second];
}

std::vector<std::string> building_registry::chain_for(const std::string& id) const {
    std::vector<std::string> chain;
    std::string cur = id;
    while (true) {
        const building_type* b = find(cur);
        if (!b) {
            break;
        }
        chain.insert(chain.begin(), cur);
        if (!b->built_from()) {
            break;
        }
        cur = *b->built_from();
    }
    return chain;
}

bool building_registry::satisfies(const std::string& higher, const std::string& lower) const {
    if (higher == lower) {
        return true;
    }
    auto chain = chain_for(higher);
    for (const auto& stage : chain) {
        if (stage == lower) {
            return true;
        }
    }
    // Class matching: a building satisfies a requirement on its `class`
    // (e.g. flour_milling targets the "peasant" class, covering
    // villein/freeholder/yeoman). Checked per chain stage in case a stage's
    // class differs from the base.
    for (const auto& stage : chain) {
        const building_type* b = find(stage);
        if (b && b->is_class(lower)) {
            return true;
        }
    }
    return false;
}
