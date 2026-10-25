// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/NeuralRendering/FrameHistory.h"
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
    std::cout << "Frame history tests passed\n";
}
