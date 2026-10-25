// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColourTransfer.h"
#ifdef _WIN64
#include "RuntimeLibrary.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
namespace processing {
using Microsoft::WRL::ComPtr;
void ColourTransfer::Create(ID3D12Device* device) {
    device_ = device;
    const char shader[] = R"(
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);
cbuffer Params : register(b0) { uint encode; };
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    uint w,h; target.GetDimensions(w,h); if (id.x>=w || id.y>=h) return;
    float3 c=max(source.Load(int3(id.xy,0)).rgb,0);
    if (encode) c=float3(c.r<=.0031308 ? 12.92*c.r : 1.055*pow(c.r,1/2.4)-.055,
                        c.g<=.0031308 ? 12.92*c.g : 1.055*pow(c.g,1/2.4)-.055,
                        c.b<=.0031308 ? 12.92*c.b : 1.055*pow(c.b,1/2.4)-.055);
    else c=float3(c.r<=.04045 ? c.r/12.92 : pow((c.r+.055)/1.055,2.4),
                  c.g<=.04045 ? c.g/12.92 : pow((c.g+.055)/1.055,2.4),
                  c.b<=.04045 ? c.b/12.92 : pow((c.b+.055)/1.055,2.4));
    target[id.xy]=float4(c,1);
})";
    ComPtr<ID3DBlob> code, error, signature;
    const auto result = D3DCompile(shader, sizeof(shader) - 1, "ColourTransfer", nullptr, nullptr, "main", "cs_5_0",
                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(result) && error)
        throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
    CheckHr(result, "Compile colour transfer");
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable = {2, ranges};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = 1;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    CheckHr(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error),
            "Serialize colour root signature");
    CheckHr(
        device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_)),
        "Create colour root signature");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_.Get();
    pso.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    CheckHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline_)), "Create colour pipeline");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 4;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHr(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&heap_)), "Create colour descriptors");
    stride_ = device->GetDescriptorHandleIncrementSize(heap.Type);
}
void ColourTransfer::Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* source, ID3D12Resource* target,
                              bool encode) {
    // Decode and encode use distinct descriptor slots because both dispatches may be in flight together.
    const unsigned slot = encode ? 2 : 0;
    auto cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += slot * stride_;
    auto gpu = heap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += slot * stride_;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = source->GetDesc().Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(source, &srv, cpu);
    cpu.ptr += stride_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = target->GetDesc().Format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(target, nullptr, &uav, cpu);
    ID3D12DescriptorHeap* heaps[] = {heap_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root_.Get());
    list->SetPipelineState(pipeline_.Get());
    list->SetComputeRootDescriptorTable(0, gpu);
    list->SetComputeRoot32BitConstant(1, encode ? 1 : 0, 0);
    const auto size = target->GetDesc();
    list->Dispatch((static_cast<UINT>(size.Width) + 7) / 8, (size.Height + 7) / 8, 1);
}
ComPtr<ID3D12Resource> ColourTransfer::LinearTexture(ID3D12Device* device, unsigned width, unsigned height) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> texture;
    CheckHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            IID_PPV_ARGS(&texture)),
            "Create linear texture");
    return texture;
}
}

#endif
