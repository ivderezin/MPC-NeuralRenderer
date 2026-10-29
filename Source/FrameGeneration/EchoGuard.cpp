// SPDX-License-Identifier: GPL-3.0-or-later
#include "EchoGuard.h"
#include "EchoGuardShaders.h"
#include "../Processing/ResourceBarrier.h"
#include "../Processing/RuntimeLibrary.h"
#include <d3dcompiler.h>
#include <algorithm>
namespace framegen {
using processing::CheckHr;
using processing::Transition;
void EchoGuard::Create(ID3D12Device* device,UINT width,UINT height,bool hdr,bool motionOutliers) {
    device_=device; width_=width; height_=height;hdr_=hdr;haveBefore_=false;motionOutliers_=motionOutliers;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=5;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=2;ranges[1].OffsetInDescriptorsFromTableStart=5;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={2,ranges};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants.Num32BitValues=9;
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=2;root.pParameters=parameters;
    Ptr<ID3DBlob> code,errors,signature;
    CheckHr(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"Serialize Echo guard");
    CheckHr(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_)),"Create Echo guard root");
    auto pipeline=[&](const char* entry,Ptr<ID3D12PipelineState>& target) {
        code.Reset();errors.Reset();
        const auto hr=D3DCompile(EchoGuardShaders,sizeof(EchoGuardShaders)-1,"EchoGuard",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        CheckHr(hr,"Compile Echo guard");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root_.Get();desc.CS={code->GetBufferPointer(),code->GetBufferSize()};
        CheckHr(device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&target)),"Create Echo guard pipeline");
    };
    pipeline("Mean",meanPipeline_);pipeline("main",guardPipeline_);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors=28;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&heap_)),"Create Echo guard views");
    stride_=device->GetDescriptorHandleIncrementSize(heap.Type);
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;properties.CreationNodeMask=properties.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=4;
    desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    CheckHr(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&mean_)),"Create guard mean");
    desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1;desc.Format=hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    CheckHr(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&before_)),"Create guard history");
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    CheckHr(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&scratch_)),"Create guard scratch");
}
void EchoGuard::Dispatch(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* motion,
    const std::array<ID3D12Resource*,3>& output,unsigned count,bool reset) {
    if (reset) haveBefore_=false;
    count=(std::min)(count,3u);
    if (haveBefore_ && count) {
        Transition(list,before_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto views=[&](unsigned pass,ID3D12Resource* made) {
            auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=static_cast<SIZE_T>(pass)*7*stride_;
            for (auto* texture : {made,before_.Get(),color,motion}) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=texture ? texture->GetDesc().Format : color->GetDesc().Format;
                srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                device_->CreateShaderResourceView(texture,&srv,cpu);cpu.ptr+=stride_;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=1;
            srv.Buffer.StructureByteStride=4;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            device_->CreateShaderResourceView(mean_.Get(),&srv,cpu);cpu.ptr+=stride_;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=scratch_->GetDesc().Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(scratch_.Get(),nullptr,&uav,cpu);cpu.ptr+=stride_;
            uav={};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=1;uav.Buffer.StructureByteStride=4;
            device_->CreateUnorderedAccessView(mean_.Get(),nullptr,&uav,cpu);
            auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=static_cast<UINT64>(pass)*7*stride_;
            list->SetComputeRootDescriptorTable(0,gpu);
        };
        ID3D12DescriptorHeap* heaps[]={heap_.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root_.Get());
        struct C {UINT w,h;int radius;float agree,apart,reserved;UINT encoding;float white,fraction;} c{
            width_,height_,static_cast<int>((std::max)(2u,width_/640)),16.f,15.f,motionOutliers_ ? 1.f : 0.f,hdr_ ? 2u : 0u,203.f,.5f};
        views(3,nullptr);list->SetComputeRoot32BitConstants(1,9,&c,0);list->SetPipelineState(meanPipeline_.Get());list->Dispatch(1,1,1);
        Transition(list,mean_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        for (unsigned i=0;i<count;++i) {
            Transition(list,output[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            views(i,output[i]);c.fraction=static_cast<float>(i+1)/static_cast<float>(count+1);
            list->SetComputeRoot32BitConstants(1,9,&c,0);list->SetPipelineState(guardPipeline_.Get());list->Dispatch((width_+7)/8,(height_+7)/8,1);
            Transition(list,scratch_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            Transition(list,output[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyResource(output[i],scratch_.Get());
            Transition(list,scratch_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(list,output[i],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        Transition(list,mean_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list,before_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    }
    Transition(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyResource(before_.Get(),color);
    Transition(list,color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    haveBefore_=true;
}
}
