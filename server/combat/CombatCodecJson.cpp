#include "CombatCodec.hpp"
#include <cstdio>

namespace combat {

namespace {

// Fast-path JSON string escaping — unit display names only.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string fmt_double(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", v);
    return std::string(buf);
}

// Fixed-shape snake_case unit row, hand-built for speed. This is the hot path
// serializer; keep it free of nlohmann object construction.
void append_unit_row(std::string& out, const combat_unit& u) {
    out += "{\"id\":";
    out += std::to_string(u.unit_id);
    out += ",\"owner\":";
    out += std::to_string(u.owner_player_id);
    out += ",\"team\":";
    out += std::to_string(u.team);
    out += ",\"name\":\"";
    out += json_escape(u.display_name);
    out += "\",\"unit_class\":\"";
    out += json_escape(u.unit_class);
    out += "\",\"level\":";
    out += std::to_string(u.level);
    out += ",\"x\":";
    out += fmt_double(u.x);
    out += ",\"y\":";
    out += fmt_double(u.y);
    out += ",\"facing\":";
    out += fmt_double(u.facing);
    out += ",\"hp\":";
    out += fmt_double(u.hp);
    out += ",\"max_hp\":";
    out += fmt_double(u.max_hp);
    out += ",\"status\":\"";
    out += json_escape(u.status);
    out += "\",\"is_enemy\":";
    out += u.is_enemy ? "true" : "false";
    // Static stats for the selection card (§12.4): attack type + arc defense /
    // dodge triples. These are class/level constants, so the cost is small and
    // the client avoids a second config round-trip.
    out += ",\"attack_type\":\"";
    out += json_escape(u.stats.attack_type);
    out += "\",\"def_front\":";
    out += fmt_double(u.stats.direction_defense_front);
    out += ",\"def_side\":";
    out += fmt_double(u.stats.direction_defense_side);
    out += ",\"def_back\":";
    out += fmt_double(u.stats.direction_defense_back);
    out += ",\"dodge_front\":";
    out += fmt_double(u.stats.dodge_front);
    out += ",\"dodge_side\":";
    out += fmt_double(u.stats.dodge_side);
    out += ",\"dodge_back\":";
    out += fmt_double(u.stats.dodge_back);
    // Body size (§5.6): normalized per-class radius. The client scales the drawn
    // sprite/ring/token by this, independent of the source image dimensions.
    out += ",\"radius\":";
    out += fmt_double(u.stats.radius);
    out += "}";
}

void append_structure_row(std::string& out, const combat_structure& s) {
    out += "{\"id\":";
    out += std::to_string(s.id);
    out += ",\"type\":\"";
    out += json_escape(s.type);
    out += "\",\"owner\":";
    out += std::to_string(s.owner_player_id);
    out += ",\"team\":";
    out += std::to_string(s.team);
    out += ",\"x\":";
    out += fmt_double(s.x);
    out += ",\"y\":";
    out += fmt_double(s.y);
    out += ",\"hp\":";
    out += fmt_double(s.hp);
    out += ",\"max_hp\":";
    out += fmt_double(s.max_hp);
    out += ",\"state\":\"";
    out += json_escape(s.state);
    out += "\",\"garrisoned_unit_id\":";
    out += std::to_string(s.garrisoned_unit_id);
    out += "}";
}

} // namespace

std::string json_codec::serialize_message(const combat_message& msg) const {
    if (msg.type == "match_state" || msg.type == "match_update") {
        // Hot path: hand-build the payload instead of using nlohmann::json.
        std::string out;
        out.reserve(256 + msg.units.size() * 160 + msg.structures.size() * 120);
        out += "{\"v\":1,\"fmt\":\"json\",\"type\":\"";
        out += msg.type;
        out += "\",\"match_id\":\"";
        out += json_escape(msg.match_id);
        out += "\",\"tick\":";
        out += std::to_string(msg.tick);
        out += ",\"payload\":{\"full\":";
        out += msg.is_full_state ? "true" : "false";
        out += ",\"phase\":\"";
        out += json_escape(msg.phase_name);
        out += "\"";
        if (msg.countdown >= 0.0) {
            out += ",\"countdown\":";
            out += fmt_double(msg.countdown);
        }
        if (msg.deploy_time >= 0.0) {
            out += ",\"deploy_time\":";
            out += fmt_double(msg.deploy_time);
        }
        out += ",\"battle_time\":";
        out += fmt_double(msg.battle_time);
        out += ",\"units\":[";
        bool first = true;
        for (const auto& u : msg.units) {
            if (!first) out += ",";
            first = false;
            append_unit_row(out, u);
        }
        out += "],\"structures\":[";
        first = true;
        for (const auto& s : msg.structures) {
            if (!first) out += ",";
            first = false;
            append_structure_row(out, s);
        }
        out += "],\"removed\":[";
        first = true;
        for (int64_t id : msg.removed_units) {
            if (!first) out += ",";
            first = false;
            out += std::to_string(id);
        }
        out += "],\"events\":[";
        first = true;
        for (const auto& e : msg.events) {
            if (!first) out += ",";
            first = false;
            out += "{\"type\":\"";
            out += json_escape(e.type);
            out += "\",\"payload\":";
            // Events are rare and non-uniform — nlohmann is fine here.
            out += e.payload.dump();
            out += "}";
        }
        out += "]}}";
        return out;
    }

    // General messages (welcome/chat/voice/error/result): nlohmann is fine.
    nlohmann::json envelope;
    envelope["v"] = 1;
    envelope["fmt"] = "json";
    envelope["type"] = msg.type;
    envelope["match_id"] = msg.match_id;
    envelope["tick"] = msg.tick;
    envelope["payload"] = msg.json_payload;
    return envelope.dump();
}

combat_message json_codec::deserialize_message(std::string_view wire) const {
    combat_message msg;
    nlohmann::json j = nlohmann::json::parse(wire, nullptr, true, true);
    msg.type = j.value("type", "");
    msg.match_id = j.value("match_id", "");
    msg.tick = j.value("tick", static_cast<int64_t>(-1));
    if (j.contains("payload") && j["payload"].is_object()) {
        msg.json_payload = j["payload"];
    }
    return msg;
}

} // namespace combat
