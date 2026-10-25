// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace neural {
class FrameHistory {
    bool valid_ = false;
    std::int64_t timestamp_ = 0;
public:
    enum class Action { Reset, Advance, Repeat };
    Action Classify(std::int64_t timestamp) const {
        if (!valid_) return Action::Reset;
        if (timestamp == timestamp_) return Action::Repeat;
        if (timestamp < timestamp_ || static_cast<std::uint64_t>(timestamp) - static_cast<std::uint64_t>(timestamp_) > 5'000'000) return Action::Reset;
        return Action::Advance;
    }
    void Commit(std::int64_t timestamp) { timestamp_ = timestamp; valid_ = true; }
    void Clear() { valid_ = false; }
};
}
