// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Processing/Providers.h"
namespace neural {
// Where NR runs relative to the upscaler; without upscaling it always runs once on the source frame.
enum class NrStage : unsigned {
    BeforeUpscale,
    AfterUpscale,
    Both
};
struct Settings {
    bool enabled = false;
    int intensity = 50;
    NrStage nrStage = NrStage::BeforeUpscale;
    processing::Upscaler upscaler = processing::Upscaler::Dlss;
    processing::FrameGenerator frameGenerator = processing::FrameGenerator::Dlss;
    bool ManualGeneration() const { return frameMultiplier > 1 && frameGenerator != processing::FrameGenerator::Xess; }
    unsigned frameMultiplier = 1;
    // Present DLSS / FSR generated frames from a dedicated thread with a queued schedule.
    bool presentThread = true;
    bool operator==(const Settings&) const = default;
    static Settings Load();
    long Save() const;
};
}
