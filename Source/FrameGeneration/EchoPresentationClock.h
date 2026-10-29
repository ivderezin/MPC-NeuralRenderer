// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace framegen {
// Host adaptation of EchoStorm framegen11's interval smoothing and
// SampleDisplay / AlignFits / SnapToRefresh. Alignment never lowers a multiplier.
class EchoPresentationClock {
    std::int64_t anchorTime_=0,windowTime_=0,lastSlot_=-1;
    std::uint32_t anchorCount_=0,windowCount_=0;
    double period_=0,smoothedDuration_=0;
    unsigned stableWindows_=0;
    bool haveAnchor_=false;
public:
    void Reset() { *this={}; }
    void Observe(std::uint32_t count,std::int64_t qpc,double frequency) {
        if (!std::isfinite(frequency) || frequency<=0 || qpc<=0) return;
        if (!haveAnchor_ || count<anchorCount_ || qpc<anchorTime_) {
            Reset();haveAnchor_=true;anchorTime_=windowTime_=qpc;anchorCount_=windowCount_=count;return;
        }
        if (static_cast<double>(qpc-windowTime_)>=frequency*.25 && count>windowCount_) {
            const double measured=static_cast<double>(qpc-windowTime_)/(count-windowCount_);
            if (period_>0 && std::abs(measured-period_)/period_<.02) ++stableWindows_;
            else stableWindows_=1;
            period_=measured;windowTime_=qpc;windowCount_=count;
        }
        anchorTime_=qpc;anchorCount_=count;
    }
    std::int64_t SmoothDuration(std::int64_t duration,double frequency) {
        const double ms=static_cast<double>(duration)*1000.0/frequency;
        if (ms<=2 || ms>=100) { smoothedDuration_=0;return duration; }
        smoothedDuration_=smoothedDuration_>0 && static_cast<double>(duration)<3*smoothedDuration_
            ? .9*smoothedDuration_+.1*static_cast<double>(duration) : static_cast<double>(duration);
        return static_cast<std::int64_t>(smoothedDuration_);
    }
    void Align(std::array<std::int64_t,4>& deadlines,unsigned count,std::int64_t now,std::int64_t duration) {
        if (!haveAnchor_ || stableWindows_<2 || period_<=0 || !count || static_cast<double>(duration)<count*period_*1.1) return;
        auto proposed=deadlines;
        auto last=lastSlot_;
        for (unsigned i=0;i<count;++i) {
            auto slot=static_cast<std::int64_t>(anchorCount_)+
                static_cast<std::int64_t>(std::floor(static_cast<double>(deadlines[i]-anchorTime_)/period_));
            slot=(std::max)(slot,last+1);
            auto time=anchorTime_+static_cast<std::int64_t>((static_cast<double>(slot-static_cast<std::int64_t>(anchorCount_))+.5)*period_);
            if (time<now) { ++slot;time+=static_cast<std::int64_t>(period_); }
            if (time>=now+duration) return; // no backlog beyond the next real interval
            proposed[i]=time;last=slot;
        }
        deadlines=proposed;lastSlot_=last;
    }
};
}
