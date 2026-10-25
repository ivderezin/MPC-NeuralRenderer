// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Processing/Providers.h"
namespace neural {
struct Settings {
    bool enabled = false;
    int intensity = 50;
    processing::Upscaler upscaler = processing::Upscaler::Dlss;
    processing::FrameGenerator frameGenerator = processing::FrameGenerator::Dlss;
    bool ManualGeneration() const { return frameMultiplier > 1 && frameGenerator != processing::FrameGenerator::Xess; }
    unsigned frameMultiplier = 1;
    bool operator==(const Settings&) const = default;
    static Settings Load();
    long Save() const;
};
}
