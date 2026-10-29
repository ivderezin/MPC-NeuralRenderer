#include "../Source/FrameGeneration/EchoPresentationClock.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/NeuralRendering/FrameHistory.h"
#include "../Source/NeuralRendering/AsyncOutput.h"
#include <cstdlib>
#include <iostream>
#include <limits>

void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using Action = neural::FrameHistory::Action;
    neural::FrameHistory history;
    Require(history.Classify(0) == Action::Reset, "first frame must reset");
    history.Commit(0);
    Require(history.Classify(0) == Action::Repeat, "paused redraw must reuse output");
    Require(history.Classify(166667) == Action::Advance, "60 Hz frame must advance");
    Require(history.Classify(-1) == Action::Reset, "backwards seek must reset");
    Require(history.Classify(5'000'001) == Action::Reset, "capture interruption must reset");
    history.Commit(100);
    Require(history.Classify(100) == Action::Repeat, "only committed frames can repeat");
    history.Clear();
    Require(history.Classify(100) == Action::Reset, "flush must invalidate cached result");
    history.Commit(-100);
    Require(history.Classify(0) == Action::Advance, "negative preroll timestamps are valid");
    history.Commit(std::numeric_limits<long long>::min());
    Require(history.Classify(std::numeric_limits<long long>::max()) == Action::Reset, "timestamp difference must not overflow");
    neural::AsyncOutput output;
    output.Submit(1, false);
    Require(output.Busy(0) && !output.Collect(0), "GPU warmup must remain pending without blocking");
    bool rejected = false;
    try { output.Submit(2, true); } catch (const std::logic_error&) { rejected = true; }
    Require(rejected, "in-flight work must not accumulate into a queue");
    Require(!output.Collect(1) && !output.Available(), "warmup completion is not a rendered frame");
    output.Submit(2, true);
    Require(!output.Collect(1) && !output.Available(), "incomplete output must not be sampled");
    Require(output.Collect(2) && output.Available(), "completed inference becomes presentable");
    Require(!output.Collect(2), "completed output must be collected only once");
    output.Submit(3, true);
    Require(output.Busy(2) && output.Available(), "rendering must reuse the last completed image while GPU is busy");
    output.Invalidate();
    Require(!output.Available() && output.Busy(2), "seek invalidates display without freeing in-flight resources");
    Require(!output.Collect(3) && !output.Available(), "pre-seek inference must never reappear");
    output.Submit(4, true);
    Require(output.Collect(4) && output.Available(), "post-seek inference must restore output");
    framegen::EchoPresentationClock clock;
    // Stable 100 Hz display, two matching quarter-second observations.
    clock.Observe(1,10000,1000000);clock.Observe(26,260000,1000000);clock.Observe(51,510000,1000000);
    std::array<std::int64_t,4> aligned{515001,525001,535001,545001};
    clock.Align(aligned,3,510000,40000);
    Require(aligned[0]==515000 && aligned[1]==525000 && aligned[2]==535000,"Stable refresh must align independent presentation slots");
    std::array<std::int64_t,4> overRefresh{512000,514000,516000,518000};
    const auto original=overRefresh;clock.Align(overRefresh,4,510000,10000);
    Require(overRefresh==original,"Refresh alignment must not cap an above-refresh multiplier");
    clock.Reset();auto unstable=original;clock.Align(unstable,4,510000,10000);
    Require(unstable==original,"Unstable / unavailable refresh timings must preserve interval pacing");
    Require(clock.SmoothDuration(16000,1000000)==16000,"Initial duration");
    Require(clock.SmoothDuration(20000,1000000)==16400,"Echo interval smoothing");
    std::cout << "Frame history and asynchronous output tests passed\n";
}
