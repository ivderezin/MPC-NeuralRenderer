// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace framegen {
// DXGI's displayed-image count is distinct from successful Present calls.
// Consume scheduler QPC timestamps; never substitute the API submission rate.
class DisplayCadence {
    std::uint32_t count_ = 0;
    std::int64_t time_ = 0;
    double fps_ = 0;
    bool started_ = false, available_ = false;
public:
    void Reset() { *this = {}; }
    bool Available() const { return available_; }
    double Fps() const { return fps_; }
    void Observe(std::uint32_t count, std::int64_t time, std::int64_t frequency) {
        if (frequency <= 0 || time < 0) { Reset(); return; }
        if (!started_ || time < time_ || count < count_) {
            Reset(); count_ = count; time_ = time; started_ = true; return;
        }
        if (time == time_ || time - time_ < frequency / 2) return;
        fps_ = static_cast<double>(count - count_) * static_cast<double>(frequency) /
               static_cast<double>(time - time_);
        available_ = true;
        count_ = count; time_ = time;
    }
};
}
