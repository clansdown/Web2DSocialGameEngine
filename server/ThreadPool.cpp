#include "ThreadPool.hpp"

#include <algorithm>
#include <utility>

thread_pool::thread_pool(std::size_t thread_count, std::size_t max_queue)
    : max_queue_(std::max<std::size_t>(1, max_queue)) {
    const std::size_t count = std::max<std::size_t>(1, thread_count);
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

thread_pool::~thread_pool() {
    shutdown();
}

bool thread_pool::post(std::function<void()> task) {
    if (!task) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || tasks_.size() >= max_queue_) return false;
        tasks_.push_back(std::move(task));
    }
    condition_.notify_one();
    return true;
}

void thread_pool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ && workers_.empty()) return;
        stopping_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

std::size_t thread_pool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void thread_pool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) {
                if (stopping_) return;
                continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try {
            task();
        } catch (...) {
            // Individual tasks must never terminate the server. The task itself
            // is responsible for reporting failures through its completion.
        }
    }
}
