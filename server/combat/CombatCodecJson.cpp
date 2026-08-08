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
    out += ",\"hp\":";
    out += fmt_double(u.hp);
    out += ",\"max_hp\":";
    out += fmt_double(u.max_hp);
    out += ",\"status\":\"";
    out += json_escape(u.status);
    out += "\"}";
}

} // namespace

std::string json_codec::serialize_message(const combat_message& msg) const {
    if (msg.type == "match_state" || msg.type == "match_update") {
        // Hot path: hand-build the payload instead of using nlohmann::json.
        std::string out;
        out.reserve(256 + msg.units.size() * 160);
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
        out += ",\"battle_time\":";
        out += fmt_double(msg.battle_time);
        out += ",\"units\":[";
        bool first = true;
        for (const auto& u : msg.units) {
            if (!first) out += ",";
            first = false;
            append_unit_row(out, u);
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
