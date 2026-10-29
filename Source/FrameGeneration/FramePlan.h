// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "EchoPresentationClock.h"
namespace framegen {
class FramePlan {
    std::array<std::int64_t,4> deadlines_{};
    std::array<unsigned,4> textures_{};
    unsigned count_ = 0, next_ = 0;
public:
    void Clear() { count_ = next_ = 0; }
    void Start(std::int64_t now, std::int64_t duration, unsigned generated, double refreshHz, double clockHz,
               bool limitToDisplay = false,EchoPresentationClock* displayClock=nullptr) {
        Clear();
        if (duration <= 0 || !generated || !std::isfinite(clockHz) || clockHz <= 0) return;
        generated = (std::min)(generated, 3u);
        unsigned frames = generated + 1;
        if (limitToDisplay) {
            if (!std::isfinite(refreshHz) || refreshHz <= 0) return;
            const auto visible = static_cast<unsigned>(std::clamp(std::floor(refreshHz * static_cast<double>(duration) / clockHz + 0.05), 1.0, 4.0));
            frames = (std::min)(frames, visible);
            if (frames < 2) return;
        }
        for (unsigned step = 1; step <= frames; ++step) {
            const auto texture = (step * (generated + 1) + frames - 1) / frames - 1;
            deadlines_[count_] = now + duration * texture / (generated+1);
            textures_[count_++] = texture;
        }
        if (displayClock) displayClock->Align(deadlines_,count_,now,duration);
    }
    bool Pending() const { return next_ < count_; }
    std::int64_t NextDeadline() const { return deadlines_[next_]; }
    // Returns the latest due texture; frames overtaken by a late wakeup are counted in skipped.
    int TakeDue(std::int64_t now, unsigned* skipped = nullptr) {
        int texture = -1;
        while (Pending() && deadlines_[next_] <= now) {
            if (texture >= 0 && skipped) ++*skipped;
            texture = static_cast<int>(textures_[next_++]);
        }
        return texture;
    }
};
}
