// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>

namespace neural {
class AsyncOutput {
    std::uint64_t fence_ = 0, generation_ = 0, pendingGeneration_ = 0;
    bool producesOutput_ = false, available_ = false;
public:
    bool Busy(std::uint64_t completed) const { return fence_ && completed < fence_; }
    bool Available() const { return available_; }
    void Submit(std::uint64_t fence, bool producesOutput) {
        if (fence_) throw std::logic_error("NR submission is already pending");
        fence_ = fence;
        pendingGeneration_ = generation_;
        producesOutput_ = producesOutput;
    }
    bool Collect(std::uint64_t completed) {
        if (!fence_ || Busy(completed)) return false;
        fence_ = 0;
        if (!producesOutput_ || pendingGeneration_ != generation_) return false;
        available_ = true;
        return true;
    }
    void Invalidate() { ++generation_; available_ = false; }
};
}
