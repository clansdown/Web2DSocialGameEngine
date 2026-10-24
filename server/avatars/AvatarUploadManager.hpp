#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "ThreadPool.hpp"
#include "avatars/AvatarStore.hpp"

struct avatar_upload_task {
    int character_id = 0;
    int64_t user_id = 0;
    std::string format;
    std::string image_base64;
    std::string old_key;
    avatar_storage_kind old_storage = avatar_storage_kind::local;
    int64_t started_changed_at = 0;
};

struct avatar_upload_result {
    bool ok = false;
    std::string error;
    std::string detail;
    std::string key;
    std::string public_url;
    int character_id = 0;
};

// Coordinates asynchronous avatar writes. try_begin()/finish() run on the uWS
// loop thread; submit() enqueues work on the bounded upload pool. The completion
// callback is called on a worker thread and must defer before touching uWS or
// SQLite.
class avatar_upload_manager {
public:
    avatar_upload_manager(avatar_store& store,
                          thread_pool& upload_pool,
                          thread_pool& maintenance_pool);

    bool try_begin(int character_id);
    void finish(int character_id);

    bool submit(avatar_upload_task task,
                std::function<void(avatar_upload_result)> completion);

    void shutdown();

private:
    avatar_store& store_;
    thread_pool& upload_pool_;
    thread_pool& maintenance_pool_;
    std::unordered_set<int> in_flight_;
};
