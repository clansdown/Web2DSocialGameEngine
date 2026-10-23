#pragma once
// Web Push delivery: VAPID (ES256 JWT) + RFC 8291 aes128gcm payload encryption,
// POSTed to the subscription endpoint via libcurl. Subscriptions are keyed on
// account; the payload names the addressed character.
#include <nlohmann/json.hpp>
#include <string>

namespace chat {

struct push_subscription {
    std::string id;
    std::string endpoint;
    std::string p256dh;
    std::string auth;
    std::string vapid_key_id;
};

struct push_message {
    std::string title;
    std::string body;
    std::string chat_id;
    std::string character_id;
    std::string url;
};

class PushSender {
public:
    static PushSender& instance() {
        static PushSender inst;
        return inst;
    }

    // (Re)loads the VAPID keyring from game/config/push.json.
    void configure(const nlohmann::json& push_config);

    bool configured() const { return !vapid_public_.empty() && !vapid_private_.empty(); }
    std::string public_key() const { return vapid_public_; }
    std::string key_id() const { return current_key_id_; }

    // Delivery outcome.
    enum class outcome { ok, retryable, gone, not_configured };

    // Encrypts + delivers one notification. `gone` means the subscription is
    // dead (404/410) and should be deleted.
    outcome send(const push_subscription& subscription, const push_message& message);

private:
    PushSender() = default;

    std::string vapid_public_;
    std::string vapid_private_;
    std::string vapid_subject_ = "mailto:admin@example.com";
    std::string current_key_id_;
    int ttl_ = 2419200;
};

}  // namespace chat
