// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresentationThread.h"
#include <mmsystem.h>
#include <algorithm>
#include <stdexcept>
#pragma comment(lib, "winmm.lib")
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
namespace framegen {
void PresentationThread::Start(NextDeadline next, Present present) {
    if (Running())
        return;
    next_ = std::move(next);
    present_ = std::move(present);
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    highResolution_ = timer_ != nullptr;
    if (!timer_)
        timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!wake_ || !timer_) {
        Stop();
        throw std::runtime_error("Create MFG presentation thread objects failed");
    }
    // Without a high-resolution timer, waitable timers follow the system tick.
    if (!highResolution_)
        periodSet_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    quit_ = false;
    thread_ = std::thread(&PresentationThread::Run, this);
}
void PresentationThread::Wake() {
    if (wake_)
        SetEvent(wake_);
}
void PresentationThread::Stop() {
    quit_ = true;
    Wake();
    if (thread_.joinable())
        thread_.join();
    if (timer_) {
        CloseHandle(timer_);
        timer_ = nullptr;
    }
    if (wake_) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
    if (periodSet_) {
        timeEndPeriod(1);
        periodSet_ = false;
    }
}
void PresentationThread::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    while (!quit_) {
        std::int64_t deadline = 0;
        bool scheduled = false;
        try {
            scheduled = next_(deadline);
        } catch (...) {
        }
        if (!scheduled) {
            WaitForSingleObject(wake_, INFINITE);
            continue;
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (deadline > now.QuadPart) {
            LARGE_INTEGER due{};
            due.QuadPart = -(std::max)(std::int64_t{1}, (deadline - now.QuadPart) * 10'000'000 / frequency.QuadPart);
            SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
            HANDLE handles[] = {wake_, timer_};
            WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            continue; // re-read the schedule: it may have changed, or the timer fired slightly early
        }
        bool presented = true;
        try {
            presented = present_();
        } catch (...) {
        }
        if (!presented) {
            retries_.fetch_add(1, std::memory_order_relaxed);
            WaitForSingleObject(wake_, 1);
        }
    }
}
}
