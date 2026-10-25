// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/FrameGeneration/PresentationTimer.h"
#include <iostream>
#include <stdexcept>
unsigned delivered = 0;
DWORD uiThread = 0;
bool correctThread = true;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == framegen::PresentMessage()) {
        ++delivered;
        correctThread = correctThread && GetCurrentThreadId() == uiThread;
        return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
void Pump(unsigned milliseconds, unsigned target = 0) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end && (!target || delivered < target)) {
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&message);
    }
}
int main() {
    try {
        uiThread = GetCurrentThreadId();
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = WindowProc; cls.lpszClassName = L"MPCNeuralPresentationTest";
        Require(RegisterClassW(&cls) != 0,"Register test window failed");
        HWND window = CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
        Require(window != nullptr,"Create message-only window failed");
        LARGE_INTEGER frequency{}, now{}; QueryPerformanceFrequency(&frequency);
        {
            framegen::PresentationTimer timer;
            QueryPerformanceCounter(&now);
            timer.Schedule(window,now.QuadPart + frequency.QuadPart/50,frequency.QuadPart);
            Pump(2000,1);
            Require(delivered == 1 && correctThread,"Presentation must be dispatched on the window thread");
            QueryPerformanceCounter(&now);
            timer.Schedule(window,now.QuadPart + frequency.QuadPart/50,frequency.QuadPart);
            Pump(2000,2);
            Require(delivered == 2,"One-shot timer must be rearmable after completion");
            QueryPerformanceCounter(&now);
            timer.Schedule(window,now.QuadPart + frequency.QuadPart/5,frequency.QuadPart);
            timer.Cancel();
            Pump(250);
            Require(delivered == 2,"Cancelled presentation must not post future frames");
        }
        framegen::PresentationTimer::Retry(window);
        Pump(2000,3);
        Require(delivered == 3 && correctThread,"Busy-lock retry must return to the same window thread");
        DestroyWindow(window); UnregisterClassW(cls.lpszClassName,cls.hInstance);
        std::cout << "Windows presentation timer tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
