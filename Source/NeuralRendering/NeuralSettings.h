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
enum class MotionMethod : unsigned { Fast, EchoStorm };
inline const wchar_t* Name(MotionMethod value) {
    return value == MotionMethod::EchoStorm ? L"EchoStorm coarse-to-fine" : L"Fast block matching";
}
struct Settings {
    MotionMethod motionMethod = MotionMethod::EchoStorm;
    bool enabled = false;
    bool amdBridge = false; // Experimental AMDLSS5: dispatch FSR even at 1:1 resolution.
    bool FsrNative() const { return amdBridge && upscaler == processing::Upscaler::Fsr; }
    int intensity = 50;
    NrStage nrStage = NrStage::BeforeUpscale;
    processing::Upscaler upscaler = processing::Upscaler::Dlss;
    processing::FrameGenerator frameGenerator = processing::FrameGenerator::Dlss;
    void ToggleUpscaling(processing::Upscaler& previous) {
        if (upscaler != processing::Upscaler::Off) {
            previous = upscaler;
            upscaler = processing::Upscaler::Off;
        } else upscaler = previous;
    }
    void ToggleGeneration(unsigned& previous) {
        if (frameMultiplier > 1) {
            previous = frameMultiplier;
            frameMultiplier = 1;
        } else frameMultiplier = previous;
    }
    bool ManualGeneration() const { return frameMultiplier > 1 && frameGenerator != processing::FrameGenerator::Xess; }
    unsigned frameMultiplier = 1;
    // Present DLSS / FSR generated frames from a dedicated thread with a queued schedule.
    bool presentThread = true;
    bool limitGenerationToDisplay = false;
    bool operator==(const Settings&) const = default;
    static Settings Load();
    long Save() const;
};
}
