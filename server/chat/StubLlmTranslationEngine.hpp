#pragma once
#include "chat/TranslationEngine.hpp"

namespace chat {

// Premium LLM translation seam (monthly-pass holders). Deliberately inert:
// the real backend is a deferred design. It is wired through the same
// TranslationEngine contract so swapping in a real engine is additive. It
// accepts a target-language SET and conversation context, as the eventual
// engine will need to batch the top chat languages.
class StubLlmTranslationEngine : public LlmTranslationEngine {
public:
    bool available() const override { return false; }

    std::vector<translation_result> translate_batch(
        const std::vector<translation_request>& requests, const llm_context& context) override {
        (void)requests;
        (void)context;
        return {};
    }
};

}  // namespace chat
