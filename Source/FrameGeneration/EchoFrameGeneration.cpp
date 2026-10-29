// SPDX-License-Identifier: GPL-3.0-or-later
#include "EchoFrameGeneration.h"
#include "../EchoStorm/engine/own_synth.h"
#include "../EchoStorm/engine/flow_estimator.h"
#include "../EchoStorm/engine/flow_fix.h"
#include "../Processing/ResourceBarrier.h"
#include "../Processing/RuntimeLibrary.h"
#include <wrl/client.h>
#include <algorithm>
namespace framegen {
using Microsoft::WRL::ComPtr;
using processing::Transition;
using processing::CheckHr;
struct EchoFrameGeneration::Impl {
    nr::OwnSynth synth;
    nr::FlowEstimator estimate;
    nr::FlowFix repair;
    ComPtr<ID3D12Resource> before, motion, distrust, repaired;
    unsigned width=0,height=0,scale=1,slot=0,pending=0;
    bool hdr=false,haveBefore=false;
    std::string error;
    ComPtr<ID3D12Resource> Texture(ID3D12Device* device,unsigned w,unsigned h,DXGI_FORMAT format,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=w; desc.Height=h; desc.Format=format;
        desc.MipLevels=desc.DepthOrArraySize=desc.SampleDesc.Count=1;
        if (state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS) desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> result;
        CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"Create EchoFG texture");
        return result;
    }
};
EchoFrameGeneration::EchoFrameGeneration()=default;
EchoFrameGeneration::~EchoFrameGeneration()=default;
void EchoFrameGeneration::Create(ID3D12Device* device,unsigned width,unsigned height,bool hdr) {
    auto next=std::make_unique<Impl>();
    next->width=width; next->height=height; next->hdr=hdr;
    next->scale=width>2560 ? 2u : 1u;
    auto log=[p=next.get()](const char* message) { p->error=message; };
    if (!next->estimate.Init(device,log) || !next->repair.Init(device,log) || !next->synth.Init(device,log))
        throw std::runtime_error("Initialize EchoFG: "+next->error);
    const unsigned aw=(width+next->scale-1)/next->scale, ah=(height+next->scale-1)/next->scale;
    next->estimate.SetInputScale(static_cast<int>(next->scale));
    if (!next->estimate.Ensure(aw,ah) || !next->repair.Ensure(aw,ah,next->scale,width,height) || !next->synth.Ensure(width,height))
        throw std::runtime_error("Allocate EchoFG: "+next->error);
    next->before=next->Texture(device,width,height,hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST);
    next->motion=next->Texture(device,aw,ah,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    next->distrust=next->Texture(device,aw,ah,DXGI_FORMAT_R8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    next->repaired=next->Texture(device,width,height,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    impl_=std::move(next);
}
void EchoFrameGeneration::Dispatch(ID3D12GraphicsCommandList* list,ID3D12Resource* color,
    const std::array<ID3D12Resource*,3>& output,unsigned count,bool reset) {
    auto& p=*impl_;
    count=(std::min)(count,3u); p.pending=0;
    if (reset) { p.estimate.Forget(); p.haveBefore=false; }
    const auto format=color->GetDesc().Format;
    const unsigned encoding=p.hdr ? 2u : 0u;
    const int slot=static_cast<int>(p.slot++%nr::FlowEstimator::kSlots);
    p.estimate.ReadStats(slot);
    p.estimate.Record(list,slot,color,format,p.motion.Get(),p.distrust.Get(),0.f,encoding,203.f);
    Transition(list,p.motion.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,p.distrust.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (p.haveBefore) Transition(list,p.before.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const bool repaired=p.repair.Record(list,slot,p.haveBefore ? p.before.Get() : color,format,color,format,
        p.motion.Get(),p.distrust.Get(),p.repaired.Get(),encoding,203.f,p.haveBefore);
    Transition(list,p.repaired.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bool made=false;
    if (repaired && p.haveBefore && count) {
        nr::OwnSynth::Target targets[3]{};
        for (unsigned i=0;i<count;++i) targets[i]={output[i],static_cast<float>(i+1)/static_cast<float>(count+1)};
        made=p.synth.Record(list,slot,p.before.Get(),format,color,format,p.repaired.Get(),format,targets,
            static_cast<int>(count),encoding,203.f,.6f,1.f,nullptr,1.f);
    }
    if (p.haveBefore) Transition(list,p.before.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(list,p.repaired.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(list,p.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(list,p.distrust.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(p.before.Get(),color);
    Transition(list,color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const bool expected=p.haveBefore && count;
    p.haveBefore=true;
    if (!repaired || (expected && !made)) throw std::runtime_error("Record EchoFG: "+p.error);
    p.pending=made ? count : 0;
}
unsigned EchoFrameGeneration::Collect() const { return impl_ ? impl_->pending : 0; }
}
