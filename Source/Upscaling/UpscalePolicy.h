// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace upscaling {
struct Extent {
    unsigned width = 0, height = 0;
    bool operator==(const Extent&) const = default;
};
inline bool ShouldUpscale(Extent source, Extent target, Extent monitor) {
    if (!source.width || !source.height || !target.width || !target.height || !monitor.width || !monitor.height) return false;
    return source.width <= monitor.width && source.height <= monitor.height && source != monitor
        && target.width >= source.width && target.height >= source.height && target != source;
}
}
