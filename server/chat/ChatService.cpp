#include "chat/ChatService.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <vector>
#include <optional>
#include <limits>

#include "AvatarUrl.hpp"
#include "Database.hpp"
#include "GameConfigCache.hpp"
#include "Visibility.hpp"
#include "chat/ChatDB.hpp"
#include "chat/MadladTranslationEngine.hpp"
#include "chat/Presence.hpp"
#include "chat/PushSender.hpp"
#include "chat/StubLlmTranslationEngine.hpp"

namespace chat {

namespace {

// ── Small utilities ─────────────────────────────────────────────────────────

size_t utf8_length(const std::string& s) {
    size_t count = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) count++;
    }
    return count;
}

int line_count(const std::string& s) {
    int lines = 1;
    for (char c : s) {
        if (c == '\n') lines++;
    }
    return lines;
}

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

json chat_cfg(GameConfigCache* cache) {
    if (!cache) return json::object();
    return cache->getChatConfig();
}

int cfg_int(const json& j, const char* key, int fallback) {
    if (j.is_object() && j.contains(key) && j[key].is_number_integer()) {
        return j[key].get<int>();
    }
    return fallback;
}

// Resolves character display names / portrait URLs from game.db on demand.
// Chat.db deliberately stores no names (two independent databases).
struct CharacterDirectory {
    // The viewer's adult flag drives every safe/custom choice: minors get safe
    // names and safe portraits for all players.
    explicit CharacterDirectory(bool viewer_is_adult, int64_t viewer_character_id)
        : viewer_adult(viewer_is_adult), viewer_character(viewer_character_id) {}

    bool viewer_adult = false;
    int64_t viewer_character = 0;
    std::map<int64_t, json> cache;

    const json& get(int64_t id) {
        auto it = cache.find(id);
        if (it != cache.end()) return it->second;
        json entry = {{"display_name", ""}, {"portrait_url", ""}};
        if (id > 0) {
            auto identity = visibility::load_character_identity(id);
            if (identity) {
                visibility::viewer_context viewer;
                viewer.character_id = viewer_character;
                viewer.is_adult = viewer_adult;
                json projected = visibility::project_character_identity(viewer, *identity);
                entry["display_name"] = projected.value("display_name", "");
                entry["portrait_url"] = projected.value("avatar_url", "");
                entry["level"] = projected.value("level", 1);
                entry["archetype"] = projected.value("archetype", json(nullptr));
                entry["sex"] = projected.value("sex", json(nullptr));
            }
        }
        it = cache.emplace(id, std::move(entry)).first;
        return it->second;
    }

    void collect_ids(const std::vector<int64_t>& ids, json& out) {
        for (int64_t id : ids) {
            if (id <= 0) continue;
            out[std::to_string(id)] = get(id);
        }
    }
};

// ── Relationship helpers (fast-path counter) ────────────────────────────────

int64_t relation_count(int64_t character_id) {
    int64_t count = 0;
    ChatDB::instance().db()
        << "SELECT relation_count FROM character_relation_counts WHERE character_id = ?;"
        << character_id >> [&](int64_t v) { count = v; };
    return count;
}

void bump_relation_count(int64_t character_id, int delta) {
    auto& db = ChatDB::instance().db();
    db << "INSERT OR IGNORE INTO character_relation_counts (character_id, relation_count) VALUES (?, 0);"
       << character_id;
    db << "UPDATE character_relation_counts SET relation_count = MAX(0, relation_count + ?) WHERE character_id = ?;"
       << delta << character_id;
}

bool are_friends(int64_t a, int64_t b) {
    bool found = false;
    ChatDB::instance().db()
        << "SELECT 1 FROM friends WHERE character_id = ? AND friend_character_id = ? LIMIT 1;"
        << a << b >> [&](int) { found = true; };
    return found;
}

bool is_blocked_either(int64_t a, int64_t b) {
    bool found = false;
    ChatDB::instance().db()
        << "SELECT 1 FROM blocks WHERE (blocker_character_id = ? AND blocked_character_id = ?) "
           "OR (blocker_character_id = ? AND blocked_character_id = ?) LIMIT 1;"
        << a << b << b << a >> [&](int) { found = true; };
    return found;
}

bool is_person_muted(int64_t muter, int64_t muted) {
    bool found = false;
    ChatDB::instance().db()
        << "SELECT 1 FROM character_mutes WHERE character_id = ? AND muted_character_id = ? LIMIT 1;"
        << muter << muted >> [&](int) { found = true; };
    return found;
}

bool character_exists(int64_t id) {
    bool found = false;
    Database::getInstance().gameDB()
        << "SELECT 1 FROM characters WHERE id = ? LIMIT 1;" << id
        >> [&](int) { found = true; };
    return found;
}

int64_t account_for_character(int64_t character_id) {
    int64_t user_id = 0;
    Database::getInstance().gameDB()
        << "SELECT user_id FROM characters WHERE id = ?;" << character_id
        >> [&](int64_t v) { user_id = v; };
    return user_id;
}

// The account's chosen interface / display language. This is also the default
// chat translation target ("the interface language is the display language").
std::string display_language_for_character(int64_t character_id) {
    std::string language = "en";
    Database::getInstance().gameDB()
        << "SELECT COALESCE(u.language, 'en') FROM characters c JOIN users u ON u.id = c.user_id "
           "WHERE c.id = ?;"
        << character_id >> [&](std::string v) { language = v; };
    return language;
}

// True unless the character explicitly disabled translation for this source
// language (chat_language_prefs.enabled = 0).
bool language_enabled(int64_t character_id, const std::string& language) {
    if (language.empty()) return true;
    bool disabled = false;
    ChatDB::instance().db()
        << "SELECT 1 FROM chat_language_prefs WHERE character_id = ? AND language = ? "
           "AND enabled = 0 LIMIT 1;"
        << character_id << language >> [&](int) { disabled = true; };
    return !disabled;
}

// Case-insensitive "@DisplayName" check used for group push eligibility.
bool body_mentions(const std::string& body, int64_t character_id) {
    std::string name;
    Database::getInstance().gameDB()
        << "SELECT display_name FROM characters WHERE id = ?;" << character_id
        >> [&](std::string n) { name = n; };
    if (name.empty()) return false;
    return to_lower(body).find(to_lower("@" + name)) != std::string::npos;
}

// ── Row model + serialization ───────────────────────────────────────────────

struct MessageRow {
    std::string id;
    std::string chat_id;
    int64_t seq = 0;
    int64_t author = 0;
    std::string body;
    std::string safe_message_id;
    std::string source_language;
    double source_language_confidence = 0.0;
    std::string gif;
    std::string gif_id;
    std::string reply_to;
    int64_t reply_author = 0;
    std::string reply_excerpt;
    std::string reply_safe_excerpt;
    std::string reply_source_language;
    std::string system_id;
    std::string system_args;
    int64_t created_at = 0;
    int64_t edited_at = 0;
    int64_t deleted_at = 0;
    int64_t version = 1;
    int64_t change_id = 0;
};

const char* k_message_columns =
    "m.id, m.chat_id, m.seq, m.author_character_id, m.body, m.safe_message_id, "
    "m.source_language, m.source_language_confidence, m.gif, m.gif_id, "
    "m.reply_to_message_id, m.reply_author_character_id, m.reply_excerpt, "
    "m.reply_safe_excerpt, m.reply_source_language, m.system_id, m.system_args, "
    "m.created_at, m.edited_at, m.deleted_at, m.version, m.change_id";

auto make_message_reader(std::vector<MessageRow>& out) {
    return [&out](std::string id, std::string chat_id, int64_t seq,
                  std::optional<int64_t> author,
                  std::optional<std::string> body,
                  std::optional<std::string> safe_message_id,
                  std::optional<std::string> source_language,
                  std::optional<double> source_language_confidence,
                  std::optional<std::string> gif,
                  std::optional<std::string> gif_id,
                  std::optional<std::string> reply_to,
                  std::optional<int64_t> reply_author,
                  std::optional<std::string> reply_excerpt,
                  std::optional<std::string> reply_safe_excerpt,
                  std::optional<std::string> reply_source_language,
                  std::optional<std::string> system_id,
                  std::optional<std::string> system_args,
                  int64_t created_at,
                  std::optional<int64_t> edited_at,
                  std::optional<int64_t> deleted_at,
                  int64_t version,
                  int64_t change_id) {
        MessageRow r;
        r.id = std::move(id);
        r.chat_id = std::move(chat_id);
        r.seq = seq;
        r.author = author.value_or(0);
        r.body = body.value_or("");
        r.safe_message_id = safe_message_id.value_or("");
        r.source_language = source_language.value_or("");
        r.source_language_confidence = source_language_confidence.value_or(0.0);
        r.gif = gif.value_or("");
        r.gif_id = gif_id.value_or("");
        r.reply_to = reply_to.value_or("");
        r.reply_author = reply_author.value_or(0);
        r.reply_excerpt = reply_excerpt.value_or("");
        r.reply_safe_excerpt = reply_safe_excerpt.value_or("");
        r.reply_source_language = reply_source_language.value_or("");
        r.system_id = system_id.value_or("");
        r.system_args = system_args.value_or("");
        r.created_at = created_at;
        r.edited_at = edited_at.value_or(0);
        r.deleted_at = deleted_at.value_or(0);
        r.version = version;
        r.change_id = change_id;
        out.push_back(std::move(r));
    };
}

json reactions_for(const std::string& message_id, GameConfigCache* config,
                   bool viewer_is_adult) {
    std::map<std::string, std::vector<int64_t>> by_reaction;
    ChatDB::instance().db()
        << "SELECT emoji, character_id FROM message_reactions WHERE message_id = ? "
           "ORDER BY created_at ASC;"
        << message_id
        >> [&](std::string reaction_id, int64_t cid) {
               by_reaction[reaction_id].push_back(cid);
           };
    json arr = json::array();
    for (auto& [reaction_id, ids] : by_reaction) {
        if (!config) continue;
        auto entry = visibility::find_safe_entry(*config, visibility::safe_catalog_kind::reaction,
                                                 reaction_id);
        if (!entry) continue;
        if (!viewer_is_adult && !entry->value("minor_safe", false)) continue;
        arr.push_back({{"reaction_id", reaction_id},
                       {"emoji", entry->value("emoji", std::string())},
                       {"character_ids", ids}});
    }
    return arr;
}

json translation_for(const std::string& message_id, const std::string& viewer_lang) {
    if (viewer_lang.empty()) return nullptr;
    json result = nullptr;
    ChatDB::instance().db()
        << "SELECT body, engine, target_language, detected_source_language, confidence "
           "FROM message_translations WHERE message_id = ? AND target_language = ? "
           "ORDER BY created_at DESC LIMIT 1;"
        << message_id << viewer_lang
        >> [&](std::string body, std::string engine, std::string target,
               std::optional<std::string> detected, std::optional<double> confidence) {
               result = {
                   {"body", body},
                   {"engine", engine},
                   {"language", target},
                   {"detected_source_language", detected.value_or("")},
                   {"confidence", confidence.value_or(0.0)}};
           };
    return result;
}

json message_to_json(const MessageRow& r, CharacterDirectory& dir,
                     const std::string& viewer_lang, GameConfigCache* config,
                     bool viewer_is_adult) {
    // A minor may only receive server-owned safe messages. Custom messages are
    // completely invisible (no placeholder, no unread, no preview).
    const bool is_safe_message = !r.safe_message_id.empty();
    const bool is_system_message = !r.system_id.empty();
    if (!viewer_is_adult && !is_safe_message && !is_system_message) {
        return json(nullptr);
    }

    json m;
    m["id"] = r.id;
    m["chat_id"] = r.chat_id;
    m["seq"] = r.seq;
    m["author_character_id"] = r.author;
    if (is_safe_message && config) {
        m["body"] = visibility::safe_message_body(*config, r.safe_message_id);
        m["safe_message_id"] = r.safe_message_id;
    } else {
        m["body"] = r.body;
        m["safe_message_id"] = nullptr;
    }
    m["source_language"] = is_safe_message ? "" : r.source_language;
    m["source_language_confidence"] = is_safe_message ? 0.0 : r.source_language_confidence;

    json gif = nullptr;
    if (!r.gif_id.empty() && config) {
        auto entry = visibility::find_safe_entry(*config, visibility::safe_catalog_kind::gif,
                                                 r.gif_id);
        if (entry && (viewer_is_adult || entry->value("minor_safe", false))) {
            const std::string asset_url = entry->value("asset_url", std::string());
            gif = {{"id", r.gif_id},
                   {"url", asset_url},
                   {"preview_url", entry->value("preview_url", asset_url)},
                   {"minor_safe", entry->value("minor_safe", false)}};
        }
    }
    m["gif"] = gif;
    m["created_at"] = r.created_at;
    m["edited_at"] = r.edited_at;
    m["deleted"] = r.deleted_at != 0;
    m["version"] = r.version;
    m["change_id"] = r.change_id;
    m["system_id"] = r.system_id;
    m["system_args"] = r.system_args.empty() ? json::object()
                                             : json::parse(r.system_args, nullptr, false);
    if (!r.reply_to.empty()) {
        const std::string excerpt =
            viewer_is_adult ? (!r.reply_excerpt.empty() ? r.reply_excerpt : r.reply_safe_excerpt)
                            : r.reply_safe_excerpt;
        if (!viewer_is_adult && excerpt.empty()) {
            m["reply"] = nullptr;
        } else {
            m["reply"] = {{"message_id", r.reply_to},
                          {"author_character_id", r.reply_author},
                          {"excerpt", excerpt},
                          {"source_language", viewer_is_adult ? r.reply_source_language : ""}};
        }
    } else {
        m["reply"] = nullptr;
    }
    if (r.author > 0) {
        m["author"] = dir.get(r.author);
    } else {
        m["author"] = nullptr;
    }
    m["reactions"] = reactions_for(r.id, config, viewer_is_adult);
    m["translation"] = (viewer_is_adult && !is_safe_message)
                           ? translation_for(r.id, viewer_lang)
                           : json(nullptr);
    return m;
}

// ── Membership / chat helpers ───────────────────────────────────────────────

json membership_row(const std::string& chat_id, const std::string& role,
                    int64_t sticky, int64_t muted, const std::string& notify_level,
                    int64_t last_read_seq, int64_t unread_count,
                    int64_t joined_at, int64_t left_at, int64_t change_id) {
    return {{"chat_id", chat_id},
            {"role", role},
            {"sticky", sticky != 0},
            {"muted", muted != 0},
            {"notify_level", notify_level},
            {"last_read_seq", last_read_seq},
            {"unread_count", unread_count},
            {"joined_at", joined_at},
            {"left_at", left_at},
            {"change_id", change_id}};
}

json chat_row(const std::string& id, const std::string& type,
              const std::optional<std::string>& name,
              const std::optional<int64_t>& owner, const std::optional<std::string>& direct_key,
              int64_t members_can_invite, int64_t created_at,
              const std::optional<int64_t>& name_changed_at,
              const std::optional<int64_t>& archived_at, int64_t last_seq,
              const std::optional<std::string>& source_kind,
              const std::optional<int64_t>& source_id) {
    return {{"id", id},
            {"type", type},
            {"name", name.value_or("")},
            {"owner_character_id", owner.value_or(0)},
            {"direct_key", direct_key.value_or("")},
            {"members_can_invite", members_can_invite != 0},
            {"created_at", created_at},
            {"name_changed_at", name_changed_at.value_or(0)},
            {"archived", archived_at.has_value()},
            {"last_seq", last_seq},
            {"source_kind", source_kind.value_or("")},
            {"source_id", source_id.value_or(0)}};
}

// Barony chat titles come from the barony's viewer-relative identity, never
// from the denormalized chats.name column.
std::string resolve_chat_name(GameConfigCache* config, bool viewer_is_adult,
                              const std::string& type, const std::string& stored_name,
                              int64_t source_id) {
    if (type == "barony" && source_id > 0 && config) {
        visibility::barony_identity barony;
        barony.id = source_id;
        Database::getInstance().gameDB()
            << "SELECT COALESCE(safe_name_id, ''), COALESCE(safe_tag_ids, '[]'), "
               "COALESCE(name, ''), COALESCE(description, ''), accepts_minors "
               "FROM baronies WHERE id = ?;"
            << source_id
            >> [&](std::string safe_name_id, std::string safe_tag_ids, std::string custom_name,
                   std::string description, int accepts_minors) {
                   barony.safe_name_id = std::move(safe_name_id);
                   try {
                       json tags = json::parse(safe_tag_ids);
                       if (tags.is_array()) {
                           for (const auto& tag : tags) {
                               if (tag.is_string()) barony.safe_tag_ids.push_back(tag.get<std::string>());
                           }
                       }
                   } catch (...) {
                   }
                   barony.custom_name = std::move(custom_name);
                   barony.custom_description = std::move(description);
                   barony.accepts_minors = accepts_minors != 0;
               };
        visibility::viewer_context viewer;
        viewer.is_adult = viewer_is_adult;
        std::string name = visibility::barony_display_name(*config, viewer, barony);
        if (!name.empty()) return name;
    }
    if (type == "dukedom" && !viewer_is_adult) return "Dukedom";
    if (type == "barony" && !viewer_is_adult) return "";
    return stored_name;
}

void add_member(const std::string& chat_id, int64_t character_id, const std::string& role,
                int64_t change_id) {
    ChatDB::instance().db()
        << "INSERT INTO chat_members (chat_id, character_id, role, joined_at, left_at, sticky, "
           "muted, notify_level, last_read_seq, unread_count, change_id) "
           "VALUES (?, ?, ?, ?, NULL, 0, 0, 'all', 0, 0, ?) "
           "ON CONFLICT(chat_id, character_id) DO UPDATE SET role = excluded.role, left_at = NULL, change_id = excluded.change_id;"
        << chat_id << character_id << role << now_seconds() << change_id;
}

void check_body_limits(const json& cfg, const std::string& type_key, const std::string& body) {
    json limits = cfg.value("limits", json::object());
    json lim = limits.value(type_key, json::object());
    int max_chars = lim.value("max_characters", 512);
    int max_lines = lim.value("max_lines", 20);
    if (static_cast<int>(utf8_length(body)) > max_chars) {
        throw chat_error("message_too_long");
    }
    if (line_count(body) > max_lines) {
        throw chat_error("too_many_lines");
    }
}

std::string limit_key_for_chat(const std::string& type, bool friends) {
    if (type == "wildlands") return "wildlands";
    if (type == "barony") return "barony";
    if (type == "dukedom") return "dukedom";
    if (type == "group") return "group";
    return friends ? "direct_friend" : "direct";
}

struct ChatMeta {
    std::string id;
    std::string type;
    std::string name;
    int64_t owner = 0;
    std::string direct_key;
    int64_t last_seq = 0;
};

bool load_chat_meta(const std::string& chat_id, ChatMeta& out) {
    bool found = false;
    ChatDB::instance().db()
        << "SELECT id, type, COALESCE(name, ''), COALESCE(owner_character_id, 0), "
           "COALESCE(direct_key, ''), last_seq FROM chats WHERE id = ?;"
        << chat_id
        >> [&](std::string id, std::string type, std::string name, int64_t owner,
               std::string direct_key, int64_t last_seq) {
               out.id = std::move(id);
               out.type = std::move(type);
               out.name = std::move(name);
               out.owner = owner;
               out.direct_key = std::move(direct_key);
               out.last_seq = last_seq;
               found = true;
           };
    return found;
}

void insert_system_message(const std::string& chat_id, const std::string& system_id,
                           const json& system_args) {
    auto& db = ChatDB::instance().db();
    int64_t change_id = ChatDB::instance().allocate_change_id();
    int64_t seq = ChatDB::instance().allocate_seq(chat_id, change_id);
    db << "INSERT INTO messages (id, chat_id, seq, author_character_id, body, source_language, "
          "source_language_confidence, gif, reply_to_message_id, reply_author_character_id, "
          "reply_excerpt, reply_source_language, system_id, system_args, created_at, edited_at, "
          "deleted_at, version, change_id) "
          "VALUES (?, ?, ?, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, ?, ?, ?, NULL, NULL, 1, ?);"
       << generate_id("m_") << chat_id << seq << system_id << system_args.dump() << now_seconds()
       << change_id;
}

}  // namespace

// ── Service ─────────────────────────────────────────────────────────────────

void ChatService::initialize(GameConfigCache* config_cache) {
    config_ = config_cache;
    bool enabled = true;
    std::string sidecar_url = "http://127.0.0.1:8790";
    int timeout_ms = 2000;
    if (config_) {
        json translation = config_->getTranslationConfig();
        if (translation.is_object()) {
            enabled = translation.value("enabled", true);
            sidecar_url = translation.value("sidecar_url", sidecar_url);
            timeout_ms = translation.value("request_timeout_ms", timeout_ms);
        }
    }
    baseline_engine_ = std::make_shared<MadladTranslationEngine>(sidecar_url, timeout_ms, enabled);
    llm_engine_ = std::make_shared<StubLlmTranslationEngine>();
    if (config_) {
        PushSender::instance().configure(config_->getPushConfig());
    }
}

// Per-account flood control. Limits: 1/s, 30/min, 100/5min. A breach records a
// violation and applies the next escalating timeout (30s→5m→30m→2h→24h→7d).
// Levels decay after the configured decay window.
void ChatService::check_rate_limit(int64_t account_id) {
    if (account_id <= 0) return;
    auto& db = ChatDB::instance().db();
    int64_t now = now_seconds();

    json cfg = chat_cfg(config_);
    json rate = cfg.value("rate_limit", json::object());
    int per_second = rate.value("per_second", 1);
    int per_minute = rate.value("per_minute", 30);
    int per_5_minutes = rate.value("per_5_minutes", 100);
    int decay = rate.value("decay_seconds", 604800);
    std::vector<int64_t> escalation;
    for (const auto& value : rate.value("escalation_seconds", json::array())) {
        if (value.is_number_integer()) escalation.push_back(value.get<int64_t>());
    }

    int64_t until = 0;
    int64_t level = 0;
    db << "SELECT until, level FROM rate_limit_timeouts WHERE account_id = ?;" << account_id
       >> [&](int64_t u, int64_t l) {
              until = u;
              level = l;
          };
    if (until > now) throw chat_error("rate_limited");

    if (level > 0) {
        int64_t last_violation = 0;
        db << "SELECT COALESCE(MAX(created_at), 0) FROM rate_limit_violations "
              "WHERE account_id = ? AND kind = 'violation';"
           << account_id >> [&](int64_t v) { last_violation = v; };
        if (last_violation > 0 && now - last_violation > decay) level = 0;
    }

    db << "DELETE FROM rate_limit_violations WHERE account_id = ? AND kind = 'request' "
          "AND created_at < ?;"
       << account_id << (now - decay);

    int64_t in_second = 0;
    int64_t in_minute = 0;
    int64_t in_5min = 0;
    // "Per second" uses the current integer second (a rolling 1s window with
    // second-resolution timestamps would span two clock seconds and over-count).
    db << "SELECT COUNT(*) FROM rate_limit_violations WHERE account_id = ? AND kind = 'request' "
          "AND created_at >= ?;"
       << account_id << now >> [&](int64_t v) { in_second = v; };
    db << "SELECT COUNT(*) FROM rate_limit_violations WHERE account_id = ? AND kind = 'request' "
          "AND created_at >= ?;"
       << account_id << (now - 60) >> [&](int64_t v) { in_minute = v; };
    db << "SELECT COUNT(*) FROM rate_limit_violations WHERE account_id = ? AND kind = 'request' "
          "AND created_at >= ?;"
       << account_id << (now - 300) >> [&](int64_t v) { in_5min = v; };

    bool breach = in_second >= per_second || in_minute >= per_minute || in_5min >= per_5_minutes;
    if (breach) {
        level += 1;
        size_t index = escalation.empty()
                           ? 0
                           : static_cast<size_t>(std::min<int64_t>(
                                 level - 1, static_cast<int64_t>(escalation.size()) - 1));
        int64_t duration = escalation.empty() ? 30 : escalation[index];
        db << "INSERT INTO rate_limit_violations (account_id, kind, created_at) "
              "VALUES (?, 'violation', ?);"
           << account_id << now;
        db << "INSERT OR REPLACE INTO rate_limit_timeouts (account_id, level, until) "
              "VALUES (?, ?, ?);"
           << account_id << level << (now + duration);
        throw chat_error("rate_limited");
    }
    db << "INSERT INTO rate_limit_violations (account_id, kind, created_at) VALUES (?, 'request', ?);"
       << account_id << now;
}

bool ChatService::is_admin(int64_t account_id) {
    if (!config_) return false;
    json admins = config_->getChatAdmins();
    if (!admins.is_object()) return false;
    for (const auto& id : admins.value("account_ids", json::array())) {
        if (id.is_number_integer() && id.get<int64_t>() == account_id) return true;
    }
    return false;
}

bool ChatService::can_view_chat(int64_t character_id, const std::string& chat_id) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta)) return false;
    if (meta.type == "group" && !visibility::character_is_adult(character_id)) return false;
    if (meta.type == "wildlands") return true;
    return ChatDB::instance().is_member(chat_id, character_id);
}

std::string ChatService::wildlands_chat_id() {
    std::string id;
    ChatDB::instance().db()
        << "SELECT id FROM chats WHERE type = 'wildlands' LIMIT 1;"
        >> [&](std::string v) { id = v; };
    if (!id.empty()) return id;
    // Lazily create the single global wildlands chat.
    id = generate_id("c_");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "INSERT INTO chats (id, type, name, name_changed_at, owner_character_id, direct_key, "
           "topic_id, source_kind, source_id, members_can_invite, created_at, archived_at, "
           "last_seq, change_id) VALUES (?, 'wildlands', NULL, NULL, NULL, NULL, NULL, NULL, NULL, "
           "0, ?, NULL, 0, ?);"
        << id << now_seconds() << change_id;
    return id;
}

std::string ChatService::ensure_direct_chat(int64_t a, int64_t b) {
    std::string key = make_direct_key(static_cast<int>(a), static_cast<int>(b));
    std::string id;
    ChatDB::instance().db()
        << "SELECT id FROM chats WHERE type = 'direct' AND direct_key = ? LIMIT 1;" << key
        >> [&](std::string v) { id = v; };
    if (!id.empty()) return id;

    id = generate_id("c_");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "INSERT INTO chats (id, type, name, name_changed_at, owner_character_id, direct_key, "
           "topic_id, source_kind, source_id, members_can_invite, created_at, archived_at, "
           "last_seq, change_id) VALUES (?, 'direct', NULL, NULL, NULL, ?, NULL, NULL, NULL, 0, ?, "
           "NULL, 0, ?);"
        << id << key << now_seconds() << change_id;
    add_member(id, a, "member", ChatDB::instance().allocate_change_id());
    add_member(id, b, "member", ChatDB::instance().allocate_change_id());
    return id;
}

// ── Core reads ──────────────────────────────────────────────────────────────

json ChatService::get_chats(int64_t character_id) {
    auto& db = ChatDB::instance().db();
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);

    json chats = json::array();
    json memberships = json::array();
    std::vector<std::string> my_chat_ids;

    const std::string group_filter = dir.viewer_adult ? " " : " AND c.type != 'group' ";
    db << "SELECT c.id, c.type, COALESCE(c.name, ''), COALESCE(c.owner_character_id, 0), "
          "COALESCE(c.direct_key, ''), c.members_can_invite, c.created_at, c.name_changed_at, "
          "c.archived_at, c.last_seq, c.source_kind, c.source_id, cm.role, cm.sticky, cm.muted, "
          "cm.notify_level, cm.last_read_seq, cm.unread_count, cm.joined_at, cm.left_at, cm.change_id "
          "FROM chats c JOIN chat_members cm ON cm.chat_id = c.id "
          "WHERE cm.character_id = ? AND cm.left_at IS NULL AND c.type != 'wildlands'" + group_filter +
          "ORDER BY cm.sticky DESC, c.created_at DESC;"
       << character_id
       >> [&](std::string id, std::string type, std::string name, int64_t owner,
              std::string direct_key, int64_t members_can_invite, int64_t created_at,
              std::optional<int64_t> name_changed_at, std::optional<int64_t> archived_at,
              int64_t last_seq, std::optional<std::string> source_kind,
              std::optional<int64_t> source_id, std::string role, int64_t sticky,
              int64_t muted, std::string notify_level, int64_t last_read_seq,
              int64_t unread_count, int64_t joined_at, std::optional<int64_t> left_at,
              int64_t change_id) {
              my_chat_ids.push_back(id);
              const std::string visible_name = resolve_chat_name(
                  config_, dir.viewer_adult, type, name, source_id.value_or(0));
              chats.push_back(chat_row(id, type, visible_name.empty() ? std::nullopt : std::optional<std::string>(visible_name),
                                       owner > 0 ? std::optional<int64_t>(owner) : std::nullopt,
                                       direct_key.empty() ? std::nullopt : std::optional<std::string>(direct_key),
                                       members_can_invite, created_at, name_changed_at, archived_at,
                                       last_seq, source_kind, source_id));
              memberships.push_back(membership_row(id, role, sticky, muted, notify_level,
                                                   last_read_seq, unread_count, joined_at,
                                                   left_at.value_or(0), change_id));
          };

    // Last message preview per chat.
    json last_messages = json::object();
    for (const auto& chat_id : my_chat_ids) {
        std::vector<MessageRow> rows;
        const std::string safe_filter = dir.viewer_adult
            ? ""
            : " AND (m.safe_message_id IS NOT NULL OR m.system_id IS NOT NULL) ";
        db << std::string("SELECT ") + k_message_columns +
                  " FROM messages m WHERE m.chat_id = ?" + safe_filter +
                  " ORDER BY m.seq DESC LIMIT 1;"
           << chat_id >> make_message_reader(rows);
        if (!rows.empty()) {
            json last = message_to_json(rows.front(), dir, "", config_, dir.viewer_adult);
            if (!last.is_null()) last_messages[chat_id] = std::move(last);
        }
    }

    // Directory for everyone referenced.
    std::vector<int64_t> ids;
    for (const auto& chat : chats) {
        int64_t owner = chat.value("owner_character_id", 0);
        if (owner > 0) ids.push_back(owner);
    }
    json characters = json::object();
    dir.collect_ids(ids, characters);

    json result;
    result["cursor"] = ChatDB::instance().current_change_id();
    result["chats"] = chats;
    result["memberships"] = memberships;
    result["last_messages"] = last_messages;
    result["characters"] = characters;
    result["wildlands_chat_id"] = wildlands_chat_id();
    result["blocked_character_ids"] = json::array();
    result["muted_character_ids"] = json::array();
    result["limits"] = chat_cfg(config_).value("limits", json::object());
    return result;
}

json ChatService::get_messages(int64_t character_id, const std::string& chat_id,
                               int64_t before_seq, int64_t after_seq, int limit,
                               const std::string& viewer_lang) {
    if (!can_view_chat(character_id, chat_id)) {
        throw chat_error("not_a_member");
    }
    if (limit <= 0 || limit > 200) limit = 50;

    auto& db = ChatDB::instance().db();
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);
    std::vector<MessageRow> rows;
    const std::string safe_filter = dir.viewer_adult
        ? ""
        : " AND (m.safe_message_id IS NOT NULL OR m.system_id IS NOT NULL) ";

    if (after_seq > 0) {
        db << std::string("SELECT ") + k_message_columns +
                  " FROM messages m WHERE m.chat_id = ? AND m.seq > ?" + safe_filter +
                  " ORDER BY m.seq ASC LIMIT ?;"
           << chat_id << after_seq << limit >> make_message_reader(rows);
    } else {
        int64_t upper = before_seq > 0 ? before_seq : (std::numeric_limits<int64_t>::max)();
        db << std::string("SELECT ") + k_message_columns +
                  " FROM messages m WHERE m.chat_id = ? AND m.seq < ?" + safe_filter +
                  " ORDER BY m.seq DESC LIMIT ?;"
           << chat_id << upper << limit >> make_message_reader(rows);
        std::reverse(rows.begin(), rows.end());
    }

    json messages = json::array();
    for (const auto& r : rows) {
        json message = message_to_json(r, dir, viewer_lang, config_, dir.viewer_adult);
        if (!message.is_null()) messages.push_back(std::move(message));
    }

    json result;
    result["chat_id"] = chat_id;
    result["messages"] = messages;
    result["has_more"] = static_cast<int>(rows.size()) == limit;
    result["characters"] = json::object();
    return result;
}

json ChatService::poll(int64_t character_id, int64_t cursor, const std::string& viewer_lang) {
    auto& db = ChatDB::instance().db();
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);
    const int cap = 200;

    json result;
    result["cursor"] = cursor;
    result["has_more"] = false;
    result["chats"] = json::array();
    result["memberships"] = json::array();
    result["members"] = json::array();
    result["messages"] = json::array();
    result["translations"] = json::array();
    result["friend_events"] = json::array();
    result["invites"] = json::array();
    result["bans"] = json::array();

    std::set<int64_t> character_ids;

    // Chats I belong to that changed.
    const std::string group_filter = dir.viewer_adult ? " " : " AND c.type != 'group' ";
    db << "SELECT c.id, c.type, COALESCE(c.name, ''), COALESCE(c.owner_character_id, 0), "
          "COALESCE(c.direct_key, ''), c.members_can_invite, c.created_at, c.name_changed_at, "
          "c.archived_at, c.last_seq, c.source_kind, c.source_id "
          "FROM chats c WHERE c.change_id > ? AND c.type != 'wildlands'" + group_filter +
          "AND EXISTS (SELECT 1 FROM chat_members cm WHERE cm.chat_id = c.id AND cm.character_id = ? AND cm.left_at IS NULL) "
          "ORDER BY c.change_id ASC LIMIT ?;"
       << cursor << character_id << cap
       >> [&](std::string id, std::string type, std::string name, int64_t owner,
              std::string direct_key, int64_t members_can_invite, int64_t created_at,
              std::optional<int64_t> name_changed_at, std::optional<int64_t> archived_at,
              int64_t last_seq, std::optional<std::string> source_kind,
              std::optional<int64_t> source_id) {
              const std::string visible_name = resolve_chat_name(
                  config_, dir.viewer_adult, type, name, source_id.value_or(0));
              result["chats"].push_back(chat_row(
                  id, type, visible_name.empty() ? std::nullopt : std::optional<std::string>(visible_name),
                  owner > 0 ? std::optional<int64_t>(owner) : std::nullopt,
                  direct_key.empty() ? std::nullopt : std::optional<std::string>(direct_key),
                  members_can_invite, created_at, name_changed_at, archived_at, last_seq,
                  source_kind, source_id));
              if (owner > 0) character_ids.insert(owner);
          };

    // My membership rows. Minors never see group membership rows.
    const std::string membership_group_filter = dir.viewer_adult
        ? " "
        : " AND chat_id NOT IN (SELECT id FROM chats WHERE type = 'group') ";
    db << "SELECT chat_id, role, sticky, muted, notify_level, last_read_seq, unread_count, "
          "joined_at, left_at, change_id FROM chat_members "
          "WHERE character_id = ? AND change_id > ?" + membership_group_filter +
          "ORDER BY change_id ASC LIMIT ?;"
       << character_id << cursor << cap
       >> [&](std::string chat_id, std::string role, int64_t sticky, int64_t muted,
              std::string notify_level, int64_t last_read_seq, int64_t unread_count,
              int64_t joined_at, std::optional<int64_t> left_at, int64_t change_id) {
              result["memberships"].push_back(membership_row(chat_id, role, sticky, muted,
                                                             notify_level, last_read_seq,
                                                             unread_count, joined_at,
                                                             left_at.value_or(0), change_id));
          };

    // Other members' rows for chats I'm in.
    const std::string member_group_filter = dir.viewer_adult
        ? " "
        : " AND cm.chat_id NOT IN (SELECT id FROM chats WHERE type = 'group') ";
    db << "SELECT cm.chat_id, cm.character_id, cm.role, cm.sticky, cm.muted, cm.notify_level, "
          "cm.last_read_seq, cm.unread_count, cm.joined_at, cm.left_at, cm.change_id "
          "FROM chat_members cm WHERE cm.change_id > ?" + member_group_filter +
          "AND cm.chat_id IN (SELECT chat_id FROM chat_members WHERE character_id = ? AND left_at IS NULL) "
          "AND cm.character_id != ? ORDER BY cm.change_id ASC LIMIT ?;"
       << cursor << character_id << character_id << cap
       >> [&](std::string chat_id, int64_t cid, std::string role, int64_t sticky, int64_t muted,
              std::string notify_level, int64_t last_read_seq, int64_t unread_count,
              int64_t joined_at, std::optional<int64_t> left_at, int64_t change_id) {
              json row = membership_row(chat_id, role, sticky, muted, notify_level, last_read_seq,
                                        unread_count, joined_at, left_at.value_or(0), change_id);
              row["character_id"] = cid;
              result["members"].push_back(row);
              character_ids.insert(cid);
          };

    // Messages in my chats.
    std::vector<MessageRow> rows;
    const std::string safe_filter = dir.viewer_adult
        ? " "
        : " AND (m.safe_message_id IS NOT NULL OR m.system_id IS NOT NULL) ";
    db << std::string("SELECT ") + k_message_columns +
              " FROM messages m WHERE m.change_id > ? AND m.chat_id != ?" + safe_filter +
              "AND EXISTS (SELECT 1 FROM chat_members cm WHERE cm.chat_id = m.chat_id "
              "AND cm.character_id = ? AND cm.left_at IS NULL) "
              "ORDER BY m.change_id ASC LIMIT ?;"
       << cursor << wildlands_chat_id() << character_id << cap
       >> make_message_reader(rows);

    int64_t max_change = cursor;
    for (const auto& r : rows) {
        if (r.change_id > max_change) max_change = r.change_id;
        json message = message_to_json(r, dir, viewer_lang, config_, dir.viewer_adult);
        if (message.is_null()) continue;
        if (r.author > 0) character_ids.insert(r.author);
        if (r.reply_author > 0) character_ids.insert(r.reply_author);
        result["messages"].push_back(std::move(message));
    }

    // Translations produced for my language since the cursor, in chats I'm in.
    // Minors never receive custom translations (safe messages use server text).
    if (!viewer_lang.empty() && dir.viewer_adult) {
        db << "SELECT t.message_id, t.body, t.engine, t.detected_source_language, t.confidence, t.change_id "
              "FROM message_translations t JOIN messages m ON m.id = t.message_id "
              "WHERE t.change_id > ? AND t.target_language = ? "
              "AND EXISTS (SELECT 1 FROM chat_members cm WHERE cm.chat_id = m.chat_id "
              "AND cm.character_id = ? AND cm.left_at IS NULL) "
              "ORDER BY t.change_id ASC LIMIT ?;"
           << cursor << viewer_lang << character_id << cap
           >> [&](std::string message_id, std::string body, std::string engine,
                  std::optional<std::string> detected, std::optional<double> confidence,
                  int64_t change_id) {
                  result["translations"].push_back(
                      {{"message_id", message_id},
                       {"body", body},
                       {"engine", engine},
                       {"language", viewer_lang},
                       {"detected_source_language", detected.value_or("")},
                       {"confidence", confidence.value_or(0.0)},
                       {"change_id", change_id}});
                  if (change_id > max_change) max_change = change_id;
              };
    }

    // Friend events (requests/friends/blocks/mutes) touching me.
    db << "SELECT 'request', from_character_id, to_character_id, created_at, change_id "
          "FROM friend_requests WHERE change_id > ? AND (from_character_id = ? OR to_character_id = ?) "
          "UNION ALL SELECT 'friend', character_id, friend_character_id, created_at, change_id "
          "FROM friends WHERE change_id > ? AND (character_id = ? OR friend_character_id = ?) "
          "UNION ALL SELECT 'block', blocker_character_id, blocked_character_id, created_at, change_id "
          "FROM blocks WHERE change_id > ? AND (blocker_character_id = ? OR blocked_character_id = ?) "
          "UNION ALL SELECT 'mute', character_id, muted_character_id, created_at, change_id "
          "FROM character_mutes WHERE change_id > ? AND (character_id = ? OR muted_character_id = ?) "
          "ORDER BY 5 ASC LIMIT ?;"
       << cursor << character_id << character_id
       << cursor << character_id << character_id
       << cursor << character_id << character_id
       << cursor << character_id << character_id << cap
       >> [&](std::string kind, int64_t a, int64_t b, int64_t created_at, int64_t change_id) {
              result["friend_events"].push_back(
                  {{"kind", kind}, {"a", a}, {"b", b}, {"created_at", created_at},
                   {"change_id", change_id}});
              character_ids.insert(a);
              character_ids.insert(b);
              if (change_id > max_change) max_change = change_id;
          };

    // Group invites addressed to me. Minors never receive group invites.
    const std::string invite_filter = dir.viewer_adult
        ? ""
        : " AND chat_id NOT IN (SELECT id FROM chats WHERE type = 'group') ";
    db << "SELECT chat_id, invited_by_character_id, created_at, expires_at, change_id "
          "FROM chat_invites WHERE invited_character_id = ? AND change_id > ?" + invite_filter +
          " ORDER BY change_id ASC LIMIT ?;"
       << character_id << cursor << cap
       >> [&](std::string chat_id, int64_t invited_by, int64_t created_at, int64_t expires_at,
              int64_t change_id) {
              result["invites"].push_back({{"chat_id", chat_id},
                                           {"invited_by_character_id", invited_by},
                                           {"created_at", created_at},
                                           {"expires_at", expires_at},
                                           {"change_id", change_id}});
              character_ids.insert(invited_by);
              if (change_id > max_change) max_change = change_id;
          };

    // Bans affecting me. Group bans are hidden from minors.
    const std::string ban_group_filter = dir.viewer_adult
        ? " "
        : " AND chat_id NOT IN (SELECT id FROM chats WHERE type = 'group') ";
    db << "SELECT chat_id, banned_by_character_id, COALESCE(reason, ''), created_at, change_id "
          "FROM chat_bans WHERE character_id = ? AND change_id > ?" + ban_group_filter +
          "ORDER BY change_id ASC LIMIT ?;"
       << character_id << cursor << cap
       >> [&](std::string chat_id, std::optional<int64_t> banned_by, std::string reason,
              int64_t created_at, int64_t change_id) {
              if (!dir.viewer_adult) {
                  // Ban reasons are custom moderator text; minors receive a
                  // generic server-owned safe reason.
                  reason = config_
                      ? visibility::safe_label(*config_,
                                               visibility::safe_catalog_kind::ban_reason,
                                               "moderator_action")
                      : std::string();
              }
              result["bans"].push_back({{"chat_id", chat_id},
                                        {"banned_by_character_id", banned_by.value_or(0)},
                                        {"reason", reason},
                                        {"created_at", created_at},
                                        {"change_id", change_id}});
              if (banned_by.value_or(0) > 0) character_ids.insert(banned_by.value_or(0));
              if (change_id > max_change) max_change = change_id;
          };

    json characters = json::object();
    for (int64_t id : character_ids) {
        characters[std::to_string(id)] = dir.get(id);
    }
    result["characters"] = characters;
    result["cursor"] = max_change;
    result["has_more"] = static_cast<int>(rows.size()) >= cap;
    return result;
}

json ChatService::poll_wildlands(int64_t character_id, int64_t cursor,
                                 const std::string& viewer_lang) {
    (void)character_id;
    auto& db = ChatDB::instance().db();
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);
    const int cap = 200;
    std::string wid = wildlands_chat_id();

    json result;
    result["cursor"] = cursor;
    result["has_more"] = false;
    result["messages"] = json::array();
    result["translations"] = json::array();
    result["characters"] = json::object();

    std::vector<MessageRow> rows;
    const std::string safe_filter = dir.viewer_adult
        ? ""
        : " AND (m.safe_message_id IS NOT NULL OR m.system_id IS NOT NULL) ";
    db << std::string("SELECT ") + k_message_columns +
              " FROM messages m WHERE m.change_id > ? AND m.chat_id = ?" + safe_filter +
              " ORDER BY m.change_id ASC LIMIT ?;"
       << cursor << wid << cap >> make_message_reader(rows);

    std::set<int64_t> character_ids;
    int64_t max_change = cursor;
    for (const auto& r : rows) {
        if (r.change_id > max_change) max_change = r.change_id;
        json message = message_to_json(r, dir, viewer_lang, config_, dir.viewer_adult);
        if (message.is_null()) continue;
        if (r.author > 0) character_ids.insert(r.author);
        if (r.reply_author > 0) character_ids.insert(r.reply_author);
        result["messages"].push_back(std::move(message));
    }
    if (!viewer_lang.empty() && dir.viewer_adult) {
        db << "SELECT t.message_id, t.body, t.engine, t.detected_source_language, t.confidence, t.change_id "
              "FROM message_translations t JOIN messages m ON m.id = t.message_id "
              "WHERE t.change_id > ? AND t.target_language = ? AND m.chat_id = ? "
              "ORDER BY t.change_id ASC LIMIT ?;"
           << cursor << viewer_lang << wid << cap
           >> [&](std::string message_id, std::string body, std::string engine,
                  std::optional<std::string> detected, std::optional<double> confidence,
                  int64_t change_id) {
                  result["translations"].push_back(
                      {{"message_id", message_id},
                       {"body", body},
                       {"engine", engine},
                       {"language", viewer_lang},
                       {"detected_source_language", detected.value_or("")},
                       {"confidence", confidence.value_or(0.0)},
                       {"change_id", change_id}});
                  if (change_id > max_change) max_change = change_id;
              };
    }
    json characters = json::object();
    for (int64_t id : character_ids) characters[std::to_string(id)] = dir.get(id);
    result["characters"] = characters;
    result["cursor"] = max_change;
    result["has_more"] = static_cast<int>(rows.size()) >= cap;
    return result;
}

namespace {
// Bump unread counters for a new message. Fast path: when the author has no
// block/mute relationships at all, a single bulk UPDATE (excluding self and
// chat-muted members) is provably correct. Otherwise check each recipient.
void apply_unread(const std::string& chat_id, int64_t author, int64_t change_id,
                  bool message_is_safe) {
    auto& db = ChatDB::instance().db();
    if (message_is_safe && relation_count(author) == 0) {
        db << "UPDATE chat_members SET unread_count = unread_count + 1, change_id = ? "
              "WHERE chat_id = ? AND character_id != ? AND left_at IS NULL AND muted = 0;"
           << change_id << chat_id << author;
        return;
    }
    std::vector<int64_t> members;
    db << "SELECT character_id FROM chat_members WHERE chat_id = ? AND character_id != ? "
          "AND left_at IS NULL AND muted = 0;"
       << chat_id << author >> [&](int64_t cid) { members.push_back(cid); };
    for (int64_t cid : members) {
        // A custom message is invisible to minors, so it must never create an
        // unread badge for them.
        if (!message_is_safe && !visibility::character_is_adult(cid)) continue;
        if (is_blocked_either(cid, author)) continue;
        if (is_person_muted(cid, author)) continue;
        db << "UPDATE chat_members SET unread_count = unread_count + 1, change_id = ? "
              "WHERE chat_id = ? AND character_id = ?;"
           << change_id << chat_id << cid;
    }
}

void log_moderation(int64_t actor, const std::string& action, const std::string& chat_id,
                    int64_t target, const json& payload) {
    ChatDB::instance().db()
        << "INSERT INTO moderation_log (created_at, actor_character_id, action, chat_id, "
           "target_character_id, payload) VALUES (?, ?, ?, ?, ?, ?);"
        << now_seconds() << actor << action
        << (chat_id.empty() ? std::optional<std::string>(std::nullopt)
                            : std::optional<std::string>(chat_id))
        << (target > 0 ? std::optional<int64_t>(target) : std::optional<int64_t>(std::nullopt))
        << payload.dump();
}
}  // namespace

json ChatService::send_message(int64_t character_id, const std::string& chat_id,
                               const std::string& safe_message_id, const std::string& body,
                               const std::string& gif_id,
                               const std::string& reply_to_message_id,
                               const std::string& client_id,
                               const std::string& sender_language) {
    (void)client_id;
    auto& db = ChatDB::instance().db();
    const bool author_is_adult = visibility::character_is_adult(character_id);

    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta)) throw chat_error("chat_not_found");
    bool wildlands = meta.type == "wildlands";
    if (!wildlands && !ChatDB::instance().is_member(chat_id, character_id)) {
        throw chat_error("not_a_member");
    }
    if (meta.type == "group" && !author_is_adult) throw chat_error("group_adults_only");
    bool banned = false;
    db << "SELECT 1 FROM chat_bans WHERE chat_id = ? AND character_id = ? LIMIT 1;"
       << chat_id << character_id >> [&](int) { banned = true; };
    if (banned) throw chat_error("banned_from_chat");

    const bool has_safe_message = !safe_message_id.empty();
    if (has_safe_message) {
        auto entry = config_
            ? visibility::find_safe_entry(*config_, visibility::safe_catalog_kind::message,
                                          safe_message_id)
            : std::nullopt;
        if (!entry) throw chat_error("invalid_safe_message");
        if (entry->contains("contexts") && (*entry)["contexts"].is_array()) {
            bool context_ok = false;
            for (const auto& context : (*entry)["contexts"]) {
                if (context.is_string() && context.get<std::string>() == meta.type) {
                    context_ok = true;
                    break;
                }
            }
            if (!context_ok) throw chat_error("invalid_safe_message_context");
        }
        if (!author_is_adult && !body.empty()) throw chat_error("minor_custom_content_forbidden");
    } else if (!author_is_adult) {
        // Minors may only author server-owned safe content.
        throw chat_error("minor_custom_content_forbidden");
    }

    if (!gif_id.empty()) {
        if (!config_ || !visibility::is_valid_safe_id(
                            *config_, visibility::safe_catalog_kind::gif, gif_id)) {
            throw chat_error("invalid_gif");
        }
        if (!author_is_adult && !visibility::is_minor_safe(
                                    *config_, visibility::safe_catalog_kind::gif, gif_id)) {
            throw chat_error("gif_not_minor_safe");
        }
    }
    if (has_safe_message && gif_id.empty() && body.empty()) {
        // Safe text is valid on its own; this branch only documents intent.
    }
    if (!has_safe_message && body.empty() && gif_id.empty()) throw chat_error("empty_message");

    bool friends = false;
    if (meta.type == "direct") {
        int64_t other = 0;
        db << "SELECT character_id FROM chat_members WHERE chat_id = ? AND character_id != ? LIMIT 1;"
           << chat_id << character_id >> [&](int64_t v) { other = v; };
        if (other > 0) {
            if (is_blocked_either(character_id, other)) throw chat_error("blocked");
            friends = are_friends(character_id, other);
        }
    }
    if (!has_safe_message) {
        check_body_limits(chat_cfg(config_), limit_key_for_chat(meta.type, friends), body);
    }
    check_rate_limit(account_for_character(character_id));

    // Detect the source language for custom text only. Safe messages are
    // server-owned strings and never enter the translation pipeline.
    std::string source_language;
    double source_confidence = 0.0;
    if (!has_safe_message && baseline_engine_ && baseline_engine_->available() && !body.empty()) {
        auto detected = baseline_engine_->detect(body);
        if (detected) {
            source_language = detected->first;
            source_confidence = detected->second;
        }
    }
    if (!has_safe_message && source_language.empty()) source_language = sender_language;

    int64_t reply_author = 0;
    std::string reply_excerpt;
    std::string reply_safe_excerpt;
    std::string reply_src;
    if (!reply_to_message_id.empty()) {
        std::string parent_safe_id;
        db << "SELECT COALESCE(author_character_id, 0), COALESCE(body, ''), "
              "COALESCE(safe_message_id, ''), COALESCE(source_language, '') "
              "FROM messages WHERE id = ? AND chat_id = ?;"
           << reply_to_message_id << chat_id
           >> [&](int64_t a, std::string b, std::string safe_id, std::string s) {
                  reply_author = a;
                  reply_excerpt = b;
                  parent_safe_id = safe_id;
                  reply_src = s;
              };
        if (!author_is_adult && parent_safe_id.empty()) {
            throw chat_error("message_not_visible");
        }
        if (!parent_safe_id.empty() && config_) {
            reply_safe_excerpt = visibility::safe_message_body(*config_, parent_safe_id);
        }
        if (utf8_length(reply_excerpt) > 120) {
            reply_excerpt = reply_excerpt.substr(0, 120);
        }
        if (utf8_length(reply_safe_excerpt) > 120) {
            reply_safe_excerpt = reply_safe_excerpt.substr(0, 120);
        }
    }

    int64_t change_id = ChatDB::instance().allocate_change_id();
    int64_t seq = ChatDB::instance().allocate_seq(chat_id, change_id);
    std::string id = generate_id("m_");
    db << "INSERT INTO messages (id, chat_id, seq, author_character_id, body, safe_message_id, "
          "source_language, source_language_confidence, gif, gif_id, reply_to_message_id, "
          "reply_author_character_id, reply_excerpt, reply_safe_excerpt, reply_source_language, "
          "system_id, system_args, created_at, edited_at, deleted_at, version, change_id) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?, ?, ?, ?, ?, NULL, NULL, ?, NULL, NULL, 1, ?);"
       << id << chat_id << seq << character_id
       << (has_safe_message ? std::optional<std::string>() :
                              (body.empty() ? std::optional<std::string>()
                                            : std::optional<std::string>(body)))
       << (has_safe_message ? std::optional<std::string>(safe_message_id)
                            : std::optional<std::string>())
       << (source_language.empty() ? std::optional<std::string>()
                                    : std::optional<std::string>(source_language))
       << (source_language.empty() ? std::optional<double>() : std::optional<double>(source_confidence))
       << (gif_id.empty() ? std::optional<std::string>() : std::optional<std::string>(gif_id))
       << (reply_to_message_id.empty() ? std::optional<std::string>()
                                        : std::optional<std::string>(reply_to_message_id))
       << (reply_author > 0 ? std::optional<int64_t>(reply_author) : std::optional<int64_t>())
       << (reply_excerpt.empty() ? std::optional<std::string>()
                                  : std::optional<std::string>(reply_excerpt))
       << (reply_safe_excerpt.empty() ? std::optional<std::string>()
                                      : std::optional<std::string>(reply_safe_excerpt))
       << (reply_src.empty() ? std::optional<std::string>()
                              : std::optional<std::string>(reply_src))
       << now_seconds() << change_id;

    const std::string push_body = has_safe_message && config_
        ? visibility::safe_message_body(*config_, safe_message_id)
        : body;
    apply_unread(chat_id, character_id, change_id, has_safe_message);
    dispatch_push(chat_id, character_id, id, push_body, source_language, safe_message_id);

    MessageRow r;
    r.id = id;
    r.chat_id = chat_id;
    r.seq = seq;
    r.author = character_id;
    r.body = has_safe_message ? "" : body;
    r.safe_message_id = safe_message_id;
    r.source_language = source_language;
    r.source_language_confidence = source_confidence;
    r.gif_id = gif_id;
    r.reply_to = reply_to_message_id;
    r.reply_author = reply_author;
    r.reply_excerpt = reply_excerpt;
    r.reply_safe_excerpt = reply_safe_excerpt;
    r.reply_source_language = reply_src;
    r.created_at = now_seconds();
    r.version = 1;
    r.change_id = change_id;
    CharacterDirectory dir(author_is_adult, character_id);
    json result = message_to_json(r, dir, "", config_, dir.viewer_adult);
    return result.is_null() ? json::object() : result;
}

json ChatService::edit_message(int64_t character_id, const std::string& message_id,
                               const std::string& safe_message_id, const std::string& body,
                               const std::string& gif_id) {
    auto& db = ChatDB::instance().db();
    const bool author_is_adult = visibility::character_is_adult(character_id);
    std::string chat_id;
    int64_t author = 0;
    int64_t version = 0;
    db << "SELECT chat_id, COALESCE(author_character_id, 0), version FROM messages WHERE id = ?;"
       << message_id >> [&](std::string c, int64_t a, int64_t v) {
              chat_id = c;
              author = a;
              version = v;
          };
    if (chat_id.empty()) throw chat_error("message_not_found");
    if (author != character_id) throw chat_error("not_author");
    bool banned = false;
    db << "SELECT 1 FROM chat_bans WHERE chat_id = ? AND character_id = ? LIMIT 1;"
       << chat_id << character_id >> [&](int) { banned = true; };
    if (banned) throw chat_error("banned_from_chat");

    ChatMeta meta;
    load_chat_meta(chat_id, meta);
    if (meta.type == "group" && !author_is_adult) throw chat_error("group_adults_only");
    const bool has_safe_message = !safe_message_id.empty();
    if (has_safe_message) {
        auto entry = config_
            ? visibility::find_safe_entry(*config_, visibility::safe_catalog_kind::message,
                                          safe_message_id)
            : std::nullopt;
        if (!entry) throw chat_error("invalid_safe_message");
        if (entry->contains("contexts") && (*entry)["contexts"].is_array()) {
            bool context_ok = false;
            for (const auto& context : (*entry)["contexts"]) {
                if (context.is_string() && context.get<std::string>() == meta.type) {
                    context_ok = true;
                    break;
                }
            }
            if (!context_ok) throw chat_error("invalid_safe_message_context");
        }
        if (!author_is_adult && !body.empty()) throw chat_error("minor_custom_content_forbidden");
    } else if (!author_is_adult) {
        throw chat_error("minor_custom_content_forbidden");
    }
    if (!gif_id.empty()) {
        if (!config_ || !visibility::is_valid_safe_id(
                            *config_, visibility::safe_catalog_kind::gif, gif_id)) {
            throw chat_error("invalid_gif");
        }
        if (!author_is_adult && !visibility::is_minor_safe(
                                    *config_, visibility::safe_catalog_kind::gif, gif_id)) {
            throw chat_error("gif_not_minor_safe");
        }
    }
    if (!has_safe_message && body.empty() && gif_id.empty()) throw chat_error("empty_message");
    bool friends = false;
    if (meta.type == "direct") {
        int64_t other = 0;
        db << "SELECT character_id FROM chat_members WHERE chat_id = ? AND character_id != ? LIMIT 1;"
           << chat_id << character_id >> [&](int64_t v) { other = v; };
        if (other > 0) friends = are_friends(character_id, other);
    }
    if (!has_safe_message) {
        check_body_limits(chat_cfg(config_), limit_key_for_chat(meta.type, friends), body);
    }

    int64_t new_version = version + 1;
    db << "INSERT OR REPLACE INTO message_versions (message_id, version, body, safe_message_id, "
          "gif_id, editor_character_id, edited_at) VALUES (?, ?, ?, ?, ?, ?, ?);"
       << message_id << new_version
       << (has_safe_message ? std::optional<std::string>()
                            : (body.empty() ? std::optional<std::string>()
                                            : std::optional<std::string>(body)))
       << (has_safe_message ? std::optional<std::string>(safe_message_id)
                            : std::optional<std::string>())
       << (gif_id.empty() ? std::optional<std::string>() : std::optional<std::string>(gif_id))
       << character_id << now_seconds();
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE messages SET body = ?, safe_message_id = ?, gif_id = ?, version = ?, "
          "edited_at = ?, change_id = ? WHERE id = ?;"
       << (has_safe_message ? std::optional<std::string>()
                            : (body.empty() ? std::optional<std::string>()
                                            : std::optional<std::string>(body)))
       << (has_safe_message ? std::optional<std::string>(safe_message_id)
                            : std::optional<std::string>())
       << (gif_id.empty() ? std::optional<std::string>() : std::optional<std::string>(gif_id))
       << new_version << now_seconds() << change_id << message_id;
    // Edits invalidate stored translations.
    db << "DELETE FROM message_translations WHERE message_id = ?;" << message_id;

    json result;
    result["id"] = message_id;
    result["version"] = new_version;
    result["change_id"] = change_id;
    return result;
}

json ChatService::delete_message(int64_t character_id, const std::string& message_id) {
    auto& db = ChatDB::instance().db();
    std::string chat_id;
    int64_t author = 0;
    db << "SELECT chat_id, COALESCE(author_character_id, 0) FROM messages WHERE id = ?;"
       << message_id >> [&](std::string c, int64_t a) {
              chat_id = c;
              author = a;
          };
    if (chat_id.empty()) throw chat_error("message_not_found");
    bool privileged = false;
    if (author != character_id) {
        std::string role = ChatDB::instance().member_role(chat_id, character_id);
        privileged = (role == "owner" || role == "moderator");
        if (!privileged) throw chat_error("not_author");
    }
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE messages SET deleted_at = ?, body = NULL, change_id = ? WHERE id = ?;"
       << now_seconds() << change_id << message_id;
    json result;
    result["id"] = message_id;
    result["change_id"] = change_id;
    return result;
}

json ChatService::mark_read(int64_t character_id, const std::string& chat_id, int64_t seq) {
    if (!ChatDB::instance().is_member(chat_id, character_id)) {
        // Wildlands is implicitly readable; other chats require membership.
        ChatMeta meta;
        if (!load_chat_meta(chat_id, meta) || meta.type != "wildlands") {
            throw chat_error("not_a_member");
        }
    }
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "UPDATE chat_members SET last_read_seq = MAX(last_read_seq, ?), unread_count = 0, "
           "change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << seq << change_id << chat_id << character_id;
    json result;
    result["chat_id"] = chat_id;
    result["last_read_seq"] = seq;
    result["unread_count"] = 0;
    return result;
}

json ChatService::set_settings(int64_t character_id, const std::string& chat_id, const json& body) {
    if (!ChatDB::instance().is_member(chat_id, character_id)) throw chat_error("not_a_member");
    auto& db = ChatDB::instance().db();
    int64_t change_id = ChatDB::instance().allocate_change_id();
    if (body.contains("sticky")) {
        db << "UPDATE chat_members SET sticky = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
           << (body["sticky"].get<bool>() ? 1 : 0) << change_id << chat_id << character_id;
    }
    if (body.contains("muted")) {
        db << "UPDATE chat_members SET muted = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
           << (body["muted"].get<bool>() ? 1 : 0) << change_id << chat_id << character_id;
    }
    if (body.contains("notify_level")) {
        std::string level = body["notify_level"].get<std::string>();
        if (level != "all" && level != "mentions" && level != "none") {
            throw chat_error("invalid_notify_level");
        }
        db << "UPDATE chat_members SET notify_level = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
           << level << change_id << chat_id << character_id;
    }
    json result;
    result["chat_id"] = chat_id;
    return result;
}

json ChatService::open_direct_chat(int64_t character_id, int64_t target_character_id) {
    if (target_character_id == character_id) throw chat_error("cannot_dm_self");
    if (!character_exists(target_character_id)) throw chat_error("character_not_found");
    if (is_blocked_either(character_id, target_character_id)) throw chat_error("blocked");
    std::string chat_id = ensure_direct_chat(character_id, target_character_id);

    auto& db = ChatDB::instance().db();
    ChatMeta meta;
    load_chat_meta(chat_id, meta);
    json result;
    result["chat_id"] = chat_id;
    result["chat"] = chat_row(chat_id, "direct", std::nullopt, std::nullopt,
                              make_direct_key(static_cast<int>(character_id),
                                              static_cast<int>(target_character_id)),
                              false, 0, std::nullopt, std::nullopt, meta.last_seq, std::nullopt,
                              std::nullopt);
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);
    result["target"] = dir.get(target_character_id);
    return result;
}

json ChatService::search_characters(int64_t character_id, const std::string& query) {
    json out = json::array();
    if (query.empty()) return out;
    const bool viewer_is_adult = visibility::character_is_adult(character_id);
    const std::string like = "%" + query + "%";
    std::vector<int64_t> ids;
    auto& gdb = Database::getInstance().gameDB();
    if (viewer_is_adult) {
        gdb << "SELECT id FROM characters WHERE (display_name LIKE ? OR safe_display_name LIKE ?) "
               "AND id != ? LIMIT 20;"
            << like << like << character_id >> [&](int64_t id) { ids.push_back(id); };
    } else {
        gdb << "SELECT id FROM characters WHERE safe_display_name LIKE ? AND id != ? LIMIT 20;"
            << like << character_id >> [&](int64_t id) { ids.push_back(id); };
    }
    visibility::viewer_context viewer;
    viewer.character_id = character_id;
    viewer.is_adult = viewer_is_adult;
    for (int64_t id : ids) {
        auto identity = visibility::load_character_identity(id);
        if (!identity) continue;
        json projected = visibility::project_character_identity(viewer, *identity);
        json e = {{"character_id", id},
                  {"display_name", projected.value("display_name", "")},
                  {"portrait_url", projected.value("avatar_url", "")},
                  {"is_friend", are_friends(character_id, id)},
                  {"is_blocked", is_blocked_either(character_id, id)}};
        out.push_back(std::move(e));
    }
    return out;
}

// ── Translation ─────────────────────────────────────────────────────────────

// Shared translation path for viewing and push. Returns the display-language
// body for a message, or "" when none should be shown (same language, source
// disabled, empty text, or engine unavailable). Stores new baseline
// translations. Never uses the LLM engine (push must not block on it).
std::string ChatService::translate_for_display(int64_t viewer_character_id,
                                               const std::string& message_id,
                                               const std::string& text,
                                               const std::string& source_language,
                                               const std::string& display_language) {
    if (display_language.empty() || text.empty()) return "";
    if (!source_language.empty() && source_language == display_language) return "";
    if (!language_enabled(viewer_character_id, source_language)) return "";

    json existing = translation_for(message_id, display_language);
    if (!existing.is_null() && existing.contains("body") && existing["body"].is_string()) {
        return existing["body"].get<std::string>();
    }
    if (!baseline_engine_ || !baseline_engine_->available()) return "";

    translation_request request{message_id, text, source_language, display_language};
    auto translated = baseline_engine_->translate(request);
    if (!translated || !translated->ok) return "";

    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "INSERT OR REPLACE INTO message_translations (message_id, target_language, engine, "
           "model_version, body, detected_source_language, confidence, created_at, change_id) "
           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);"
        << message_id << display_language << baseline_engine_->name()
        << baseline_engine_->model_version() << translated->translated_text
        << (translated->detected_source_language.empty()
                ? std::optional<std::string>()
                : std::optional<std::string>(translated->detected_source_language))
        << translated->confidence << now_seconds() << change_id;
    return translated->translated_text;
}

json ChatService::request_translations(int64_t character_id,
                                       const std::vector<std::string>& message_ids,
                                       const std::string& viewer_lang) {
    auto& db = ChatDB::instance().db();
    // The interface language is the display language; default to the account's
    // when the client did not send one.
    std::string display_language =
        viewer_lang.empty() ? display_language_for_character(character_id) : viewer_lang;
    json translations = json::object();
    if (display_language.empty()) {
        json result;
        result["translations"] = translations;
        return result;
    }
    const bool viewer_is_adult = visibility::character_is_adult(character_id);

    for (const auto& message_id : message_ids) {
        std::string chat_id;
        std::string text;
        std::string source_language;
        std::string safe_message_id;
        bool found = false;
        db << "SELECT chat_id, COALESCE(body, ''), COALESCE(source_language, ''), "
              "COALESCE(safe_message_id, '') FROM messages WHERE id = ?;"
           << message_id >> [&](std::string c, std::string b, std::string s, std::string safe_id) {
                  chat_id = c;
                  text = b;
                  source_language = s;
                  safe_message_id = safe_id;
                  found = true;
              };
        if (!found || text.empty()) continue;
        if (!viewer_is_adult || !safe_message_id.empty()) continue;
        if (!can_view_chat(character_id, chat_id)) continue;
        if (source_language == display_language) continue;
        if (!language_enabled(character_id, source_language)) continue;

        json existing = translation_for(message_id, display_language);
        if (existing.is_null()) {
            std::string body = translate_for_display(character_id, message_id, text,
                                                     source_language, display_language);
            if (!body.empty()) existing = translation_for(message_id, display_language);
        }
        if (!existing.is_null()) translations[message_id] = existing;
    }

    json result;
    result["translations"] = translations;
    return result;
}

json ChatService::get_language_prefs(int64_t character_id) {
    auto& db = ChatDB::instance().db();
    json disabled = json::array();
    db << "SELECT language FROM chat_language_prefs WHERE character_id = ? AND enabled = 0;"
       << character_id >> [&](std::string language) { disabled.push_back(language); };
    json result;
    result["game_language"] = display_language_for_character(character_id);
    result["disabled_languages"] = disabled;
    result["baseline_engine"] =
        baseline_engine_ ? baseline_engine_->name() : std::string("none");
    result["baseline_available"] = baseline_engine_ && baseline_engine_->available();
    result["llm_available"] = llm_engine_ && llm_engine_->available();
    return result;
}

json ChatService::set_language_pref(int64_t character_id, const std::string& language, bool enabled) {
    if (language.empty()) throw chat_error("language_required");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "INSERT OR REPLACE INTO chat_language_prefs (character_id, language, enabled, change_id) "
           "VALUES (?, ?, ?, ?);"
       << character_id << language << (enabled ? 1 : 0) << change_id;
    json result;
    result["language"] = language;
    result["enabled"] = enabled;
    return result;
}

// ── Reactions + bookmarks ───────────────────────────────────────────────────

json ChatService::add_reaction(int64_t character_id, const std::string& message_id,
                               const std::string& reaction_id) {
    if (!config_ || !visibility::is_valid_safe_id(
                        *config_, visibility::safe_catalog_kind::reaction, reaction_id)) {
        throw chat_error("invalid_reaction");
    }
    if (!visibility::character_is_adult(character_id) &&
        !visibility::is_minor_safe(*config_, visibility::safe_catalog_kind::reaction,
                                   reaction_id)) {
        throw chat_error("reaction_not_minor_safe");
    }
    auto& db = ChatDB::instance().db();
    std::string chat_id;
    std::string safe_message_id;
    int64_t author = 0;
    db << "SELECT chat_id, COALESCE(author_character_id, 0), COALESCE(safe_message_id, '') "
          "FROM messages WHERE id = ?;"
       << message_id >> [&](std::string c, int64_t a, std::string safe_id) {
              chat_id = c;
              author = a;
              safe_message_id = safe_id;
          };
    if (chat_id.empty()) throw chat_error("message_not_found");
    if (!can_view_chat(character_id, chat_id)) throw chat_error("not_a_member");
    if (!visibility::character_is_adult(character_id) && safe_message_id.empty()) {
        throw chat_error("message_not_visible");
    }
    if (author > 0 && author != character_id && is_blocked_either(character_id, author)) {
        throw chat_error("blocked");
    }
    db << "INSERT OR IGNORE INTO message_reactions (message_id, character_id, emoji, created_at) "
          "VALUES (?, ?, ?, ?);"
       << message_id << character_id << reaction_id << now_seconds();
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE messages SET change_id = ? WHERE id = ?;" << change_id << message_id;
    json result;
    result["message_id"] = message_id;
    result["reactions"] = reactions_for(
        message_id, config_, visibility::character_is_adult(character_id));
    result["change_id"] = change_id;
    return result;
}

json ChatService::remove_reaction(int64_t character_id, const std::string& message_id,
                                  const std::string& reaction_id) {
    if (!config_ || !visibility::is_valid_safe_id(
                        *config_, visibility::safe_catalog_kind::reaction, reaction_id)) {
        throw chat_error("invalid_reaction");
    }
    auto& db = ChatDB::instance().db();
    std::string chat_id;
    db << "SELECT chat_id FROM messages WHERE id = ?;" << message_id
       >> [&](std::string v) { chat_id = v; };
    if (chat_id.empty()) throw chat_error("message_not_found");
    if (!can_view_chat(character_id, chat_id)) throw chat_error("not_a_member");
    db << "DELETE FROM message_reactions WHERE message_id = ? AND character_id = ? AND emoji = ?;"
       << message_id << character_id << reaction_id;
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE messages SET change_id = ? WHERE id = ?;" << change_id << message_id;
    json result;
    result["message_id"] = message_id;
    result["reactions"] = reactions_for(
        message_id, config_, visibility::character_is_adult(character_id));
    result["change_id"] = change_id;
    return result;
}

json ChatService::add_bookmark(int64_t character_id, const std::string& message_id) {
    auto& db = ChatDB::instance().db();
    std::string chat_id;
    std::string safe_message_id;
    db << "SELECT chat_id, COALESCE(safe_message_id, '') FROM messages WHERE id = ?;"
       << message_id >> [&](std::string c, std::string safe_id) {
              chat_id = c;
              safe_message_id = safe_id;
          };
    if (chat_id.empty()) throw chat_error("message_not_found");
    if (!can_view_chat(character_id, chat_id)) throw chat_error("not_a_member");
    if (!visibility::character_is_adult(character_id) && safe_message_id.empty()) {
        throw chat_error("message_not_visible");
    }
    db << "INSERT OR IGNORE INTO bookmarks (character_id, message_id, created_at) VALUES (?, ?, ?);"
       << character_id << message_id << now_seconds();
    json result;
    result["message_id"] = message_id;
    result["bookmarked"] = true;
    return result;
}

json ChatService::remove_bookmark(int64_t character_id, const std::string& message_id) {
    ChatDB::instance().db()
        << "DELETE FROM bookmarks WHERE character_id = ? AND message_id = ?;"
        << character_id << message_id;
    json result;
    result["message_id"] = message_id;
    result["bookmarked"] = false;
    return result;
}

json ChatService::get_bookmarks(int64_t character_id) {
    auto& db = ChatDB::instance().db();
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);
    json out = json::array();
    std::vector<MessageRow> rows;
    const std::string safe_filter = dir.viewer_adult
        ? ""
        : " AND (m.safe_message_id IS NOT NULL OR m.system_id IS NOT NULL) "
          "AND m.chat_id NOT IN (SELECT id FROM chats WHERE type = 'group') ";
    db << std::string("SELECT ") + k_message_columns +
              " FROM bookmarks b JOIN messages m ON m.id = b.message_id "
              "WHERE b.character_id = ?" + safe_filter +
              " ORDER BY b.created_at DESC LIMIT 200;"
       << character_id >> make_message_reader(rows);
    for (const auto& r : rows) {
        json message = message_to_json(r, dir, "", config_, dir.viewer_adult);
        if (!message.is_null()) out.push_back(std::move(message));
    }
    json result;
    result["messages"] = out;
    return result;
}

// ── Social graph ────────────────────────────────────────────────────────────

json ChatService::send_friend_request(int64_t character_id, int64_t target_character_id) {
    if (target_character_id == character_id) throw chat_error("cannot_friend_self");
    if (!character_exists(target_character_id)) throw chat_error("character_not_found");
    if (is_blocked_either(character_id, target_character_id)) throw chat_error("blocked");
    if (are_friends(character_id, target_character_id)) throw chat_error("already_friends");

    auto& db = ChatDB::instance().db();
    json cfg = chat_cfg(config_);
    int cap = cfg_int(cfg, "max_friend_requests_per_day", 20);
    int64_t since = now_seconds() - 86400;
    int64_t sent = 0;
    db << "SELECT COUNT(*) FROM friend_requests WHERE from_character_id = ? AND created_at >= ?;"
       << character_id << since >> sent;
    if (sent >= cap) throw chat_error("friend_request_limit");

    bool pending = false;
    db << "SELECT 1 FROM friend_requests WHERE from_character_id = ? AND to_character_id = ? LIMIT 1;"
       << character_id << target_character_id >> [&](int) { pending = true; };
    if (pending) throw chat_error("already_requested");

    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "INSERT OR REPLACE INTO friend_requests (from_character_id, to_character_id, created_at, change_id) "
          "VALUES (?, ?, ?, ?);"
       << character_id << target_character_id << now_seconds() << change_id;
    json result;
    result["target_character_id"] = target_character_id;
    result["change_id"] = change_id;
    return result;
}

json ChatService::accept_friend_request(int64_t character_id, int64_t target_character_id) {
    auto& db = ChatDB::instance().db();
    bool pending = false;
    db << "SELECT 1 FROM friend_requests WHERE from_character_id = ? AND to_character_id = ? LIMIT 1;"
       << target_character_id << character_id >> [&](int) { pending = true; };
    if (!pending) throw chat_error("no_pending_request");
    if (is_blocked_either(character_id, target_character_id)) throw chat_error("blocked");

    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "INSERT OR REPLACE INTO friends (character_id, friend_character_id, created_at, change_id) "
          "VALUES (?, ?, ?, ?);"
       << character_id << target_character_id << now_seconds() << change_id;
    db << "INSERT OR REPLACE INTO friends (character_id, friend_character_id, created_at, change_id) "
          "VALUES (?, ?, ?, ?);"
       << target_character_id << character_id << now_seconds() << change_id;
    db << "DELETE FROM friend_requests WHERE (from_character_id = ? AND to_character_id = ?) "
          "OR (from_character_id = ? AND to_character_id = ?);"
       << character_id << target_character_id << target_character_id << character_id;
    std::string chat_id = ensure_direct_chat(character_id, target_character_id);
    json result;
    result["friend_character_id"] = target_character_id;
    result["chat_id"] = chat_id;
    return result;
}

json ChatService::decline_friend_request(int64_t character_id, int64_t target_character_id) {
    ChatDB::instance().db()
        << "DELETE FROM friend_requests WHERE from_character_id = ? AND to_character_id = ?;"
        << target_character_id << character_id;
    json result;
    result["target_character_id"] = target_character_id;
    return result;
}

json ChatService::unfriend(int64_t character_id, int64_t target_character_id) {
    auto& db = ChatDB::instance().db();
    db << "DELETE FROM friends WHERE (character_id = ? AND friend_character_id = ?) "
          "OR (character_id = ? AND friend_character_id = ?);"
       << character_id << target_character_id << target_character_id << character_id;
    json result;
    result["target_character_id"] = target_character_id;
    return result;
}

json ChatService::block_user(int64_t character_id, int64_t target_character_id) {
    if (target_character_id == character_id) throw chat_error("cannot_block_self");
    if (!character_exists(target_character_id)) throw chat_error("character_not_found");
    auto& db = ChatDB::instance().db();
    bool existing = is_blocked_either(character_id, target_character_id);
    if (!existing) {
        int64_t change_id = ChatDB::instance().allocate_change_id();
        db << "INSERT OR REPLACE INTO blocks (blocker_character_id, blocked_character_id, created_at, change_id) "
              "VALUES (?, ?, ?, ?);"
           << character_id << target_character_id << now_seconds() << change_id;
        bump_relation_count(character_id, 1);
        bump_relation_count(target_character_id, 1);
    }
    db << "DELETE FROM friends WHERE (character_id = ? AND friend_character_id = ?) "
          "OR (character_id = ? AND friend_character_id = ?);"
       << character_id << target_character_id << target_character_id << character_id;
    db << "DELETE FROM friend_requests WHERE (from_character_id = ? AND to_character_id = ?) "
          "OR (from_character_id = ? AND to_character_id = ?);"
       << character_id << target_character_id << target_character_id << character_id;
    json result;
    result["target_character_id"] = target_character_id;
    result["blocked"] = true;
    return result;
}

json ChatService::unblock_user(int64_t character_id, int64_t target_character_id) {
    auto& db = ChatDB::instance().db();
    bool existing = false;
    db << "SELECT 1 FROM blocks WHERE blocker_character_id = ? AND blocked_character_id = ? LIMIT 1;"
       << character_id << target_character_id >> [&](int) { existing = true; };
    if (existing) {
        db << "DELETE FROM blocks WHERE blocker_character_id = ? AND blocked_character_id = ?;"
           << character_id << target_character_id;
        bump_relation_count(character_id, -1);
        bump_relation_count(target_character_id, -1);
    }
    json result;
    result["target_character_id"] = target_character_id;
    result["blocked"] = false;
    return result;
}

json ChatService::mute_character(int64_t character_id, int64_t target_character_id) {
    if (target_character_id == character_id) throw chat_error("cannot_mute_self");
    if (!character_exists(target_character_id)) throw chat_error("character_not_found");
    auto& db = ChatDB::instance().db();
    bool existing = is_person_muted(character_id, target_character_id);
    if (!existing) {
        int64_t change_id = ChatDB::instance().allocate_change_id();
        db << "INSERT OR REPLACE INTO character_mutes (character_id, muted_character_id, created_at, change_id) "
              "VALUES (?, ?, ?, ?);"
           << character_id << target_character_id << now_seconds() << change_id;
        bump_relation_count(character_id, 1);
        bump_relation_count(target_character_id, 1);
    }
    json result;
    result["target_character_id"] = target_character_id;
    result["muted"] = true;
    return result;
}

json ChatService::unmute_character(int64_t character_id, int64_t target_character_id) {
    auto& db = ChatDB::instance().db();
    bool existing = is_person_muted(character_id, target_character_id);
    if (existing) {
        db << "DELETE FROM character_mutes WHERE character_id = ? AND muted_character_id = ?;"
           << character_id << target_character_id;
        bump_relation_count(character_id, -1);
        bump_relation_count(target_character_id, -1);
    }
    json result;
    result["target_character_id"] = target_character_id;
    result["muted"] = false;
    return result;
}

json ChatService::get_friends(int64_t character_id) {
    auto& db = ChatDB::instance().db();
    CharacterDirectory dir(visibility::character_is_adult(character_id), character_id);
    std::vector<int64_t> ids;
    json friends = json::array();
    db << "SELECT friend_character_id, created_at FROM friends WHERE character_id = ? ORDER BY created_at DESC;"
       << character_id
       >> [&](int64_t fid, int64_t created_at) {
              ids.push_back(fid);
              friends.push_back({{"character_id", fid}, {"since", created_at}});
          };
    json incoming = json::array();
    db << "SELECT from_character_id, created_at FROM friend_requests WHERE to_character_id = ? ORDER BY created_at DESC;"
       << character_id
       >> [&](int64_t fid, int64_t created_at) {
              ids.push_back(fid);
              incoming.push_back({{"character_id", fid}, {"requested_at", created_at}});
          };
    json outgoing = json::array();
    db << "SELECT to_character_id, created_at FROM friend_requests WHERE from_character_id = ? ORDER BY created_at DESC;"
       << character_id
       >> [&](int64_t fid, int64_t created_at) {
              ids.push_back(fid);
              outgoing.push_back({{"character_id", fid}, {"requested_at", created_at}});
          };
    json blocked = json::array();
    db << "SELECT blocked_character_id FROM blocks WHERE blocker_character_id = ?;"
       << character_id
       >> [&](int64_t fid) {
              ids.push_back(fid);
              blocked.push_back(fid);
          };
    json muted = json::array();
    db << "SELECT muted_character_id FROM character_mutes WHERE character_id = ?;"
       << character_id
       >> [&](int64_t fid) {
              ids.push_back(fid);
              muted.push_back(fid);
          };

    json characters = json::object();
    for (int64_t id : ids) characters[std::to_string(id)] = dir.get(id);
    json result;
    result["friends"] = friends;
    result["incoming_requests"] = incoming;
    result["outgoing_requests"] = outgoing;
    result["blocked_character_ids"] = blocked;
    result["muted_character_ids"] = muted;
    result["characters"] = characters;
    return result;
}

json ChatService::get_friend_presence(int64_t character_id) {
    auto& db = ChatDB::instance().db();
    int64_t now = now_seconds();
    json friends = json::array();
    db << "SELECT friend_character_id FROM friends WHERE character_id = ?;" << character_id
       >> [&](int64_t friend_id) {
              json entry;
              entry["character_id"] = friend_id;
              entry["online"] = Presence::instance().is_online(friend_id, now);
              entry["typing"] = Presence::instance().typing_chats(friend_id, now);
              friends.push_back(entry);
          };
    json result;
    result["friends"] = friends;
    return result;
}

// ── Web Push ────────────────────────────────────────────────────────────────

json ChatService::get_push_config(int64_t character_id) {
    (void)character_id;
    json result;
    result["public_key"] = PushSender::instance().public_key();
    result["enabled"] = PushSender::instance().configured();
    result["key_id"] = PushSender::instance().key_id();
    return result;
}

json ChatService::subscribe_push(int64_t character_id, const json& body) {
    std::string endpoint = body.value("endpoint", std::string());
    std::string p256dh = body.value("p256dh", std::string());
    std::string auth = body.value("auth", std::string());
    if (endpoint.empty() || p256dh.empty() || auth.empty()) {
        throw chat_error("invalid_subscription");
    }
    int64_t account = account_for_character(character_id);
    if (account <= 0) throw chat_error("account_not_found");
    ChatDB::instance().db()
        << "INSERT INTO push_subscriptions (id, account_id, endpoint, p256dh, auth, vapid_key_id, "
           "created_at, last_success_at, fail_count) VALUES (?, ?, ?, ?, ?, ?, ?, NULL, 0) "
           "ON CONFLICT(account_id, endpoint) DO UPDATE SET p256dh = excluded.p256dh, "
           "auth = excluded.auth, vapid_key_id = excluded.vapid_key_id;"
        << generate_id("ps_") << account << endpoint << p256dh << auth
        << PushSender::instance().key_id() << now_seconds();
    json result;
    result["endpoint"] = endpoint;
    result["subscribed"] = true;
    return result;
}

json ChatService::unsubscribe_push(int64_t character_id, const std::string& endpoint) {
    int64_t account = account_for_character(character_id);
    ChatDB::instance().db()
        << "DELETE FROM push_subscriptions WHERE account_id = ? AND endpoint = ?;"
        << account << endpoint;
    json result;
    result["endpoint"] = endpoint;
    result["subscribed"] = false;
    return result;
}

json ChatService::set_push_preferences(int64_t character_id, const json& body) {
    int push_enabled = body.value("push_enabled", true) ? 1 : 0;
    int dm_from_friends = body.value("dm_from_friends", true) ? 1 : 0;
    int group_mentions = body.value("group_mentions", false) ? 1 : 0;
    ChatDB::instance().db()
        << "INSERT OR REPLACE INTO push_preferences (character_id, push_enabled, dm_from_friends, "
           "group_mentions) VALUES (?, ?, ?, ?);"
        << character_id << push_enabled << dm_from_friends << group_mentions;
    json result;
    result["push_enabled"] = push_enabled != 0;
    result["dm_from_friends"] = dm_from_friends != 0;
    result["group_mentions"] = group_mentions != 0;
    return result;
}

// Push bodies are sent in the original language. Per-character translation of
// push payloads is a TODO (the design wants stored-or-library translation, never
// LLM); the translation seam exists but the recipient's display language is not
// yet stored server-side. See server/docs/chat.md.
void ChatService::dispatch_push(const std::string& chat_id, int64_t author,
                                const std::string& message_id, const std::string& body,
                                const std::string& source_language,
                                const std::string& safe_message_id) {
    if (!PushSender::instance().configured()) return;
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta)) return;
    if (meta.type == "wildlands") return;

    auto& db = ChatDB::instance().db();
    struct Recipient {
        int64_t character_id;
        std::string notify_level;
    };
    std::vector<Recipient> recipients;
    db << "SELECT character_id, notify_level FROM chat_members "
          "WHERE chat_id = ? AND character_id != ? AND left_at IS NULL;"
       << chat_id << author
       >> [&](int64_t cid, std::string level) { recipients.push_back({cid, level}); };

    for (const auto& recipient : recipients) {
        if (recipient.notify_level == "none") continue;
        if (is_blocked_either(author, recipient.character_id)) continue;
        if (is_person_muted(recipient.character_id, author)) continue;
        const bool recipient_is_adult = visibility::character_is_adult(recipient.character_id);
        // Custom content is invisible to minors and must never notify them.
        if (safe_message_id.empty() && !recipient_is_adult) continue;

        int push_enabled = 1;
        int dm_from_friends = 1;
        int group_mentions = 0;
        db << "SELECT push_enabled, dm_from_friends, group_mentions FROM push_preferences "
              "WHERE character_id = ?;"
           << recipient.character_id
           >> [&](int64_t pe, int64_t dm, int64_t gm) {
                  push_enabled = static_cast<int>(pe);
                  dm_from_friends = static_cast<int>(dm);
                  group_mentions = static_cast<int>(gm);
              };
        if (!push_enabled) continue;

        bool eligible = false;
        if (meta.type == "direct") {
            if (dm_from_friends && are_friends(author, recipient.character_id)) eligible = true;
            if (recipient.notify_level == "all") eligible = true;
        } else if (group_mentions) {
            if ((!safe_message_id.empty() && recipient.notify_level == "all") ||
                (!body.empty() && body_mentions(body, recipient.character_id)) ||
                recipient.notify_level == "all") {
                eligible = true;
            }
        }
        if (!eligible) continue;

        int64_t account = account_for_character(recipient.character_id);
        if (account <= 0) continue;
        std::vector<push_subscription> subscriptions;
        db << "SELECT id, endpoint, p256dh, auth, COALESCE(vapid_key_id, '') "
              "FROM push_subscriptions WHERE account_id = ?;"
           << account
           >> [&](std::string id, std::string endpoint, std::string p256dh, std::string auth,
                  std::string key_id) {
                  subscriptions.push_back({id, endpoint, p256dh, auth, key_id});
              };
        if (subscriptions.empty()) continue;

        // Translate the notification exactly as the recipient would see the
        // message when viewing it: display language, respecting their per-source
        // language prefs, using stored-or-library translations only (never LLM).
        std::string display_language = display_language_for_character(recipient.character_id);
        std::string push_body = safe_message_id.empty()
            ? body.substr(0, 200)
            : (config_ ? visibility::safe_message_body(*config_, safe_message_id).substr(0, 200)
                       : std::string());
        if (safe_message_id.empty()) {
            std::string translated = translate_for_display(recipient.character_id, message_id, body,
                                                           source_language, display_language);
            if (!translated.empty()) push_body = translated.substr(0, 200);
        }

        push_message message;
        message.title = "Ravenest";
        message.body = push_body;
        message.chat_id = chat_id;
        message.character_id = std::to_string(recipient.character_id);
        message.url = "/#/activity/chat/" + chat_id;

        for (const auto& subscription : subscriptions) {
            auto outcome = PushSender::instance().send(subscription, message);
            if (outcome == PushSender::outcome::gone) {
                db << "DELETE FROM push_subscriptions WHERE id = ?;" << subscription.id;
            } else if (outcome == PushSender::outcome::ok) {
                db << "UPDATE push_subscriptions SET last_success_at = ? WHERE id = ?;"
                   << now_seconds() << subscription.id;
            } else if (outcome == PushSender::outcome::retryable) {
                db << "UPDATE push_subscriptions SET fail_count = fail_count + 1 WHERE id = ?;"
                   << subscription.id;
            }
        }
    }
}

// ── Groups ──────────────────────────────────────────────────────────────────

json ChatService::create_group(int64_t character_id, const std::string& name) {
    if (!visibility::can_create_group(character_id)) throw chat_error("group_adults_only");
    if (name.empty() || utf8_length(name) > 64) throw chat_error("invalid_group_name");
    auto& db = ChatDB::instance().db();
    json cfg = chat_cfg(config_);
    int max_groups = cfg_int(cfg, "max_groups_per_character", 50);
    int64_t count = 0;
    db << "SELECT COUNT(*) FROM chats c JOIN chat_members cm ON cm.chat_id = c.id "
          "WHERE c.type = 'group' AND cm.character_id = ? AND cm.left_at IS NULL AND c.archived_at IS NULL;"
       << character_id >> count;
    if (count >= max_groups) throw chat_error("group_limit");

    std::string id = generate_id("c_");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "INSERT INTO chats (id, type, name, name_changed_at, owner_character_id, direct_key, "
          "topic_id, source_kind, source_id, members_can_invite, created_at, archived_at, last_seq, "
          "change_id) VALUES (?, 'group', ?, ?, ?, NULL, NULL, NULL, NULL, 0, ?, NULL, 0, ?);"
       << id << name << now_seconds() << character_id << now_seconds() << change_id;
    add_member(id, character_id, "owner", ChatDB::instance().allocate_change_id());
    json result;
    result["chat_id"] = id;
    result["name"] = name;
    return result;
}

json ChatService::invite_to_group(int64_t character_id, const std::string& chat_id,
                                  int64_t target_character_id) {
    if (!visibility::character_is_adult(character_id)) throw chat_error("group_adults_only");
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta) || meta.type != "group") throw chat_error("not_a_group");
    auto& db = ChatDB::instance().db();
    std::string role = ChatDB::instance().member_role(chat_id, character_id);
    if (role.empty()) throw chat_error("not_a_member");
    bool can_invite = (role == "owner" || role == "moderator");
    if (!can_invite) {
        int64_t members_can = 0;
        db << "SELECT members_can_invite FROM chats WHERE id = ?;" << chat_id >> members_can;
        if (members_can == 0) throw chat_error("not_permitted");
    }
    if (target_character_id == character_id) throw chat_error("already_member");
    if (!character_exists(target_character_id)) throw chat_error("character_not_found");
    if (!visibility::character_is_adult(target_character_id)) throw chat_error("group_adults_only");
    if (is_blocked_either(character_id, target_character_id)) throw chat_error("blocked");
    if (ChatDB::instance().is_member(chat_id, target_character_id)) throw chat_error("already_member");
    bool banned = false;
    db << "SELECT 1 FROM chat_bans WHERE chat_id = ? AND character_id = ? LIMIT 1;"
       << chat_id << target_character_id >> [&](int) { banned = true; };
    if (banned) throw chat_error("banned_from_chat");

    json cfg = chat_cfg(config_);
    int max_pending = cfg_int(cfg, "max_pending_invites_per_group", 200);
    int64_t pending = 0;
    db << "SELECT COUNT(*) FROM chat_invites WHERE chat_id = ?;" << chat_id >> pending;
    if (pending >= max_pending) throw chat_error("invite_limit");

    int64_t change_id = ChatDB::instance().allocate_change_id();
    int64_t expires = now_seconds() + cfg_int(cfg, "invite_expiry_seconds", 604800);
    db << "INSERT OR REPLACE INTO chat_invites (chat_id, invited_character_id, "
          "invited_by_character_id, created_at, expires_at, change_id) VALUES (?, ?, ?, ?, ?, ?);"
       << chat_id << target_character_id << character_id << now_seconds() << expires << change_id;
    json result;
    result["chat_id"] = chat_id;
    result["invited_character_id"] = target_character_id;
    return result;
}

json ChatService::accept_group_invite(int64_t character_id, const std::string& chat_id) {
    if (!visibility::can_join_group(character_id)) throw chat_error("group_adults_only");
    auto& db = ChatDB::instance().db();
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta) || meta.type != "group") throw chat_error("not_a_group");
    bool invited = false;
    int64_t expires = 0;
    db << "SELECT expires_at FROM chat_invites WHERE chat_id = ? AND invited_character_id = ?;"
       << chat_id << character_id >> [&](int64_t e) {
              invited = true;
              expires = e;
          };
    if (!invited) throw chat_error("no_invite");
    if (expires < now_seconds()) {
        db << "DELETE FROM chat_invites WHERE chat_id = ? AND invited_character_id = ?;"
           << chat_id << character_id;
        throw chat_error("invite_expired");
    }
    bool banned = false;
    db << "SELECT 1 FROM chat_bans WHERE chat_id = ? AND character_id = ? LIMIT 1;"
       << chat_id << character_id >> [&](int) { banned = true; };
    if (banned) throw chat_error("banned_from_chat");

    int max_members = cfg_int(chat_cfg(config_), "group_max_members", 101);
    int64_t count = 0;
    db << "SELECT COUNT(*) FROM chat_members WHERE chat_id = ? AND left_at IS NULL;" << chat_id >> count;
    if (count >= max_members) throw chat_error("group_full");

    add_member(chat_id, character_id, "member", ChatDB::instance().allocate_change_id());
    db << "DELETE FROM chat_invites WHERE chat_id = ? AND invited_character_id = ?;"
       << chat_id << character_id;
    insert_system_message(chat_id, "member_joined", {{"character_id", character_id}});
    json result;
    result["chat_id"] = chat_id;
    return result;
}

json ChatService::decline_group_invite(int64_t character_id, const std::string& chat_id) {
    ChatDB::instance().db()
        << "DELETE FROM chat_invites WHERE chat_id = ? AND invited_character_id = ?;"
        << chat_id << character_id;
    json result;
    result["chat_id"] = chat_id;
    return result;
}

json ChatService::leave_chat(int64_t character_id, const std::string& chat_id) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta)) throw chat_error("chat_not_found");
    if (meta.type == "barony" || meta.type == "dukedom" || meta.type == "wildlands") {
        throw chat_error("cannot_leave_chat");
    }
    if (!ChatDB::instance().is_member(chat_id, character_id)) throw chat_error("not_a_member");
    auto& db = ChatDB::instance().db();

    if (meta.type == "group" && meta.owner == character_id) {
        int64_t successor = 0;
        db << "SELECT m.author_character_id FROM messages m "
              "JOIN chat_members cm ON cm.chat_id = m.chat_id AND cm.character_id = m.author_character_id "
              "AND cm.left_at IS NULL "
              "WHERE m.chat_id = ? AND m.author_character_id IS NOT NULL AND m.author_character_id != ? "
              "ORDER BY m.seq DESC LIMIT 1;"
           << chat_id << character_id >> successor;
        if (successor == 0) {
            db << "SELECT character_id FROM chat_members WHERE chat_id = ? AND character_id != ? "
                  "AND left_at IS NULL ORDER BY joined_at ASC LIMIT 1;"
               << chat_id << character_id >> successor;
        }
        if (successor > 0) {
            int64_t change_id = ChatDB::instance().allocate_change_id();
            db << "UPDATE chat_members SET role = 'moderator', change_id = ? "
                  "WHERE chat_id = ? AND character_id = ?;"
               << change_id << chat_id << character_id;
            db << "UPDATE chat_members SET role = 'owner', change_id = ? "
                  "WHERE chat_id = ? AND character_id = ?;"
               << change_id << chat_id << successor;
            db << "UPDATE chats SET owner_character_id = ?, change_id = ? WHERE id = ?;"
               << successor << change_id << chat_id;
            insert_system_message(chat_id, "owner_transferred", {{"character_id", successor}});
        } else {
            int64_t change_id = ChatDB::instance().allocate_change_id();
            db << "UPDATE chats SET archived_at = ?, change_id = ? WHERE id = ?;"
               << now_seconds() << change_id << chat_id;
        }
    }

    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE chat_members SET left_at = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << now_seconds() << change_id << chat_id << character_id;
    insert_system_message(chat_id, "member_left", {{"character_id", character_id}});
    json result;
    result["chat_id"] = chat_id;
    return result;
}

json ChatService::transfer_ownership(int64_t character_id, const std::string& chat_id,
                                     int64_t target_character_id) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta) || meta.type != "group") throw chat_error("not_a_group");
    if (meta.owner != character_id) throw chat_error("not_owner");
    if (!ChatDB::instance().is_member(chat_id, target_character_id)) throw chat_error("not_a_member");
    auto& db = ChatDB::instance().db();
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE chat_members SET role = 'moderator', change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << change_id << chat_id << character_id;
    db << "UPDATE chat_members SET role = 'owner', change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << change_id << chat_id << target_character_id;
    db << "UPDATE chats SET owner_character_id = ?, change_id = ? WHERE id = ?;"
       << target_character_id << change_id << chat_id;
    insert_system_message(chat_id, "owner_transferred", {{"character_id", target_character_id}});
    json result;
    result["chat_id"] = chat_id;
    result["owner_character_id"] = target_character_id;
    return result;
}

json ChatService::set_moderator(int64_t character_id, const std::string& chat_id,
                                int64_t target_character_id, bool moderator) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta) || meta.type != "group") throw chat_error("not_a_group");
    if (meta.owner != character_id) throw chat_error("not_owner");
    if (target_character_id == meta.owner) throw chat_error("cannot_change_owner");
    if (!ChatDB::instance().is_member(chat_id, target_character_id)) throw chat_error("not_a_member");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "UPDATE chat_members SET role = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << (moderator ? "moderator" : "member") << change_id << chat_id << target_character_id;
    json result;
    result["chat_id"] = chat_id;
    result["moderator"] = moderator;
    return result;
}

json ChatService::kick_member(int64_t character_id, const std::string& chat_id,
                              int64_t target_character_id) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta)) throw chat_error("chat_not_found");
    std::string role = ChatDB::instance().member_role(chat_id, character_id);
    if (role != "owner" && role != "moderator") throw chat_error("not_permitted");
    if (target_character_id == meta.owner) throw chat_error("cannot_kick_owner");
    if (!ChatDB::instance().is_member(chat_id, target_character_id)) throw chat_error("not_a_member");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "UPDATE chat_members SET left_at = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << now_seconds() << change_id << chat_id << target_character_id;
    insert_system_message(chat_id, "member_kicked", {{"character_id", target_character_id}});
    log_moderation(character_id, "kick", chat_id, target_character_id, json::object());
    json result;
    result["chat_id"] = chat_id;
    return result;
}

json ChatService::rename_group(int64_t character_id, const std::string& chat_id,
                               const std::string& name) {
    if (name.empty() || utf8_length(name) > 64) throw chat_error("invalid_group_name");
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta) || meta.type != "group") throw chat_error("not_a_group");
    if (meta.owner != character_id) throw chat_error("not_owner");
    auto& db = ChatDB::instance().db();
    int64_t name_changed_at = 0;
    db << "SELECT COALESCE(name_changed_at, 0) FROM chats WHERE id = ?;" << chat_id >> name_changed_at;
    int cooldown = cfg_int(chat_cfg(config_), "group_name_change_cooldown_seconds", 86400);
    if (name_changed_at > 0 && now_seconds() - name_changed_at < cooldown) {
        throw chat_error("name_change_cooldown");
    }
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "UPDATE chats SET name = ?, name_changed_at = ?, change_id = ? WHERE id = ?;"
       << name << now_seconds() << change_id << chat_id;
    json result;
    result["chat_id"] = chat_id;
    result["name"] = name;
    return result;
}

json ChatService::set_invite_policy(int64_t character_id, const std::string& chat_id,
                                    bool members_can_invite) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta) || meta.type != "group") throw chat_error("not_a_group");
    if (meta.owner != character_id) throw chat_error("not_owner");
    int64_t change_id = ChatDB::instance().allocate_change_id();
    ChatDB::instance().db()
        << "UPDATE chats SET members_can_invite = ?, change_id = ? WHERE id = ?;"
       << (members_can_invite ? 1 : 0) << change_id << chat_id;
    json result;
    result["chat_id"] = chat_id;
    result["members_can_invite"] = members_can_invite;
    return result;
}

// ── Moderation ──────────────────────────────────────────────────────────────

json ChatService::ban_from_chat(int64_t character_id, const std::string& chat_id,
                                int64_t target_character_id, const std::string& reason) {
    ChatMeta meta;
    if (!load_chat_meta(chat_id, meta)) throw chat_error("chat_not_found");
    std::string role = ChatDB::instance().member_role(chat_id, character_id);
    if (role != "owner" && role != "moderator") throw chat_error("not_permitted");
    if (target_character_id == meta.owner) throw chat_error("cannot_ban_owner");
    if (!character_exists(target_character_id)) throw chat_error("character_not_found");
    auto& db = ChatDB::instance().db();
    int64_t change_id = ChatDB::instance().allocate_change_id();
    db << "INSERT OR REPLACE INTO chat_bans (chat_id, character_id, banned_by_character_id, reason, "
          "created_at, change_id) VALUES (?, ?, ?, ?, ?, ?);"
       << chat_id << target_character_id << character_id << reason << now_seconds() << change_id;
    db << "UPDATE chat_members SET left_at = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
       << now_seconds() << change_id << chat_id << target_character_id;
    log_moderation(character_id, "ban", chat_id, target_character_id, {{"reason", reason}});
    json result;
    result["chat_id"] = chat_id;
    result["banned_character_id"] = target_character_id;
    return result;
}

json ChatService::unban_from_chat(int64_t character_id, const std::string& chat_id,
                                  int64_t target_character_id) {
    std::string role = ChatDB::instance().member_role(chat_id, character_id);
    if (role != "owner" && role != "moderator") throw chat_error("not_permitted");
    ChatDB::instance().db()
        << "DELETE FROM chat_bans WHERE chat_id = ? AND character_id = ?;"
        << chat_id << target_character_id;
    log_moderation(character_id, "unban", chat_id, target_character_id, json::object());
    json result;
    result["chat_id"] = chat_id;
    result["unbanned_character_id"] = target_character_id;
    return result;
}

json ChatService::get_moderation_log(int64_t character_id, const json& filter) {
    (void)character_id;
    auto& db = ChatDB::instance().db();
    json out = json::array();
    std::string chat_filter = filter.value("chat_id", std::string(""));
    if (!chat_filter.empty()) {
        db << "SELECT id, created_at, COALESCE(actor_character_id, 0), action, COALESCE(chat_id, ''), "
              "COALESCE(target_character_id, 0), COALESCE(payload, '') FROM moderation_log "
              "WHERE chat_id = ? ORDER BY id DESC LIMIT 200;"
           << chat_filter
           >> [&](int64_t id, int64_t created_at, int64_t actor, std::string action,
                  std::string cid, int64_t target, std::string payload) {
                  out.push_back({{"id", id},
                                 {"created_at", created_at},
                                 {"actor_character_id", actor},
                                 {"action", action},
                                 {"chat_id", cid},
                                 {"target_character_id", target},
                                 {"payload", json::parse(payload, nullptr, false)}});
              };
    } else {
        db << "SELECT id, created_at, COALESCE(actor_character_id, 0), action, COALESCE(chat_id, ''), "
              "COALESCE(target_character_id, 0), COALESCE(payload, '') FROM moderation_log "
              "ORDER BY id DESC LIMIT 200;"
           >> [&](int64_t id, int64_t created_at, int64_t actor, std::string action,
                  std::string cid, int64_t target, std::string payload) {
                  out.push_back({{"id", id},
                                 {"created_at", created_at},
                                 {"actor_character_id", actor},
                                 {"action", action},
                                 {"chat_id", cid},
                                 {"target_character_id", target},
                                 {"payload", json::parse(payload, nullptr, false)}});
              };
    }
    json result;
    result["entries"] = out;
    return result;
}

// ── Alliance materialization ────────────────────────────────────────────────

void ChatService::sync_barony_chat(int64_t barony_id) {
    auto& gdb = Database::getInstance().gameDB();
    auto& db = ChatDB::instance().db();

    std::string name;
    int64_t owner = 0;
    int64_t baron = 0;
    int64_t defunct_at = 0;
    bool found = false;
    gdb << "SELECT name, owner_character_id, COALESCE(baron_character_id, 0), defunct_at "
           "FROM baronies WHERE id = ?;"
        << barony_id >> [&](std::string n, int64_t o, int64_t b, int64_t d) {
               name = n;
               owner = o;
               baron = b;
               defunct_at = d;
               found = true;
           };
    if (!found) return;
    int64_t chat_owner = baron > 0 ? baron : owner;

    std::string chat_id;
    db << "SELECT id FROM chats WHERE source_kind = 'barony' AND source_id = ?;" << barony_id
       >> [&](std::string v) { chat_id = v; };

    // A tombstoned (defunct) barony's chat is archived and emptied — it must
    // never re-add the departed founder as chat owner.
    if (defunct_at > 0) {
        if (chat_id.empty()) return;
        int64_t change_id = ChatDB::instance().allocate_change_id();
        db << "UPDATE chats SET archived_at = ?, change_id = ? WHERE id = ?;"
           << now_seconds() << change_id << chat_id;
        db << "UPDATE chat_members SET left_at = ?, change_id = ? "
              "WHERE chat_id = ? AND left_at IS NULL;"
           << now_seconds() << change_id << chat_id;
        return;
    }

    if (chat_id.empty()) {
        chat_id = generate_id("c_");
        int64_t change_id = ChatDB::instance().allocate_change_id();
        db << "INSERT INTO chats (id, type, name, name_changed_at, owner_character_id, direct_key, "
              "topic_id, source_kind, source_id, members_can_invite, created_at, archived_at, "
              "last_seq, change_id) VALUES (?, 'barony', ?, NULL, ?, NULL, NULL, 'barony', ?, 0, ?, "
              "NULL, 0, ?);"
           << chat_id << name << chat_owner << barony_id << now_seconds() << change_id;
    } else {
        int64_t change_id = ChatDB::instance().allocate_change_id();
        db << "UPDATE chats SET name = ?, owner_character_id = ?, change_id = ? WHERE id = ?;"
           << name << chat_owner << change_id << chat_id;
    }

    std::set<int64_t> desired;
    desired.insert(chat_owner);
    gdb << "SELECT character_id FROM barony_members WHERE barony_id = ?;" << barony_id
        >> [&](int64_t cid) { desired.insert(cid); };

    add_member(chat_id, chat_owner, "owner", ChatDB::instance().allocate_change_id());
    for (int64_t cid : desired) {
        if (cid == chat_owner) continue;
        add_member(chat_id, cid, "member", ChatDB::instance().allocate_change_id());
    }

    std::vector<int64_t> current;
    db << "SELECT character_id FROM chat_members WHERE chat_id = ? AND left_at IS NULL;" << chat_id
       >> [&](int64_t cid) { current.push_back(cid); };
    for (int64_t cid : current) {
        if (desired.count(cid)) continue;
        int64_t change_id = ChatDB::instance().allocate_change_id();
        db << "UPDATE chat_members SET left_at = ?, change_id = ? WHERE chat_id = ? AND character_id = ?;"
           << now_seconds() << change_id << chat_id << cid;
    }
}

void ChatService::reconcile_alliances() {
    std::vector<int64_t> barony_ids;
    Database::getInstance().gameDB()
        << "SELECT id FROM baronies;" >> [&](int64_t id) { barony_ids.push_back(id); };
    for (int64_t id : barony_ids) {
        try {
            sync_barony_chat(id);
        } catch (const std::exception&) {
            // Reconciliation is best-effort; a single bad row must not abort startup.
        }
    }
}

// ── Admin identity lock ─────────────────────────────────────────────────────

namespace {
void record_identity_history(sqlite::database& gdb, int64_t character_id, const std::string& kind,
                             const std::string& value) {
    gdb << "INSERT INTO character_identity_history (character_id, kind, value, changed_at) "
           "VALUES (?, ?, ?, ?);"
        << character_id << kind << value << now_seconds();
}

std::string latest_identity_history(sqlite::database& gdb, int64_t character_id,
                                    const std::string& kind) {
    std::string value;
    gdb << "SELECT value FROM character_identity_history WHERE character_id = ? AND kind = ? "
           "ORDER BY changed_at DESC, id DESC LIMIT 1;"
        << character_id << kind >> value;
    return value;
}
}  // namespace

json ChatService::get_identity_history(int64_t admin_character_id, int64_t target_character_id) {
    (void)admin_character_id;
    auto& gdb = Database::getInstance().gameDB();
    json out = json::array();
    gdb << "SELECT id, kind, value, changed_at FROM character_identity_history "
           "WHERE character_id = ? ORDER BY changed_at DESC, id DESC LIMIT 200;"
        << target_character_id
        >> [&](int64_t id, std::string kind, std::string value, int64_t changed_at) {
               out.push_back({{"id", id},
                              {"kind", kind},
                              {"value", value},
                              {"changed_at", changed_at}});
           };
    json result;
    result["character_id"] = target_character_id;
    result["history"] = out;
    return result;
}

json ChatService::admin_lock_identity(int64_t admin_character_id, const json& body) {
    if (!body.contains("character_id")) throw chat_error("character_id_required");
    int64_t target = body["character_id"].get<int64_t>();
    if (!character_exists(target)) throw chat_error("character_not_found");

    auto& gdb = Database::getInstance().gameDB();
    std::string display_name;
    std::string safe_name;
    std::string avatar_safe;
    std::unique_ptr<std::string> avatar_custom;
    gdb << "SELECT display_name, safe_display_name, COALESCE(avatar_safe_id, ''), "
           "avatar_custom_id FROM characters WHERE id = ?;"
        << target >> [&](std::string dn, std::string sn, std::string as,
                         std::unique_ptr<std::string> ac) {
               display_name = dn;
               safe_name = sn;
               avatar_safe = as;
               avatar_custom = std::move(ac);
           };

    std::string revert_to = body.value("revert_to", std::string("safe"));
    bool revert_name = body.value("revert_name", false);
    bool revert_avatar = body.value("revert_avatar", false);
    bool lock_name = body.value("lock_name", false);
    bool lock_avatar = body.value("lock_avatar", false);

    if (revert_name) {
        std::string prior = latest_identity_history(gdb, target, "name");
        record_identity_history(gdb, target, "name", display_name);
        std::string new_name = (revert_to == "prior" && !prior.empty()) ? prior : safe_name;
        if (new_name.empty()) new_name = display_name;
        gdb << "UPDATE characters SET display_name = ? WHERE id = ?;" << new_name << target;
    }
    if (revert_avatar) {
        std::string prior = latest_identity_history(gdb, target, "avatar");
        record_identity_history(gdb, target, "avatar",
                                (avatar_custom && !avatar_custom->empty())
                                    ? std::string("custom")
                                    : avatar_safe);
        std::string new_avatar = (revert_to == "prior" && !prior.empty() && prior != "custom")
                                     ? prior
                                     : avatar_safe;
        gdb << "UPDATE characters SET avatar_safe_id = ?, avatar_custom_id = NULL, "
               "avatar_storage = 'local', avatar_changed_at = ? WHERE id = ?;"
            << new_avatar << now_seconds() << target;
    }
    if (lock_name || lock_avatar) {
        gdb << "UPDATE characters SET name_locked = ?, avatar_locked = ?, identity_locked_by = ?, "
               "identity_locked_at = ? WHERE id = ?;"
            << (lock_name ? 1 : 0) << (lock_avatar ? 1 : 0) << admin_character_id << now_seconds()
            << target;
    }

    log_moderation(admin_character_id, "identity_lock", "", target,
                   {{"lock_name", lock_name},
                    {"lock_avatar", lock_avatar},
                    {"revert_name", revert_name},
                    {"revert_avatar", revert_avatar},
                    {"revert_to", revert_to}});

    json result;
    result["character_id"] = target;
    result["locked"] = lock_name || lock_avatar;
    return result;
}

}  // namespace chat
