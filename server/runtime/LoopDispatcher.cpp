#include "runtime/LoopDispatcher.hpp"

#include <uWebSockets/Loop.h>

#include <utility>

loop_dispatcher::loop_dispatcher(uWS::Loop* loop)
    : loop_(loop), accepting_(loop != nullptr) {}

loop_dispatcher::~loop_dispatcher() = default;

bool loop_dispatcher::post(std::function<void()> fn) {
    if (!fn) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_ || loop_ == nullptr) return false;
    loop_->defer(std::move(fn));
    return true;
}

void loop_dispatcher::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    loop_ = nullptr;
}
