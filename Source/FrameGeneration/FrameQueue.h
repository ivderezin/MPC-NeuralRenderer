// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
namespace framegen {
struct QueuedFrame {
    std::int64_t deadline = 0;
    unsigned long long batch = 0;
    unsigned texture = 0;
    bool real = false;
};
// Presentation schedule for the dedicated presenter. A batch that arrives before the previous one has
// finished is queued after the previous real frame instead of replacing it. Output textures are double
// buffered, so only the directly preceding batch can still be presented.
class FrameQueue {
    std::array<QueuedFrame, 8> frames_{};
    unsigned count_ = 0;
public:
    void Clear() { count_ = 0; }
    bool Pending() const { return count_ != 0; }
    std::int64_t NextDeadline() const { return frames_[0].deadline; }
    bool Add(std::int64_t now, std::int64_t duration, unsigned generated, double refreshHz, double clockHz,
             unsigned long long batch) {
        if (duration <= 0 || !generated || !std::isfinite(refreshHz) || !std::isfinite(clockHz) || refreshHz <= 0 ||
            clockHz <= 0)
            return false;
        generated = (std::min)(generated, 3u);
        const auto visibleFrames = static_cast<unsigned>(
            std::clamp(std::floor(refreshHz * static_cast<double>(duration) / clockHz + 0.05), 1.0, 4.0));
        const auto frames = (std::min)(generated + 1, visibleFrames);
        if (frames < 2)
            return false;
        // Keep only the newest pending real frame of the preceding batch; anything older is superseded or
        // would read a texture slot that the new batch has already overwritten.
        std::optional<QueuedFrame> previous;
        for (unsigned i = 0; i < count_; ++i)
            if (frames_[i].real && frames_[i].batch + 1 == batch)
                previous = frames_[i];
        count_ = 0;
        const auto step = duration / (generated + 1);
        std::array<QueuedFrame, 4> added{};
        for (unsigned i = 0; i < frames; ++i) {
            const auto texture = ((i + 1) * (generated + 1) + frames - 1) / frames - 1;
            added[i] = {now + duration * texture / (generated + 1), batch, texture, texture == generated};
        }
        std::int64_t shift = 0;
        if (previous) {
            const auto earliest = (std::max)(previous->deadline, now) + step;
            shift = (std::max)(std::int64_t{0}, earliest - added[0].deadline);
            // Bound the added latency to one batch; beyond that the old real frame is dropped.
            if (shift > duration) {
                previous.reset();
                shift = 0;
            }
        }
        if (previous)
            frames_[count_++] = *previous;
        for (unsigned i = 0; i < frames; ++i) {
            added[i].deadline += shift;
            frames_[count_++] = added[i];
        }
        return true;
    }
    // Returns the latest due frame; frames overtaken by a late wakeup are counted in skipped.
    std::optional<QueuedFrame> TakeDue(std::int64_t now, unsigned* skipped = nullptr) {
        unsigned due = 0;
        while (due < count_ && frames_[due].deadline <= now)
            ++due;
        if (!due)
            return std::nullopt;
        if (skipped)
            *skipped += due - 1;
        const auto frame = frames_[due - 1];
        std::copy(frames_.begin() + due, frames_.begin() + count_, frames_.begin());
        count_ -= due;
        return frame;
    }
};
}
