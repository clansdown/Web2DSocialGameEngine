#include "chat/MadladTranslationEngine.hpp"

#include <curl/curl.h>

namespace chat {

namespace {
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}
}  // namespace

MadladTranslationEngine::MadladTranslationEngine(std::string sidecar_url, int timeout_ms,
                                                 bool enabled)
    : sidecar_url_(std::move(sidecar_url)), timeout_ms_(timeout_ms), enabled_(enabled) {}

std::optional<nlohmann::json> MadladTranslationEngine::post(const std::string& path,
                                                            const nlohmann::json& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string url = sidecar_url_ + path;
    std::string payload = body.dump();
    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms_));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout_ms_));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode result = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || http_code < 200 || http_code >= 300) return std::nullopt;
    try {
        return nlohmann::json::parse(response);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<translation_result> MadladTranslationEngine::translate(
    const translation_request& request) {
    if (!enabled_ || request.text.empty() || request.target.empty()) return std::nullopt;

    nlohmann::json items = nlohmann::json::array();
    items.push_back({{"id", request.id},
                     {"text", request.text},
                     {"source", request.source},
                     {"target", request.target}});
    auto response = post("/translate", {{"items", items}});
    if (!response || !response->contains("results") || !(*response)["results"].is_array() ||
        (*response)["results"].empty()) {
        return std::nullopt;
    }
    const auto& row = (*response)["results"][0];
    translation_result out;
    out.id = row.value("id", request.id);
    out.translated_text = row.value("translated_text", std::string());
    out.detected_source_language = row.value("detected_source_language", std::string());
    out.confidence = row.value("confidence", 0.0);
    out.ok = !out.translated_text.empty();
    if (!out.ok) return std::nullopt;
    return out;
}

std::vector<translation_result> MadladTranslationEngine::translate_many(
    const std::vector<translation_request>& requests) {
    std::vector<translation_result> out;
    if (!enabled_ || requests.empty()) return out;

    nlohmann::json items = nlohmann::json::array();
    for (const auto& request : requests) {
        if (request.text.empty() || request.target.empty()) continue;
        items.push_back({{"id", request.id},
                         {"text", request.text},
                         {"source", request.source},
                         {"target", request.target}});
    }
    if (items.empty()) return out;

    auto response = post("/translate", {{"items", items}});
    if (!response || !response->contains("results") || !(*response)["results"].is_array()) {
        return out;
    }
    for (const auto& row : (*response)["results"]) {
        if (!row.is_object()) continue;
        translation_result result;
        result.id = row.value("id", std::string());
        result.translated_text = row.value("translated_text", std::string());
        result.detected_source_language =
            row.value("detected_source_language", std::string());
        result.confidence = row.value("confidence", 0.0);
        result.ok = !result.translated_text.empty();
        if (result.ok) out.push_back(std::move(result));
    }
    return out;
}

std::optional<std::pair<std::string, double>> MadladTranslationEngine::detect(
    const std::string& text) {
    if (!enabled_ || text.empty()) return std::nullopt;
    auto response = post("/detect", {{"text", text}});
    if (!response) return std::nullopt;
    std::string language = response->value("language", std::string());
    if (language.empty()) return std::nullopt;
    return std::make_pair(language, response->value("confidence", 0.0));
}

}  // namespace chat
