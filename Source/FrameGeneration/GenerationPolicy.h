// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace framegen {
// Honor the selected multiplier and provider capabilities. Display refresh must
// not change the generated-frame count as NR load/input cadence changes.
inline unsigned GeneratedLimit(unsigned multiplier, unsigned supported, double refreshHz, float frameTimeMs,
                               bool limitToDisplay = false) {
    if (multiplier < 2 || !std::isfinite(frameTimeMs) || frameTimeMs <= 0)
        return 0;
    unsigned count = (std::min)({multiplier - 1, supported, 3u});
    if (limitToDisplay) {
        if (!std::isfinite(refreshHz) || refreshHz <= 0) return 0;
        const auto visible = static_cast<unsigned>(std::clamp(std::floor(refreshHz * frameTimeMs / 1000.0 + 0.05), 1.0, 4.0));
        count = (std::min)(count, visible - 1);
    }
    return count;
}
}
