// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/Upscaling/UpscalePolicy.h"
#include "../Source/FrameGeneration/FramePlan.h"
#include "../Source/FrameGeneration/FrameQueue.h"
#include "../Source/FrameGeneration/GenerationPolicy.h"
#include "../Source/FrameGeneration/DisplayCadence.h"
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
    Require(GeneratedLimit(4,3,160,1000.0f/60,true)==1,"160 Hz display must cap 60 FPS generation to 2x");
    Require(GeneratedLimit(4,1,240,1000.0f/60)==1,"FSR 3 supports only one generated frame");
    Require(GeneratedLimit(4,3,240,1000.0f/60)==3,"240 Hz display supports 4x for 60 FPS");
    Require(GeneratedLimit(1,3,240,16.67f)==0,"Off must bypass generation");
    Require(GeneratedLimit(4,3,60,16.67f,true)==0,"No spare refresh slots must bypass generation");
    Require(GeneratedLimit(4,3,std::numeric_limits<double>::infinity(),16,true)==0,"Invalid refresh rate must bypass");
    for (float ms : {1000.f/30,1000.f/60,1000.f/120})
        Require(GeneratedLimit(4,3,160,ms)==3,"Without the optional cap NR load must not change the selected multiplier");
    Require(GeneratedLimit(4,1,60,16.67f)==1,"Uncapped FSR must still respect its provider limit");
    Require(GeneratedLimit(4,3,0,16.67f)==3,"Unknown refresh must not suppress uncapped FG");
    neural::Settings selection;
    selection.upscaler=processing::Upscaler::Fsr;
    selection.frameGenerator=processing::FrameGenerator::Xess;selection.frameMultiplier=4;
    Require(!selection.ManualGeneration(),"XeSS must use its presentation backend");
    selection.frameGenerator=processing::FrameGenerator::Fsr;
    Require(selection.ManualGeneration(),"FSR must use host presentation");
    Require(selection.upscaler==processing::Upscaler::Fsr,"Changing generation must preserve the upscaler");
    selection.frameMultiplier=1;
    Require(!selection.ManualGeneration(),"Off must bypass all manual generators");
    for (auto provider : {processing::Upscaler::Dlss, processing::Upscaler::Fsr, processing::Upscaler::Xess}) {
        selection.upscaler = provider;
        selection.amdBridge = true;
        selection.enabled = true;
        selection.frameMultiplier = 4;
        auto previous = processing::Upscaler::Dlss;
        unsigned multiplier = 2;
        selection.ToggleUpscaling(previous);
        Require(selection.upscaler == processing::Upscaler::Off && !selection.FsrNative(),
                "Disabling upscaling must also stop native FSR bridge dispatch");
        Require(selection.enabled && selection.frameMultiplier == 4, "Upscaling toggle must preserve NR and FG");
        selection.ToggleGeneration(multiplier);
        Require(selection.frameMultiplier == 1 && !selection.ManualGeneration(), "FG toggle must bypass generation");
        selection.ToggleUpscaling(previous);
        selection.ToggleGeneration(multiplier);
        Require(selection.upscaler == provider && selection.frameMultiplier == 4,
                "Toggles must restore the selected provider and multiplier");
        Require(selection.FsrNative() == (provider == processing::Upscaler::Fsr), "Native bridge must require FSR");
    }
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
    plan.Start(0,4000,3,160,240000,true);
    Require(plan.TakeDue(0)==-1 && plan.TakeDue(1000)==1,"160Hz must not receive a 240fps burst");
    Require(plan.TakeDue(3000)==3 && !plan.Pending(),"refresh-limited output must still finish with the real frame");
    plan.Start(0,4000,3,240,240000);
    Require(plan.TakeDue(2500)==2,"a late UI tick must drop overdue frames rather than replay a backlog");
    plan.Start(10000,4000,1,240,240000);
    Require(plan.TakeDue(10000)==0,"a new batch must replace the old schedule");
    plan.Clear();
    Require(!plan.Pending(),"resize and pause must cancel scheduled output");
    plan.Start(0,4000,3,60,240000,true);
    Require(!plan.Pending(),"60Hz cannot display 60fps plus generated frames");
    plan.Start(0,4000,0,240,240000);
    Require(!plan.Pending(),"reset or disabled interpolation must not produce synthetic frames");
    plan.Start(0,4000,3,std::numeric_limits<double>::quiet_NaN(),240000,true);
    Require(!plan.Pending(),"invalid refresh rate must not create presentation deadlines");
    {
        // 60fps at 240Hz with 2 generated frames: deadlines at 0, 1333 and 2666 ticks of a 240kHz clock.
        framegen::FrameQueue queue;
        Require(queue.Add(0,4000,2,240,240000,1),"queue must accept a displayable batch");
        Require(!queue.TakeDue(-1),"queued frames must never be presented early");
        auto frame = queue.TakeDue(0);
        Require(frame && frame->batch==1 && frame->texture==0 && !frame->real,"first generated frame starts the batch");
        frame = queue.TakeDue(1333);
        Require(frame && frame->texture==1 && !frame->real,"second generated frame keeps its position");
        // The next batch arrives before the previous real frame was shown: it must queue behind it.
        Require(queue.Add(2000,4000,2,240,240000,2),"early batch must be queued");
        frame = queue.TakeDue(2666);
        Require(frame && frame->batch==1 && frame->real,"previous real frame must not be dropped by an early batch");
        Require(queue.NextDeadline()==3999,"queued batch starts one step after the previous real frame");
        frame = queue.TakeDue(3999);
        Require(frame && frame->batch==2 && frame->texture==0,"queued batch follows the previous real frame");
        skipped = 0;
        frame = queue.TakeDue(100000,&skipped);
        Require(frame && frame->batch==2 && frame->real && skipped==1 && !queue.Pending(),"late wakeup shows the latest due frame");

        queue.Clear();
        Require(queue.Add(0,4000,2,240,240000,5),"queue must accept batch 5");
        Require(queue.Add(100,4000,2,240,240000,7),"queue must accept batch 7");
        frame = queue.TakeDue(100);
        Require(frame && frame->batch==7,"a non-adjacent batch must not keep a frame from an overwritten texture slot");

        queue.Clear();
        Require(queue.Add(0,4000,3,240,240000,10),"queue must accept an NR-off style fast batch");
        frame=queue.TakeDue(0);
        Require(frame && frame->batch==10 && frame->texture==0,"first interpolation must be displayed");
        Require(queue.Add(500,4000,3,240,240000,11),"an early completion must retain the preceding interpolations");
        frame=queue.TakeDue(1000);
        Require(frame && frame->batch==10 && frame->texture==1 && !frame->real,
                "early arrival must not silently discard the preceding second interpolation");
        frame=queue.TakeDue(2000);
        Require(frame && frame->batch==10 && frame->texture==2 && !frame->real,
                "early arrival must not silently discard the preceding third interpolation");
        frame=queue.TakeDue(3000);
        Require(frame && frame->batch==10 && frame->real,"real frame must follow preserved interpolations");
        frame=queue.TakeDue(4000);
        Require(frame && frame->batch==11 && frame->texture==0,"next batch must follow in temporal order");

        queue.Clear();
        Require(queue.Add(0,4000,2,240,240000,1),"queue must accept batch 1");
        Require(queue.Add(0,1000,2,960,240000,2),"queue must accept a short batch");
        frame = queue.TakeDue(0);
        Require(frame && frame->batch==2 && frame->texture==0,"added latency must stay below one batch");
        Require(!queue.Add(0,4000,3,60,240000,3,nullptr,true),"undisplayable batch must be rejected");
    }
    plan.Start(0,4000,3,60,240000);
    for (unsigned i=0;i<4;++i) Require(plan.TakeDue(i*1000)==static_cast<int>(i),"Uncapped UI mode must schedule every generated frame");
    framegen::FrameQueue uncappedQueue;
    Require(uncappedQueue.Add(0,4000,3,60,240000,1),"Uncapped threaded mode must ignore monitor refresh");
    for (unsigned i=0;i<4;++i) {
        const auto item=uncappedQueue.TakeDue(i*1000);
        Require(item && item->texture==i,"Uncapped threaded mode must schedule every texture in order");
    }
    framegen::DisplayCadence cadence;
    unsigned discardedFrames=0;
    uncappedQueue.Clear();
    Require(uncappedQueue.Add(0,4000,3,60,240000,5),"queue must accept diagnostic batch");
    Require(uncappedQueue.Add(1,4000,3,60,240000,7,&discardedFrames),"queue must accept a non-adjacent batch");
    Require(discardedFrames==4,"Overwritten pending frames must appear in skipped statistics");
    cadence.Observe(100,0,1000);
    Require(!cadence.Available(),"Displayed rate needs a complete sample interval");
    cadence.Observe(130,500,1000);
    Require(cadence.Available() && cadence.Fps()==60,"DXGI displayed rate must use displayed images rather than Present calls");
    cadence.Observe(130,1000,1000);
    Require(cadence.Fps()==0,"Repeated display statistics must not count new images");
    cadence.Observe(1,1100,1000);
    Require(!cadence.Available(),"New swap chain/disjoint counts must reset displayed rate");
    cadence.Reset();
    Require(!cadence.Available(),"Unsupported DXGI statistics must be reported as unavailable");
    std::cout << "Upscaling and MFG scheduling tests passed\n";
}
