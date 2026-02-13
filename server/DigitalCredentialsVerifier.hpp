#ifndef DIGITAL_CREDENTIALS_VERIFIER_HPP
#define DIGITAL_CREDENTIALS_VERIFIER_HPP

#include <string>
#include <nlohmann/json.hpp>

class DigitalCredentialsVerifier {
public:
    static DigitalCredentialsVerifier& getInstance();

    struct VerificationResult {
        bool success;
        bool is_adult;
        std::string error_message;
    };

    VerificationResult verifyDigitalCredential(
        const std::string& protocol,
        const nlohmann::json& credential_data
    );

    void setVerifierServiceUrl(const std::string& url);
    void setTimeout(int timeout_ms);

private:
    DigitalCredentialsVerifier();
    ~DigitalCredentialsVerifier();

    VerificationResult contactVerifierService(
        const std::string& protocol,
        const nlohmann::json& credential_data
    );

    VerificationResult parseVerifierResponse(
        const std::string& response_body
    );

    std::string verifier_service_url;
    int timeout_ms;

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s);
};

#endif // DIGITAL_CREDENTIALS_VERIFIER_HPP
