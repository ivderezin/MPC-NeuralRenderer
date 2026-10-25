// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
namespace framegen {
// Dedicated presentation thread with a high-resolution waitable timer. It never touches the player's UI
// thread; Present returns false when the renderer is busy and is retried after about a millisecond.
class PresentationThread {
public:
    using NextDeadline = std::function<bool(std::int64_t& deadline)>;
    using Present = std::function<bool()>;
    PresentationThread() = default;
    PresentationThread(const PresentationThread&) = delete;
    PresentationThread& operator=(const PresentationThread&) = delete;
    ~PresentationThread() { Stop(); }
    void Start(NextDeadline next, Present present);
    void Wake();
    // Joins the thread. Present must not block on a lock held by the caller; use a try-lock.
    void Stop();
    bool Running() const { return thread_.joinable(); }
    unsigned Retries() const { return retries_.load(std::memory_order_relaxed); }
    void ResetRetries() { retries_.store(0, std::memory_order_relaxed); }
private:
    void Run();
    std::thread thread_;
    HANDLE wake_ = nullptr, timer_ = nullptr;
    bool highResolution_ = false, periodSet_ = false;
    std::atomic<bool> quit_{false};
    std::atomic<unsigned> retries_{0};
    NextDeadline next_;
    Present present_;
};
}
