#include "chat/PushSender.hpp"

#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstring>
#include <cstdint>
#include <ctime>
#include <vector>

namespace chat {

namespace {

using bytes = std::vector<unsigned char>;

std::string b64url_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.resize(4 * ((len + 2) / 3));
    int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data,
                                  static_cast<int>(len));
    out.resize(written);
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

bytes b64url_decode(const std::string& input) {
    std::string padded = input;
    for (char& c : padded) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (padded.size() % 4 != 0) padded.push_back('=');
    bytes out(padded.size());
    int written = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(padded.data()),
                                  static_cast<int>(padded.size()));
    if (written < 0) return {};
    size_t padding = 0;
    if (!padded.empty() && padded.back() == '=') padding++;
    if (padded.size() >= 2 && padded[padded.size() - 2] == '=') padding++;
    out.resize(static_cast<size_t>(written) - padding);
    return out;
}

bytes hmac_sha256(const bytes& key, const bytes& data) {
    bytes out(32);
    unsigned int len = 32;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data.data(), data.size(),
         out.data(), &len);
    out.resize(len);
    return out;
}

bytes hkdf_expand(const bytes& prk, const bytes& info, size_t length) {
    bytes out;
    bytes block;
    unsigned char counter = 1;
    while (out.size() < length) {
        bytes input = block;
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter++);
        block = hmac_sha256(prk, input);
        out.insert(out.end(), block.begin(), block.end());
    }
    out.resize(length);
    return out;
}

EVP_PKEY* generate_p256() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) return nullptr;
    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0 ||
        EVP_PKEY_keygen(ctx, &key) <= 0) {
        key = nullptr;
    }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

bytes aes128gcm_encrypt(const bytes& key, const bytes& nonce, const bytes& plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    bytes out(plaintext.size() + 16);
    int len = 0;
    int total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()),
                                   nullptr) == 1;
    ok = ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1;
    if (ok) {
        ok = EVP_EncryptUpdate(ctx, out.data(), &len, plaintext.data(),
                               static_cast<int>(plaintext.size())) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, out.data() + total, &len) == 1;
        total += len;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + total) == 1;
        total += 16;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    out.resize(total);
    return out;
}

bytes ecdsa_der_to_raw(const unsigned char* der, size_t der_len) {
    const unsigned char* p = der;
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der_len));
    if (!sig) return {};
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);
    bytes raw(64);
    BN_bn2binpad(r, raw.data(), 32);
    BN_bn2binpad(s, raw.data() + 32, 32);
    ECDSA_SIG_free(sig);
    return raw;
}

std::string endpoint_origin(const std::string& endpoint) {
    size_t scheme = endpoint.find("://");
    if (scheme == std::string::npos) return endpoint;
    size_t path = endpoint.find('/', scheme + 3);
    if (path == std::string::npos) return endpoint;
    return endpoint.substr(0, path);
}

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

}  // namespace

void PushSender::configure(const nlohmann::json& push_config) {
    if (!push_config.is_object()) return;
    vapid_subject_ = push_config.value("sub", vapid_subject_);
    ttl_ = push_config.value("default_ttl", ttl_);
    const auto& vapid = push_config.value("vapid", nlohmann::json::object());
    if (!vapid.is_object()) return;
    current_key_id_ = vapid.value("current", std::string());
    const auto& keys = vapid.value("keys", nlohmann::json::object());
    if (keys.contains(current_key_id_)) {
        vapid_public_ = keys[current_key_id_].value("public", std::string());
        vapid_private_ = keys[current_key_id_].value("private", std::string());
    }
}

PushSender::outcome PushSender::send(const push_subscription& subscription,
                                     const push_message& message) {
    if (!configured()) return outcome::not_configured;

    bytes ua_public = b64url_decode(subscription.p256dh);
    bytes auth_secret = b64url_decode(subscription.auth);
    bytes vapid_private = b64url_decode(vapid_private_);
    if (ua_public.size() != 65 || auth_secret.empty() || vapid_private.size() != 32) {
        return outcome::retryable;
    }

    EVP_PKEY* as_key = generate_p256();
    if (!as_key) return outcome::retryable;
    bytes as_public(65);
    size_t as_public_len = as_public.size();
    if (EVP_PKEY_get_raw_public_key(as_key, as_public.data(), &as_public_len) != 1) {
        EVP_PKEY_free(as_key);
        return outcome::retryable;
    }

    EVP_PKEY* ua_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_EC, nullptr, ua_public.data(),
                                                   ua_public.size());
    if (!ua_key) {
        EVP_PKEY_free(as_key);
        return outcome::retryable;
    }
    EVP_PKEY_CTX* derive_ctx = EVP_PKEY_CTX_new(as_key, nullptr);
    bytes shared_secret;
    bool derive_ok = false;
    if (derive_ctx && EVP_PKEY_derive_init(derive_ctx) == 1 &&
        EVP_PKEY_derive_set_peer(derive_ctx, ua_key) == 1) {
        size_t secret_len = 0;
        if (EVP_PKEY_derive(derive_ctx, nullptr, &secret_len) == 1) {
            shared_secret.resize(secret_len);
            derive_ok = EVP_PKEY_derive(derive_ctx, shared_secret.data(), &secret_len) == 1;
            shared_secret.resize(secret_len);
        }
    }
    EVP_PKEY_CTX_free(derive_ctx);
    EVP_PKEY_free(ua_key);
    EVP_PKEY_free(as_key);
    if (!derive_ok) return outcome::retryable;

    // RFC 8291: IKM = HKDF-Expand(HKDF-Extract(auth_secret, shared_secret),
    //                             "WebPush: info\0" || ua_public || as_public, 32)
    bytes prk_key = hmac_sha256(auth_secret, shared_secret);
    bytes info = {'W', 'e', 'b', 'P', 'u', 's', 'h', ':', ' ', 'i', 'n', 'f', 'o', 0};
    info.insert(info.end(), ua_public.begin(), ua_public.end());
    info.insert(info.end(), as_public.begin(), as_public.end());
    bytes ikm = hkdf_expand(prk_key, info, 32);

    bytes salt(16);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) return outcome::retryable;
    bytes prk = hmac_sha256(salt, ikm);
    bytes cek_info = {'C', 'o', 'n', 't', 'e', 'n', 't', '-', 'E', 'n', 'c', 'o', 'd', 'i', 'n',
                      'g', ':', ' ', 'a', 'e', 's', '1', '2', '8', 'g', 'c', 'm', 0};
    bytes nonce_info = {'C', 'o', 'n', 't', 'e', 'n', 't', '-', 'E', 'n', 'c', 'o', 'd', 'i',
                        'n', 'g', ':', ' ', 'n', 'o', 'n', 'c', 'e', 0};
    bytes cek = hkdf_expand(prk, cek_info, 16);
    bytes nonce = hkdf_expand(prk, nonce_info, 12);

    bytes plaintext(message.body.begin(), message.body.end());
    plaintext.push_back(0x02);  // last-record padding delimiter
    bytes ciphertext = aes128gcm_encrypt(cek, nonce, plaintext);
    if (ciphertext.empty()) return outcome::retryable;

    bytes record;
    record.insert(record.end(), salt.begin(), salt.end());
    uint32_t record_size = 4096;
    record.push_back(static_cast<unsigned char>((record_size >> 24) & 0xFF));
    record.push_back(static_cast<unsigned char>((record_size >> 16) & 0xFF));
    record.push_back(static_cast<unsigned char>((record_size >> 8) & 0xFF));
    record.push_back(static_cast<unsigned char>(record_size & 0xFF));
    record.push_back(static_cast<unsigned char>(as_public.size()));
    record.insert(record.end(), as_public.begin(), as_public.end());
    record.insert(record.end(), ciphertext.begin(), ciphertext.end());

    // VAPID JWT (ES256).
    std::string header = b64url_encode(
        reinterpret_cast<const unsigned char*>(R"({"typ":"JWT","alg":"ES256"})"),
        std::strlen(R"({"typ":"JWT","alg":"ES256"})"));
    nlohmann::json payload = {{"aud", endpoint_origin(subscription.endpoint)},
                              {"exp", static_cast<int64_t>(std::time(nullptr)) + 43200},
                              {"sub", vapid_subject_}};
    std::string payload_str = payload.dump();
    std::string signing_input =
        header + "." + b64url_encode(reinterpret_cast<const unsigned char*>(payload_str.data()),
                                     payload_str.size());

    EVP_PKEY* vapid_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_EC, nullptr, vapid_private.data(),
                                                       vapid_private.size());
    if (!vapid_key) return outcome::retryable;
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    bytes der_sig;
    bool sign_ok = false;
    if (md && EVP_DigestSignInit(md, nullptr, EVP_sha256(), nullptr, vapid_key) == 1) {
        size_t sig_len = 0;
        if (EVP_DigestSign(md, nullptr, &sig_len,
                           reinterpret_cast<const unsigned char*>(signing_input.data()),
                           signing_input.size()) == 1) {
            der_sig.resize(sig_len);
            sign_ok = EVP_DigestSign(md, der_sig.data(), &sig_len,
                                     reinterpret_cast<const unsigned char*>(signing_input.data()),
                                     signing_input.size()) == 1;
            der_sig.resize(sig_len);
        }
    }
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(vapid_key);
    if (!sign_ok) return outcome::retryable;
    bytes raw_sig = ecdsa_der_to_raw(der_sig.data(), der_sig.size());
    if (raw_sig.size() != 64) return outcome::retryable;
    std::string jwt = signing_input + "." + b64url_encode(raw_sig.data(), raw_sig.size());
    std::string authorization =
        "vapid t=" + jwt + ",k=" + vapid_public_;

    CURL* curl = curl_easy_init();
    if (!curl) return outcome::retryable;
    std::string ttl_str = std::to_string(ttl_);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Encoding: aes128gcm");
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, ("TTL: " + ttl_str).c_str());
    headers = curl_slist_append(headers, ("Authorization: " + authorization).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, subscription.endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(record.data()));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(record.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode result = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) return outcome::retryable;
    if (http_code == 404 || http_code == 410) return outcome::gone;
    if (http_code >= 200 && http_code < 300) return outcome::ok;
    return outcome::retryable;
}

}  // namespace chat
