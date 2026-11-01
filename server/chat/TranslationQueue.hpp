#pragma once
// Background translation queues. The baseline queue performs the lazy MADLAD
// translation work so request handlers and push dispatch never block on the
// sidecar. The premium queue is the Phase 2 seam: it is defined and can be
// started, but the LLM engine is not configured in Phase 1.
//
// One long-lived worker thread per queue is deliberate: the PostgreSQL
// compatibility adapter pins one connection per std::thread::id for the process
// lifetime, so per-job threads would leak connections.
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "chat/TranslationEngine.hpp"

class GameConfigCache;

namespace chat {

enum class translation_tier {
    baseline,
    premium,
};

struct translation_job {
    std::string message_id;
    std::string chat_id;
    std::string text;
    std::string source_language;
    std::string target_language;                 // empty for fan-out jobs
    std::vector<std::string> target_languages;   // premium fan-out only
    int64_t message_version = 0;
    int64_t message_created_at = 0;
    translation_tier tier = translation_tier::baseline;
};

struct translation_job_key {
    std::string message_id;
    std::string target_language;
    int64_t message_version = 0;

    bool operator==(const translation_job_key&) const = default;
};

struct translation_job_key_hash {
    std::size_t operator()(const translation_job_key& key) const noexcept {
        std::size_t h = std::hash<std::string>{}(key.message_id);
        h ^= std::hash<std::string>{}(key.target_language) + 0x9e3779b97f4a7c15ULL +
             (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(key.message_version) + 0x9e3779b97f4a7c15ULL +
             (h << 6) + (h >> 2);
        return h;
    }
};

struct translation_queue_stats {
    std::size_t pending = 0;
    std::size_t in_flight = 0;
    std::size_t completed = 0;
    std::size_t failed = 0;
    std::size_t dropped = 0;
    bool running = false;
};

class BaselineTranslationQueue {
public:
    static BaselineTranslationQueue& instance();

    void start(GameConfigCache* config_cache);
    void stop();
    bool enqueue(translation_job job);
    bool available() const;
    translation_queue_stats stats() const;

private:
    BaselineTranslationQueue() = default;
    ~BaselineTranslationQueue();

    void worker_loop();
    bool drain_batch(std::vector<translation_job>& out);

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<translation_job> pending_;
    std::unordered_set<translation_job_key, translation_job_key_hash> in_flight_keys_;
    std::thread worker_;
    std::size_t max_pending_ = 256;
    std::size_t batch_size_ = 8;
    translation_queue_stats stats_;
    bool running_ = false;
    bool stopping_ = false;
};

class PremiumTranslationQueue {
public:
    static PremiumTranslationQueue& instance();

    // Phase 1 never calls start(); the method exists so Phase 2 can turn the
    // inert seam on without changing ChatService.
    void start(GameConfigCache* config_cache);
    void stop();
    bool enqueue(translation_job job);
    bool available() const;
    translation_queue_stats stats() const;

private:
    PremiumTranslationQueue() = default;
    ~PremiumTranslationQueue();

    void worker_loop();
    bool drain_batch(std::vector<translation_job>& out);

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<translation_job> pending_;
    std::unordered_set<translation_job_key, translation_job_key_hash> in_flight_keys_;
    std::thread worker_;
    std::size_t max_pending_ = 128;
    std::size_t batch_size_ = 4;
    translation_queue_stats stats_;
    bool running_ = false;
    bool stopping_ = false;
};

}  // namespace chat
