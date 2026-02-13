#include "DigitalCredentialsVerifier.hpp"
#include <curl/curl.h>
#include <iostream>
#include <cstring>

const std::string DEFAULT_VERIFIER_URL = "http://localhost:2291/verifier/dcGetData";
const int DEFAULT_TIMEOUT_MS = 30000;

DigitalCredentialsVerifier::DigitalCredentialsVerifier()
    : verifier_service_url(DEFAULT_VERIFIER_URL)
    , timeout_ms(DEFAULT_TIMEOUT_MS)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

DigitalCredentialsVerifier::~DigitalCredentialsVerifier() {
    curl_global_cleanup();
}

DigitalCredentialsVerifier& DigitalCredentialsVerifier::getInstance() {
    static DigitalCredentialsVerifier instance;
    return instance;
}

void DigitalCredentialsVerifier::setVerifierServiceUrl(const std::string& url) {
    verifier_service_url = url;
}

void DigitalCredentialsVerifier::setTimeout(int timeout) {
    timeout_ms = timeout;
}

size_t DigitalCredentialsVerifier::writeCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        return 0;
    }
    return newLength;
}

DigitalCredentialsVerifier::VerificationResult
DigitalCredentialsVerifier::verifyDigitalCredential(
    const std::string& protocol,
    const nlohmann::json& credential_data)
{
    VerificationResult result;
    result.success = false;
    result.is_adult = false;

    return contactVerifierService(protocol, credential_data);
}

DigitalCredentialsVerifier::VerificationResult
DigitalCredentialsVerifier::contactVerifierService(
    const std::string& protocol,
    const nlohmann::json& credential_data)
{
    VerificationResult result;
    result.success = false;
    result.is_adult = false;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error_message = "Failed to initialize CURL";
        return result;
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    nlohmann::json payload;
    payload["credentialProtocol"] = protocol;
    
    if (credential_data.is_string()) {
        payload["credentialResponse"] = credential_data.get<std::string>();
    } else {
        payload["credentialResponse"] = credential_data;
    }

    std::string json_body = payload.dump();
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, verifier_service_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        result.error_message = "Failed to connect to verifier service: " + std::string(curl_easy_strerror(res));
        return result;
    }

    if (http_code != 200) {
        result.error_message = "Verifier service returned HTTP " + std::to_string(http_code);
        return result;
    }

    return parseVerifierResponse(response_data);
}

DigitalCredentialsVerifier::VerificationResult
DigitalCredentialsVerifier::parseVerifierResponse(const std::string& response_body)
{
    VerificationResult result;
    result.success = false;
    result.is_adult = false;

    try {
        auto response = nlohmann::json::parse(response_body);

        if (!response.contains("pages") || !response["pages"].is_array()) {
            result.error_message = "Invalid verifier service response: missing pages";
            return result;
        }

        auto pages = response["pages"];

        for (const auto& page : pages) {
            if (!page.contains("lines") || !page["lines"].is_array()) {
                continue;
            }

            for (const auto& line : page["lines"]) {
                if (!line.contains("key") || !line.contains("value")) {
                    continue;
                }

                std::string key = line["key"];
                
                if (key.find("age") != std::string::npos || 
                    key.find("over_18") != std::string::npos ||
                    key.find("equal_or_over") != std::string::npos) {
                    
                    if (line["value"].is_boolean()) {
                        bool is_adult = line["value"].get<bool>();
                        result.success = true;
                        result.is_adult = is_adult;
                        result.error_message = "";
                        return result;
                    } else if (line["value"].is_string()) {
                        std::string value = line["value"].get<std::string>();
                        if (value == "true" || value == "yes") {
                            result.success = true;
                            result.is_adult = true;
                            result.error_message = "";
                            return result;
                        }
                    }
                }
            }
        }

        result.error_message = "Age claim not found in credential response";

    } catch (const nlohmann::json::parse_error& e) {
        result.error_message = "Failed to parse verifier response: " + std::string(e.what());
    } catch (const std::exception& e) {
        result.error_message = std::string("Verification error: ") + e.what();
    }

    return result;
}
