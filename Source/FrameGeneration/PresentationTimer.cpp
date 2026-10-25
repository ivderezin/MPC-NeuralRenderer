// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresentationTimer.h"
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <memory>
#include <new>
#pragma comment(lib,"winmm.lib")
namespace framegen {
void CALLBACK PresentationTimer::Callback(void* context, BOOLEAN) {
    PostMessageW(*static_cast<HWND*>(context),PresentMessage(),0,0);
}
namespace {
struct RetryContext { HWND window; HMODULE module; };
std::atomic<unsigned> retries{0};
}
unsigned PresentationTimer::RetryCount() { return retries.load(std::memory_order_relaxed); }
void CALLBACK PresentationTimer::RetryCallback(PTP_CALLBACK_INSTANCE instance, void* context) {
    std::unique_ptr<RetryContext> retry(static_cast<RetryContext*>(context));
    FreeLibraryWhenCallbackReturns(instance,retry->module);
    Sleep(1);
    PostMessageW(retry->window,PresentMessage(),0,0);
}
void PresentationTimer::Retry(HWND window) {
    retries.fetch_add(1, std::memory_order_relaxed);
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&RetryCallback),&module)) return;
    auto* context = new(std::nothrow) RetryContext{window,module};
    if (!context || !TrySubmitThreadpoolCallback(RetryCallback,context,nullptr)) {
        delete context; FreeLibrary(module);
    }
}
PresentationTimer::~PresentationTimer() { Cancel(); }
void PresentationTimer::Cancel() {
    if (timer_) { DeleteTimerQueueTimer(nullptr,timer_,INVALID_HANDLE_VALUE); timer_ = nullptr; }
    window_ = nullptr;
    if (highResolution_) { timeEndPeriod(1); highResolution_ = false; }
}
void PresentationTimer::Schedule(HWND window, std::int64_t deadline, std::int64_t frequency) {
    if (!window || !IsWindow(window) || !PresentMessage() || frequency <= 0) throw std::runtime_error("Invalid MFG presentation window");
    if (timer_ && window_ != window) Cancel();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const auto ticks = (std::max)(std::int64_t{0},deadline-now.QuadPart);
    const auto due = static_cast<DWORD>((ticks*1000+frequency-1)/frequency);
    if (!highResolution_) highResolution_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    // ChangeTimerQueueTimer does not rearm a one-shot timer that has already fired, so recreate it.
    if (timer_) { DeleteTimerQueueTimer(nullptr,timer_,INVALID_HANDLE_VALUE); timer_ = nullptr; }
    window_ = window;
    if (!CreateTimerQueueTimer(&timer_,nullptr,Callback,&window_,due,0,WT_EXECUTEDEFAULT))
        throw std::runtime_error("Create MFG presentation timer failed");
}
}
