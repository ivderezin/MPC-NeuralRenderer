// SPDX-License-Identifier: GPL-3.0-or-later
#include "EchoFrameGeneration.h"
#include "../EchoStorm/engine/own_synth.h"
#include "../EchoStorm/engine/flow_estimator.h"
#include "../EchoStorm/engine/flow_fix.h"
#include "../Processing/ResourceBarrier.h"
#include "../Processing/RuntimeLibrary.h"
#include <wrl/client.h>
#include <algorithm>
#include <d3dcompiler.h>
namespace framegen {
using Microsoft::WRL::ComPtr;
using processing::Transition;
using processing::CheckHr;
struct EchoFrameGeneration::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> exportRoot;
    ComPtr<ID3D12PipelineState> exportPipeline;
    ComPtr<ID3D12DescriptorHeap> exportHeap;
    unsigned exportIndex=0,descriptorStride=0;
    nr::OwnSynth synth;
    FlowEstimator estimate;
    nr::FlowFix repair;
    ComPtr<ID3D12Resource> before, motion, distrust, repaired;
    unsigned width=0,height=0,scale=1,slot=0,pending=0;
    bool hdr=false,haveBefore=false,motionOnly=false;
    std::string error;
    ~Impl() { estimate.Shutdown(); }
    ComPtr<ID3D12Resource> Texture(ID3D12Device* device,unsigned w,unsigned h,DXGI_FORMAT format,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=w; desc.Height=h; desc.Format=format;
        desc.MipLevels=1; desc.DepthOrArraySize=1; desc.SampleDesc.Count=1;
        if (state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS) desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> result;
        CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"Create EchoFG texture");
        return result;
    }
};
EchoFrameGeneration::EchoFrameGeneration()=default;
EchoFrameGeneration::~EchoFrameGeneration()=default;
void EchoFrameGeneration::Create(ID3D12Device* device,unsigned width,unsigned height,bool hdr,bool motionOnly,DXGI_FORMAT inputFormat) {
    auto next=std::make_unique<Impl>();
    next->device=device;
    next->width=width; next->height=height; next->hdr=hdr; next->motionOnly=motionOnly;
    next->scale=width>2560 ? 2u : 1u;
    auto log=[p=next.get()](const char* message) { p->error=message; };
    if (!next->estimate.Init(device,log) || !next->repair.Init(device,log) || (!motionOnly && !next->synth.Init(device,log)))
        throw std::runtime_error("Initialize EchoFG: "+next->error);
    const unsigned aw=(width+next->scale-1)/next->scale, ah=(height+next->scale-1)/next->scale;
    next->estimate.SetInputScale(static_cast<int>(next->scale));
    if (!next->estimate.Ensure(aw,ah) || !next->repair.Ensure(aw,ah,next->scale,width,height) || (!motionOnly && !next->synth.Ensure(width,height)))
        throw std::runtime_error("Allocate EchoFG: "+next->error);
    next->before=next->Texture(device,width,height,inputFormat!=DXGI_FORMAT_UNKNOWN ? inputFormat : hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST);
    next->motion=next->Texture(device,aw,ah,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    next->distrust=next->Texture(device,aw,ah,DXGI_FORMAT_R8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    next->repaired=next->Texture(device,width,height,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    impl_=std::move(next);
}
void EchoFrameGeneration::Dispatch(ID3D12GraphicsCommandList* list,ID3D12Resource* color,
    const std::array<ID3D12Resource*,3>& output,unsigned count,bool reset) {
    auto& p=*impl_;
    p.exportIndex=0;
    count=p.motionOnly ? 0u : (std::min)(count,3u); p.pending=0;
    if (reset) { p.estimate.Forget(); p.haveBefore=false; }
    const auto format=color->GetDesc().Format;
    const unsigned encoding=p.hdr ? 2u : 0u;
    const int slot=static_cast<int>(p.slot++%FlowEstimator::kSlots);
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
void EchoFrameGeneration::WriteGuidance(ID3D12GraphicsCommandList* list,ID3D12Resource* output,
    ID3D12Resource* confidence,float fastMotion) {
    auto& p=*impl_;
    if (!output || p.exportIndex>=4) throw std::logic_error("Invalid native motion export");
    if (!p.exportPipeline) {
        static constexpr char shader[]=R"(
Texture2D<float2> motion : register(t0);
Texture2D<float> distrust : register(t1);
RWTexture2D<float2> output : register(u0);
RWTexture2D<float> confidence : register(u1);
cbuffer C : register(b0) { uint2 sourceSize; uint2 targetSize; float fast; uint mask; uint2 pad; };
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=targetSize))return;
    uint2 at=min(id.xy*sourceSize/targetSize,sourceSize-1);
    float2 v=motion.Load(int3(at,0));
    output[id.xy]=v*float2(targetSize)/float2(sourceSize);
    if(mask!=0u) {
        uint w,h;distrust.GetDimensions(w,h);
        uint2 q=min(at*uint2(w,h)/sourceSize,uint2(w,h)-1);
        float d=distrust.Load(int3(q,0));
        if(fast>0.0)d=max(d,saturate((length(v)-fast)/fast));
        confidence[id.xy]=d;
    }
}
)";
        D3D12_DESCRIPTOR_RANGE range[2]{};range[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range[0].NumDescriptors=2;
        range[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;range[1].NumDescriptors=2;range[1].OffsetInDescriptorsFromTableStart=2;
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,range};
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants.Num32BitValues=8;
        D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=2;root.pParameters=params;
        ComPtr<ID3DBlob> signature,code,errors;
        CheckHr(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"Serialize native guidance export");
        CheckHr(p.device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&p.exportRoot)),"Create native guidance root");
        CheckHr(D3DCompile(shader,sizeof(shader)-1,"EchoGuidance",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors),"Compile native guidance export");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=p.exportRoot.Get();pipeline.CS={code->GetBufferPointer(),code->GetBufferSize()};
        CheckHr(p.device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&p.exportPipeline)),"Create native guidance export");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=16;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CheckHr(p.device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p.exportHeap)),"Create native guidance descriptors");
        p.descriptorStride=p.device->GetDescriptorHandleIncrementSize(heap.Type);
    }
    const auto pass=p.exportIndex++;
    auto cpu=p.exportHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=static_cast<SIZE_T>(pass)*4*p.descriptorStride;
    for(auto* resource:{p.repaired.Get(),p.distrust.Get()}) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=resource->GetDesc().Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        p.device->CreateShaderResourceView(resource,&srv,cpu);cpu.ptr+=p.descriptorStride;
    }
    for(auto* resource:{output,confidence}) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=resource ? resource->GetDesc().Format : DXGI_FORMAT_R8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        p.device->CreateUnorderedAccessView(resource,nullptr,&uav,cpu);cpu.ptr+=p.descriptorStride;
    }
    for(auto* resource:{p.repaired.Get(),p.distrust.Get()}) Transition(list,resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(auto* resource:{output,confidence}) if(resource) Transition(list,resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto desc=output->GetDesc();
    struct Constants {UINT sourceW,sourceH,targetW,targetH;float fast;UINT mask,pad[2];} c{p.width,p.height,static_cast<UINT>(desc.Width),desc.Height,fastMotion,confidence ? 1u : 0u,{}};
    ID3D12DescriptorHeap* heaps[]={p.exportHeap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(p.exportRoot.Get());list->SetPipelineState(p.exportPipeline.Get());
    auto gpu=p.exportHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=static_cast<UINT64>(pass)*4*p.descriptorStride;
    list->SetComputeRootDescriptorTable(0,gpu);list->SetComputeRoot32BitConstants(1,8,&c,0);list->Dispatch((c.targetW+7)/8,(c.targetH+7)/8,1);
    for(auto* resource:{p.repaired.Get(),p.distrust.Get()}) Transition(list,resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for(auto* resource:{output,confidence}) if(resource) Transition(list,resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
ID3D12Resource* EchoFrameGeneration::Motion() const { return impl_ ? impl_->repaired.Get() : nullptr; }
unsigned EchoFrameGeneration::Collect() const { return impl_ ? impl_->pending : 0; }
}
