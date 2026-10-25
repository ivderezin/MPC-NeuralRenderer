// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/Upscaling/UpscalePolicy.h"
#include "../Source/FrameGeneration/FramePlan.h"
#include "../Source/FrameGeneration/GenerationPolicy.h"
#include "../Source/NeuralRendering/NeuralSettings.h"
#include <cstdlib>
#include <iostream>
#include <limits>
void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using upscaling::ShouldUpscale;
    using framegen::GeneratedLimit;
    Require(GeneratedLimit(4,3,160,1000.0f/60)==1,"160 Hz display must cap 60 FPS generation to 2x");
    Require(GeneratedLimit(4,1,240,1000.0f/60)==1,"FSR 3 supports only one generated frame");
    Require(GeneratedLimit(4,3,240,1000.0f/60)==3,"240 Hz display supports 4x for 60 FPS");
    Require(GeneratedLimit(1,3,240,16.67f)==0,"Off must bypass generation");
    Require(GeneratedLimit(4,3,60,16.67f)==0,"No spare refresh slots must bypass generation");
    Require(GeneratedLimit(4,3,std::numeric_limits<double>::infinity(),16)==0,"Invalid refresh rate must bypass");
    neural::Settings selection;
    selection.upscaler=processing::Upscaler::Fsr;
    selection.frameGenerator=processing::FrameGenerator::Xess;selection.frameMultiplier=4;
    Require(!selection.ManualGeneration(),"XeSS must use its presentation backend");
    selection.frameGenerator=processing::FrameGenerator::Fsr;
    Require(selection.ManualGeneration(),"FSR must use host presentation");
    Require(selection.upscaler==processing::Upscaler::Fsr,"Changing generation must preserve the upscaler");
    selection.frameMultiplier=1;
    Require(!selection.ManualGeneration(),"Off must bypass all manual generators");
    Require(ShouldUpscale({1920,1080},{3840,2160},{3840,2160}),"1080p must upscale to a 4K display");
    Require(!ShouldUpscale({3840,2160},{3840,2160},{3840,2160}),"native 4K must bypass DLSS SR");
    Require(!ShouldUpscale({3840,2160},{1920,1080},{1920,1080}),"downscale is not DLSS SR");
    Require(!ShouldUpscale({1920,1080},{1280,720},{3840,2160}),"a smaller player window must not run SR");
    Require(!ShouldUpscale({3840,2160},{7680,4320},{3840,2160}),"native-monitor source must not upscale beyond display size");
    Require(!ShouldUpscale({1920,1080},{3840,2160},{1920,1080}),"SR must not upscale beyond the actual monitor");
    Require(!ShouldUpscale({0,1080},{3840,2160},{3840,2160}),"invalid input dimensions must bypass SR");
    framegen::FramePlan plan;
    plan.Start(0,4000,3,240,240000);
    Require(plan.Pending(),"240Hz must accept 60fps 4x MFG");
    Require(plan.TakeDue(-1)==-1,"MFG frames must never be presented early");
    Require(plan.TakeDue(0)==0,"first generated frame must precede the real frame");
    Require(plan.TakeDue(999)==-1,"MFG must wait for the next deadline");
    Require(plan.TakeDue(1000)==1,"second generated frame must keep its temporal position");
    Require(plan.TakeDue(2000)==2,"third generated frame must keep its temporal position");
    Require(plan.TakeDue(3000)==3 && !plan.Pending(),"real frame must finish the batch");
    unsigned skipped = 0;
    plan.Start(0,4000,3,240,240000);
    Require(plan.TakeDue(2500,&skipped)==2 && skipped==2,"late wakeup must report overtaken generated frames");
    Require(plan.TakeDue(3000,&skipped)==3 && skipped==2,"on-time frames are not counted as skipped");
    plan.Start(0,4000,3,160,240000);
    Require(plan.TakeDue(0)==-1 && plan.TakeDue(1000)==1,"160Hz must not receive a 240fps burst");
    Require(plan.TakeDue(3000)==3 && !plan.Pending(),"refresh-limited output must still finish with the real frame");
    plan.Start(0,4000,3,240,240000);
    Require(plan.TakeDue(2500)==2,"a late UI tick must drop overdue frames rather than replay a backlog");
    plan.Start(10000,4000,1,240,240000);
    Require(plan.TakeDue(10000)==0,"a new batch must replace the old schedule");
    plan.Clear();
    Require(!plan.Pending(),"resize and pause must cancel scheduled output");
    plan.Start(0,4000,3,60,240000);
    Require(!plan.Pending(),"60Hz cannot display 60fps plus generated frames");
    plan.Start(0,4000,0,240,240000);
    Require(!plan.Pending(),"reset or disabled interpolation must not produce synthetic frames");
    plan.Start(0,4000,3,std::numeric_limits<double>::quiet_NaN(),240000);
    Require(!plan.Pending(),"invalid refresh rate must not create presentation deadlines");
    std::cout << "Upscaling and MFG scheduling tests passed\n";
}
