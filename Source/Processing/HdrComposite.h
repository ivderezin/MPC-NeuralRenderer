// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HdrShaders.h"
#include "RuntimeLibrary.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
namespace processing {
class HdrComposite {
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    UINT stride_ = 0;
public:
    void Create(ID3D12Device* device) {
        device_=device;
        Microsoft::WRL::ComPtr<ID3DBlob> code,errors,signature;
        const auto hr=D3DCompile(HdrCompositeShader.data(),HdrCompositeShader.size(),"HdrComposite",nullptr,nullptr,
            "main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        CheckHr(hr,"Compile HDR composite");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors=3;
        ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors=1;
        ranges[1].OffsetInDescriptorsFromTableStart=3;
        D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable={2,ranges};
        D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters=1; root.pParameters=&parameter;
        CheckHr(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"Serialize HDR signature");
        CheckHr(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_)),"Create HDR signature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{}; pso.pRootSignature=root_.Get();
        pso.CS={code->GetBufferPointer(),code->GetBufferSize()};
        CheckHr(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipeline_)),"Create HDR pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors=4; heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CheckHr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&heap_)),"Create HDR descriptors");
        stride_=device->GetDescriptorHandleIncrementSize(heap.Type);
    }
    void Dispatch(ID3D12GraphicsCommandList* list,ID3D12Resource* enhanced,ID3D12Resource* original,
                  ID3D12Resource* proxy,ID3D12Resource* output) {
        auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();
        for (auto* source : {enhanced,original,proxy}) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=source->GetDesc().Format;
            srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels=1; device_->CreateShaderResourceView(source,&srv,cpu); cpu.ptr+=stride_;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format=output->GetDesc().Format;
        uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; device_->CreateUnorderedAccessView(output,nullptr,&uav,cpu);
        ID3D12DescriptorHeap* heaps[]={heap_.Get()}; list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(root_.Get()); list->SetPipelineState(pipeline_.Get());
        list->SetComputeRootDescriptorTable(0,heap_->GetGPUDescriptorHandleForHeapStart());
        const auto size=output->GetDesc(); list->Dispatch((static_cast<UINT>(size.Width)+7)/8,(size.Height+7)/8,1);
    }
};
}
