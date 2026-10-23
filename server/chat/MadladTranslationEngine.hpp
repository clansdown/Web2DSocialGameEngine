#pragma once
#include "chat/TranslationEngine.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

namespace chat {

// Baseline free translation: calls the internal localhost sidecar, which runs
// MADLAD-400-3B (Apache-2.0) via CTranslate2 with Lingua language detection.
// The sidecar is never exposed through nginx. If it is disabled or unreachable
// the engine reports unavailable and the game degrades to the original text.
class MadladTranslationEngine : public TranslationEngine {
public:
    MadladTranslationEngine(std::string sidecar_url, int timeout_ms, bool enabled);

    std::string name() const override { return "madlad400-3b"; }
    std::string model_version() const override { return "madlad400-3b-ct2-int8"; }
    bool available() const override { return enabled_; }

    std::optional<translation_result> translate(const translation_request& request) override;
    std::optional<std::pair<std::string, double>> detect(const std::string& text) override;

private:
    std::optional<nlohmann::json> post(const std::string& path, const nlohmann::json& body);

    std::string sidecar_url_;
    int timeout_ms_;
    bool enabled_;
    std::mutex mutex_;
};

}  // namespace chat
