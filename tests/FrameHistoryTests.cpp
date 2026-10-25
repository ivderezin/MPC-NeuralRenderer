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
    std::cout << "Frame history and asynchronous output tests passed\n";
}
