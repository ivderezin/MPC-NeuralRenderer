// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/FrameGeneration/DlssCaptureCamera.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include "../Source/Processing/LoadedModules.h"
#endif
void Require(bool ok,const char* text) {if(!ok)throw std::runtime_error(text);}
int main() {
    try {
        using Camera=framegen::DlssCaptureCamera;
        for(float aspect:{.5f,1.f,16.f/9.f,32.f/9.f}) {
            Camera c(aspect);
            auto depth=[&](float z){return z*c.viewToClip[2][2]+c.viewToClip[3][2];};
            Require(std::abs(depth(Camera::Near)-1.f)<1e-5f,"Reversed orthographic near plane must map to 1");
            Require(std::abs(depth(Camera::Far))<1e-5f,"Reversed orthographic far plane must map to 0");
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
                double value=0;
                for(unsigned k=0;k<4;++k)value+=static_cast<double>(c.viewToClip[y][k])*c.clipToView[k][x];
                Require(std::abs(value-(x==y ? 1.0 : 0.0))<1e-4,"Projection and inverse must round-trip");
            }
            const float z=Camera::Depth*c.clipToView[2][2]+c.clipToView[3][2];
            Require(std::isfinite(z) && z>Camera::Near && z<Camera::Far,"Fallback depth must describe a finite interior plane");
            Require(std::abs(depth(z)-Camera::Depth)<1e-5f,"Fallback plane must match projection metadata");
        }
        for(float aspect:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            bool rejected=false;try{Camera invalid(aspect);}catch(const std::invalid_argument&){rejected=true;}
            Require(rejected,"Invalid camera aspect must be rejected");
        }
#ifdef _WIN32
        const auto modules=processing::LoadedModulesMatching(L"KeRnEl32.DlL");
        Require(!modules.empty(),"Loaded-module diagnostics must find the live Windows runtime");
        Require(!modules.front().path.empty() && modules.front().version!=L"unknown","Loaded module must report path and real file version");
        Require(processing::LoadedModulesMatching(L"mpc-definitely-not-loaded-6728.dll").empty(),"Diagnostics must not invent a missing module");
#endif
        std::cout<<"DLSS capture projection and loaded-module diagnostics passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
