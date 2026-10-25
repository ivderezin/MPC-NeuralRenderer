// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace framegen {
inline unsigned GeneratedLimit(unsigned multiplier, unsigned supported, double refreshHz, float frameTimeMs) {
    if (multiplier < 2 || !std::isfinite(refreshHz) || !std::isfinite(frameTimeMs) || refreshHz <= 0 ||
        frameTimeMs <= 0)
        return 0;
    const auto visible =
        static_cast<unsigned>(std::clamp(std::floor(refreshHz * frameTimeMs / 1000.0 + 0.05), 1.0, 4.0));
    return (std::min)({multiplier - 1, supported, visible - 1, 3u});
}
}
