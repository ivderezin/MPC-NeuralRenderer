// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/FrameGeneration/PresentationThread.h"
#include <algorithm>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::int64_t Now() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
int main() {
    try {
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        const auto ms = frequency.QuadPart / 1000;
        std::mutex lock;
        std::deque<std::int64_t> deadlines;
        std::vector<std::int64_t> lateness;
        unsigned busy = 0;
        bool early = false;
        framegen::PresentationThread thread;
        auto next = [&](std::int64_t& deadline) {
            std::lock_guard guard(lock);
            if (deadlines.empty()) return false;
            deadline = deadlines.front();
            return true;
        };
        auto present = [&] {
            std::lock_guard guard(lock);
            if (busy) { --busy; return false; } // renderer lock held elsewhere
            const auto now = Now();
            if (deadlines.empty() || now < deadlines.front()) { early = early || !deadlines.empty(); return true; }
            lateness.push_back(now - deadlines.front());
            deadlines.pop_front();
            return true;
        };
        thread.Start(next, present);
        {
            std::lock_guard guard(lock);
            const auto start = Now() + 20 * ms;
            for (int i = 0; i < 20; ++i) deadlines.push_back(start + i * 8 * ms);
        }
        thread.Wake();
        for (int i = 0; i < 200; ++i) {
            Sleep(5);
            std::lock_guard guard(lock);
            if (deadlines.empty()) break;
        }
        {
            std::lock_guard guard(lock);
            Require(deadlines.empty() && lateness.size() == 20, "Every scheduled frame must be presented");
            Require(!early, "Frames must never be presented before their deadline");
            std::int64_t worst = 0;
            for (auto value : lateness) worst = (std::max)(worst, value);
            Require(worst < 20 * ms, "Presentation must follow the schedule without UI-thread delays");
            busy = 3;
            deadlines.push_back(Now());
        }
        thread.Wake();
        for (int i = 0; i < 200; ++i) {
            Sleep(5);
            std::lock_guard guard(lock);
            if (deadlines.empty()) break;
        }
        Require(thread.Retries() == 3, "Busy renderer must be retried without dropping the frame");
        {
            std::lock_guard guard(lock);
            Require(deadlines.empty(), "Retried frame must be presented once the renderer is free");
            busy = 1'000'000; // simulate a caller that holds the renderer lock while stopping
            deadlines.push_back(Now());
        }
        thread.Wake();
        Sleep(20);
        const auto stopStart = Now();
        thread.Stop();
        Require(Now() - stopStart < 500 * ms, "Stop must not deadlock while the renderer lock is held");
        Require(!thread.Running(), "Stopped thread must not keep running");
        std::cout << "Dedicated presentation thread tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
