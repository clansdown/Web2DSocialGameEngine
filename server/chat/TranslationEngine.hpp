#pragma once
// Translation engine seam. The baseline engine talks to an internal localhost
// sidecar (MADLAD-400 via CTranslate2 + Lingua detection); the premium LLM
// engine is stubbed. ChatService stores results per
// (message_id, target_language, engine, model_version). See
// server/docs/chat.md and the plan §8.
#include <string>
#include <vector>
#include <optional>
#include <utility>

namespace chat {

struct translation_request {
    std::string id;
    std::string text;
    std::string source;   // may be empty (detect)
    std::string target;
};

struct translation_result {
    std::string id;
    std::string translated_text;
    std::string detected_source_language;
    double confidence = 0.0;
    bool ok = false;
};

// Context handed to the (future) LLM engine so it can disambiguate with the
// surrounding conversation. The stub ignores it; the seam is what matters.
struct llm_context {
    std::vector<std::string> target_languages;
    std::vector<std::string> preceding_texts;
};

class TranslationEngine {
public:
    virtual ~TranslationEngine() = default;
    virtual std::string name() const = 0;
    virtual std::string model_version() const = 0;
    virtual bool available() const = 0;
    virtual std::optional<translation_result> translate(const translation_request& request) = 0;
    virtual std::optional<std::pair<std::string, double>> detect(const std::string& text) = 0;

    std::vector<translation_result> translate_batch(
        const std::vector<translation_request>& requests) {
        std::vector<translation_result> out;
        for (const auto& request : requests) {
            auto result = translate(request);
            if (result) out.push_back(*result);
        }
        return out;
    }
};

class LlmTranslationEngine {
public:
    virtual ~LlmTranslationEngine() = default;
    virtual bool available() const = 0;
    virtual std::vector<translation_result> translate_batch(
        const std::vector<translation_request>& requests, const llm_context& context) = 0;
};

}  // namespace chat
