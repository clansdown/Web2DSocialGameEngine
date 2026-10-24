#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Minimal bounded worker pool used by avatar uploads and cleanup. Tasks are
// drained on shutdown; post() fails once shutdown begins or the queue is full.
class thread_pool {
public:
    thread_pool(std::size_t thread_count, std::size_t max_queue);
    ~thread_pool();

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    bool post(std::function<void()> task);
    void shutdown();
    std::size_t pending() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t max_queue_ = 0;
    bool stopping_ = false;
};
