#pragma once

#include <functional>
#include <memory>
#include <mutex>

namespace uWS { struct Loop; }

// The only component allowed to call uWS::Loop::defer. Worker threads post
// completions here; the loop executes them on the uWS thread.
class loop_dispatcher {
public:
    explicit loop_dispatcher(uWS::Loop* loop);
    ~loop_dispatcher();

    loop_dispatcher(const loop_dispatcher&) = delete;
    loop_dispatcher& operator=(const loop_dispatcher&) = delete;

    // Thread-safe. Returns false once shutdown has closed the gate.
    bool post(std::function<void()> fn);

    // Loop thread only, before the uWS App/loop is destroyed.
    void shutdown();

private:
    std::mutex mutex_;
    uWS::Loop* loop_ = nullptr;
    bool accepting_ = false;
};
