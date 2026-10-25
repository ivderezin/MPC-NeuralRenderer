// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cstdint>
namespace framegen {
inline UINT PresentMessage() {
    static const UINT message = RegisterWindowMessageW(L"MPC.NeuralRenderer.Present.3C416620-9628-4C7A-AAA0-6C6D42D2A8B1");
    return message;
}
class PresentationTimer {
    HANDLE timer_ = nullptr;
    HWND window_ = nullptr;
    bool highResolution_ = false;
    static void CALLBACK Callback(void* context, BOOLEAN);
    static void CALLBACK RetryCallback(PTP_CALLBACK_INSTANCE instance, void* context);
public:
    ~PresentationTimer();
    void Schedule(HWND window, std::int64_t deadline, std::int64_t frequency);
    void Cancel();
    static void Retry(HWND window);
};
}
