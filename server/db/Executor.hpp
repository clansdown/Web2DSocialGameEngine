#pragma once

#include "db/Database.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace db {

enum class post_result {
    accepted,
    queue_full,
    stopping
};

template <connection_like Connection>
struct db_job {
    std::function<void(Connection&)> run;
    std::function<void()> complete;
    std::function<void(error)> fail;
};

template <connection_like Connection>
class db_executor {
public:
    using factory = std::function<std::unique_ptr<Connection>()>;

    struct stats {
        std::size_t queued = 0;
        std::size_t active = 0;
        std::size_t completed = 0;
        std::size_t failed = 0;
        bool stopping = false;
    };

    db_executor(std::string name, std::size_t worker_count, std::size_t queue_capacity,
                factory connection_factory)
        : name_(std::move(name)),
          max_queue_(std::max<std::size_t>(1, queue_capacity)),
          factory_(std::move(connection_factory)) {
        const std::size_t count = std::max<std::size_t>(1, worker_count);
        workers_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~db_executor() { shutdown(); }

    db_executor(const db_executor&) = delete;
    db_executor& operator=(const db_executor&) = delete;

    post_result submit(db_job<Connection> job) {
        if (!job.run) return post_result::stopping;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return post_result::stopping;
            if (queue_.size() >= max_queue_) return post_result::queue_full;
            queue_.push_back(std::move(job));
        }
        condition_.notify_one();
        return post_result::accepted;
    }

    void shutdown() {
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

    stats snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats{queue_.size(), active_.load(), completed_.load(), failed_.load(), stopping_};
    }

    const std::string& name() const { return name_; }

private:
    void worker_loop() {
        std::unique_ptr<Connection> connection;
        for (;;) {
            db_job<Connection> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) {
                    if (stopping_) return;
                    continue;
                }
                job = std::move(queue_.front());
                queue_.pop_front();
            }

            if (!connection) {
                try {
                    connection = factory_();
                } catch (const std::exception& exception) {
                    ++failed_;
                    if (job.fail) {
                        job.fail(error{error_code::connection, {}, exception.what(), true});
                    }
                    continue;
                } catch (...) {
                    ++failed_;
                    if (job.fail) {
                        job.fail(error{error_code::connection, {}, "connection factory failed", true});
                    }
                    continue;
                }
            }

            ++active_;
            try {
                job.run(*connection);
                ++completed_;
                if (job.complete) job.complete();
            } catch (const std::exception& exception) {
                ++failed_;
                if (job.fail) {
                    job.fail(error{error_code::unknown, {}, exception.what(), false});
                }
            } catch (...) {
                ++failed_;
                if (job.fail) {
                    job.fail(error{error_code::unknown, {}, "unknown database job failure", false});
                }
            }
            --active_;
        }
    }

    std::string name_;
    std::size_t max_queue_ = 64;
    factory factory_;
    std::vector<std::thread> workers_;
    std::deque<db_job<Connection>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool stopping_ = false;
    std::atomic<std::size_t> active_{0};
    std::atomic<std::size_t> completed_{0};
    std::atomic<std::size_t> failed_{0};
};

template <connection_like Connection>
class db_registry {
public:
    using factory = typename db_executor<Connection>::factory;

    db_registry(factory game_factory, factory chat_factory,
                factory staff_factory, factory invites_factory,
                std::size_t workers_per_database = 4,
                std::size_t queue_capacity = 64)
        : game_("game", workers_per_database, queue_capacity, std::move(game_factory)),
          chat_("chat", workers_per_database, queue_capacity, std::move(chat_factory)),
          staff_("staff", workers_per_database, queue_capacity, std::move(staff_factory)),
          invites_("invites", workers_per_database, queue_capacity, std::move(invites_factory)) {}

    db_executor<Connection>& game() { return game_; }
    db_executor<Connection>& chat() { return chat_; }
    db_executor<Connection>& staff() { return staff_; }
    db_executor<Connection>& invites() { return invites_; }

    void shutdown() {
        game_.shutdown();
        chat_.shutdown();
        staff_.shutdown();
        invites_.shutdown();
    }

private:
    db_executor<Connection> game_;
    db_executor<Connection> chat_;
    db_executor<Connection> staff_;
    db_executor<Connection> invites_;
};

} // namespace db
