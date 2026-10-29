// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <stdexcept>
namespace framegen {
// Full RGB compare (including one-pixel changes), not the sampled diagnostic.
// Read only after the existing interop completion fence; fail open on readback errors.
class ExactInputRepeat {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11ComputeShader> shader_;
    Ptr<ID3D11Buffer> flag_,readback_,constants_;
    Ptr<ID3D11UnorderedAccessView> flagView_;
    Ptr<ID3D11Texture2D> previous_;
    Ptr<ID3D11ShaderResourceView> previousView_;
    bool valid_=false,pending_=false;
    static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Exact input comparison failed"); }
public:
    void Invalidate() { valid_=pending_=false; }
    void Record(ID3D11DeviceContext* context,ID3D11Texture2D* source,ID3D11ShaderResourceView* view,
                const RECT& rect,bool reset) {
        if (!device_) {
            context->GetDevice(&device_);
            static constexpr char hlsl[]=R"(
Texture2D<float4> current : register(t0);
Texture2D<float4> previous : register(t1);
RWStructuredBuffer<uint> different : register(u0);
cbuffer C : register(b0) { uint2 size; uint2 origin; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    if (any(p.xy>=size)) return;
    const float3 a=current.Load(int3(p.xy+origin,0)).rgb;
    const float3 b=previous.Load(int3(p.xy+origin,0)).rgb;
    if (any(a!=b) || !all(isfinite(a)) || !all(isfinite(b))) InterlockedOr(different[0],1);
}
)";
            Ptr<ID3DBlob> code,errors;
            Check(D3DCompile(hlsl,sizeof(hlsl)-1,"ExactInputRepeat",nullptr,nullptr,"main","cs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors));
            Check(device_->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader_));
            D3D11_BUFFER_DESC d{}; d.ByteWidth=4; d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
            d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride=4;
            Check(device_->CreateBuffer(&d,nullptr,&flag_));
            Check(device_->CreateUnorderedAccessView(flag_.Get(),nullptr,&flagView_));
            d.BindFlags=d.MiscFlags=d.StructureByteStride=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            Check(device_->CreateBuffer(&d,nullptr,&readback_));
            d={};d.ByteWidth=16;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            Check(device_->CreateBuffer(&d,nullptr,&constants_));
        }
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        if (!previous_) {
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=desc.CPUAccessFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
            Check(device_->CreateTexture2D(&desc,nullptr,&previous_));
            Check(device_->CreateShaderResourceView(previous_.Get(),nullptr,&previousView_));
        }
        const UINT initial=reset || !valid_ ? 1u : 0u;
        context->UpdateSubresource(flag_.Get(),0,nullptr,&initial,0,0);
        if (!initial) {
            const UINT data[]={static_cast<UINT>(rect.right-rect.left),static_cast<UINT>(rect.bottom-rect.top),
                static_cast<UINT>(rect.left),static_cast<UINT>(rect.top)};
            context->UpdateSubresource(constants_.Get(),0,nullptr,data,0,0);
            ID3D11Buffer* cb=constants_.Get();
            ID3D11ShaderResourceView* inputs[]={view,previousView_.Get()};
            ID3D11UnorderedAccessView* out=flagView_.Get();
            context->CSSetShader(shader_.Get(),nullptr,0);context->CSSetConstantBuffers(0,1,&cb);
            context->CSSetShaderResources(0,2,inputs);context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
            context->Dispatch((data[0]+7)/8,(data[1]+7)/8,1);
            inputs[0]=inputs[1]=nullptr;out=nullptr;cb=nullptr;
            context->CSSetShaderResources(0,2,inputs);context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
            context->CSSetConstantBuffers(0,1,&cb);context->CSSetShader(nullptr,nullptr,0);
        }
        context->CopyResource(readback_.Get(),flag_.Get());
        context->CopyResource(previous_.Get(),source);
        valid_=pending_=true;
    }
    bool Same(ID3D11DeviceContext* context) {
        if (!pending_) return false;
        pending_=false;
        D3D11_MAPPED_SUBRESOURCE data{};
        if (FAILED(context->Map(readback_.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&data))) return false;
        const bool same=*static_cast<const UINT*>(data.pData)==0;
        context->Unmap(readback_.Get(),0);return same;
    }
};
}
