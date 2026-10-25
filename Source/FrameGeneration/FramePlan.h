// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace framegen {
class FramePlan {
    std::array<std::int64_t,4> deadlines_{};
    std::array<unsigned,4> textures_{};
    unsigned count_ = 0, next_ = 0;
public:
    void Clear() { count_ = next_ = 0; }
    void Start(std::int64_t now, std::int64_t duration, unsigned generated, double refreshHz, double clockHz) {
        Clear();
        if (duration <= 0 || !generated || !std::isfinite(refreshHz) || !std::isfinite(clockHz) || refreshHz <= 0 || clockHz <= 0) return;
        generated = (std::min)(generated, 3u);
        const auto visibleFrames = static_cast<unsigned>(std::clamp(std::floor(refreshHz * static_cast<double>(duration) / clockHz + 0.05),1.0,4.0));
        const auto frames = (std::min)(generated + 1, visibleFrames);
        if (frames < 2) return;
        for (unsigned step = 1; step <= frames; ++step) {
            const auto texture = (step * (generated+1) + frames-1) / frames - 1;
            deadlines_[count_] = now + duration * texture / (generated+1);
            textures_[count_++] = texture;
        }
    }
    bool Pending() const { return next_ < count_; }
    std::int64_t NextDeadline() const { return deadlines_[next_]; }
    int TakeDue(std::int64_t now) {
        int texture = -1;
        while (Pending() && deadlines_[next_] <= now) texture = static_cast<int>(textures_[next_++]);
        return texture;
    }
};
}
