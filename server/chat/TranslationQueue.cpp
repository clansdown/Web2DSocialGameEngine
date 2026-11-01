#include "chat/TranslationQueue.hpp"

#include "GameConfigCache.hpp"
#include "chat/ChatService.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace chat {

namespace {

std::size_t read_limit(const nlohmann::json& config, const char* section,
                       const char* key, std::size_t fallback, std::size_t minimum,
                       std::size_t maximum) {
    if (!config.is_object() || !config.contains(section) || !config[section].is_object()) {
        return fallback;
    }
    const auto& block = config[section];
    if (!block.contains(key) || !block[key].is_number_integer()) return fallback;
    const long long value = block[key].get<long long>();
    if (value < static_cast<long long>(minimum)) return minimum;
    return std::min<std::size_t>(static_cast<std::size_t>(value), maximum);
}

translation_job_key key_for(const translation_job& job) {
    return translation_job_key{job.message_id, job.target_language, job.message_version};
}

// The sidecar echoes the request id. Numeric ids are unique inside one batch and
// avoid string concatenation collisions.
std::string request_id_for(std::size_t index) {
    return std::to_string(index);
}

}  // namespace

// ── Baseline queue ─────────────────────────────────────────────────────────

BaselineTranslationQueue& BaselineTranslationQueue::instance() {
    static BaselineTranslationQueue queue;
    return queue;
}

BaselineTranslationQueue::~BaselineTranslationQueue() {
    stop();
}

void BaselineTranslationQueue::start(GameConfigCache* config_cache) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    if (config_cache) {
        const nlohmann::json config = config_cache->getTranslationConfig();
        max_pending_ = read_limit(config, "queue", "max_pending", 256, 1, 65536);
        batch_size_ = read_limit(config, "queue", "batch_size", 8, 1, 256);
    }
    stopping_ = false;
    running_ = true;
    stats_.running = true;
    worker_ = std::thread(&BaselineTranslationQueue::worker_loop, this);
}

void BaselineTranslationQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        stopping_ = true;
        running_ = false;
        stats_.running = false;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool BaselineTranslationQueue::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool BaselineTranslationQueue::enqueue(translation_job job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) return false;
    if (job.message_id.empty() || job.target_language.empty() || job.text.empty()) return false;
    if (job.message_version <= 0) return false;

    const translation_job_key key = key_for(job);
    if (in_flight_keys_.find(key) != in_flight_keys_.end()) return true;
    if (pending_.size() >= max_pending_) {
        stats_.dropped++;
        return false;
    }
    in_flight_keys_.insert(key);
    pending_.push_back(std::move(job));
    stats_.pending = pending_.size();
    condition_.notify_one();
    return true;
}

translation_queue_stats BaselineTranslationQueue::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool BaselineTranslationQueue::drain_batch(std::vector<translation_job>& out) {
    if (pending_.empty()) return false;
    const std::size_t count = std::min(batch_size_, pending_.size());
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(std::move(pending_.front()));
        pending_.pop_front();
    }
    stats_.pending = pending_.size();
    stats_.in_flight = out.size();
    return true;
}

void BaselineTranslationQueue::worker_loop() {
    while (true) {
        std::vector<translation_job> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_ && pending_.empty()) return;
            drain_batch(batch);
        }

        std::vector<translation_request> requests;
        requests.reserve(batch.size());
        for (std::size_t i = 0; i < batch.size(); ++i) {
            translation_request request;
            request.id = request_id_for(i);
            request.text = batch[i].text;
            request.source = batch[i].source_language;
            request.target = batch[i].target_language;
            requests.push_back(std::move(request));
        }

        const std::vector<translation_result> results =
            ChatService::instance().translate_baseline_batch(requests);
        std::unordered_map<std::string, translation_result> by_id;
        for (const auto& result : results) by_id[result.id] = result;

        std::size_t completed = 0;
        std::size_t failed = 0;
        for (std::size_t i = 0; i < batch.size(); ++i) {
            const auto it = by_id.find(request_id_for(i));
            if (it != by_id.end() &&
                ChatService::instance().store_translation(batch[i], it->second)) {
                ++completed;
            } else {
                ++failed;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& job : batch) in_flight_keys_.erase(key_for(job));
            stats_.in_flight = 0;
            stats_.completed += completed;
            stats_.failed += failed;
        }
    }
}

// ── Premium queue (Phase 2 seam) ───────────────────────────────────────────

PremiumTranslationQueue& PremiumTranslationQueue::instance() {
    static PremiumTranslationQueue queue;
    return queue;
}

PremiumTranslationQueue::~PremiumTranslationQueue() {
    stop();
}

void PremiumTranslationQueue::start(GameConfigCache* config_cache) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    if (config_cache) {
        const nlohmann::json config = config_cache->getTranslationConfig();
        max_pending_ = read_limit(config, "queue", "max_pending", 256, 1, 65536);
        batch_size_ = read_limit(config, "queue", "batch_size", 8, 1, 256);
    }
    stopping_ = false;
    running_ = true;
    stats_.running = true;
    worker_ = std::thread(&PremiumTranslationQueue::worker_loop, this);
}

void PremiumTranslationQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        stopping_ = true;
        running_ = false;
        stats_.running = false;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool PremiumTranslationQueue::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool PremiumTranslationQueue::enqueue(translation_job job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) return false;
    if (job.message_id.empty() || job.text.empty() || job.message_version <= 0) return false;
    if (job.target_language.empty() && job.target_languages.empty()) return false;

    const translation_job_key key = key_for(job);
    if (in_flight_keys_.find(key) != in_flight_keys_.end()) return true;
    if (pending_.size() >= max_pending_) {
        stats_.dropped++;
        return false;
    }
    in_flight_keys_.insert(key);
    pending_.push_back(std::move(job));
    stats_.pending = pending_.size();
    condition_.notify_one();
    return true;
}

translation_queue_stats PremiumTranslationQueue::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool PremiumTranslationQueue::drain_batch(std::vector<translation_job>& out) {
    if (pending_.empty()) return false;
    const std::size_t count = std::min(batch_size_, pending_.size());
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(std::move(pending_.front()));
        pending_.pop_front();
    }
    stats_.pending = pending_.size();
    stats_.in_flight = out.size();
    return true;
}

void PremiumTranslationQueue::worker_loop() {
    while (true) {
        std::vector<translation_job> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_ && pending_.empty()) return;
            drain_batch(batch);
        }

        std::size_t completed = 0;
        std::size_t failed = 0;
        for (const auto& job : batch) {
            fanout_translation_request request;
            request.id = job.message_id;
            request.text = job.text;
            request.source_language = job.source_language;
            request.target_languages = job.target_languages;
            if (request.target_languages.empty() && !job.target_language.empty()) {
                request.target_languages.push_back(job.target_language);
            }

            auto result = ChatService::instance().translate_premium_fanout(request);
            if (!result) {
                ++failed;
                continue;
            }
            for (const auto& [language, translated] : result->by_target) {
                if (language.empty()) continue;
                translation_job single = job;
                single.target_language = language;
                single.target_languages.clear();
                single.tier = translation_tier::premium;
                if (ChatService::instance().store_translation(single, translated)) {
                    ++completed;
                } else {
                    ++failed;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& job : batch) in_flight_keys_.erase(key_for(job));
            stats_.in_flight = 0;
            stats_.completed += completed;
            stats_.failed += failed;
        }
    }
}

}  // namespace chat
