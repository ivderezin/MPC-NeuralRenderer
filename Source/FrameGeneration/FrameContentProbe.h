// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <string>
namespace framegen {
// Diagnostic only: sampled RGB differences never decide whether to skip a frame.
// Readback is polled without flushing/waiting; an outstanding probe is not reused.
class FrameContentProbe {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11ComputeShader> shader_;
    Ptr<ID3D11Buffer> result_, readback_;
    Ptr<ID3D11UnorderedAccessView> resultView_;
    Ptr<ID3D11Query> ready_;
    Ptr<ID3D11Texture2D> previousRaw_, previousReal_;
    bool havePrevious_ = false, pending_ = false, validPending_ = false;
    unsigned pendingCount_ = 0;
    static void Check(HRESULT hr) {
        if (FAILED(hr)) throw std::runtime_error("Frame content probe: " + std::to_string(static_cast<unsigned long>(hr)));
    }
    void Initialize(ID3D11Device* device) {
        static constexpr char source[] = R"(
Texture2D<float4> tRaw : register(t0);
Texture2D<float4> tPreviousRaw : register(t1);
Texture2D<float4> tReal : register(t2);
Texture2D<float4> tPreviousReal : register(t3);
Texture2D<float4> tG0 : register(t4);
Texture2D<float4> tG1 : register(t5);
Texture2D<float4> tG2 : register(t6);
RWStructuredBuffer<float> result : register(u0);
groupshared float parts[256];
[numthreads(256,1,1)] void main(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    uint rw,rh,w,h; tRaw.GetDimensions(rw,rh); tReal.GetDimensions(w,h);
    float sum=0;
    for (uint i=tid;i<2304;i+=256) {
        const uint2 grid=uint2(i%64,i/64);
        const float2 uv=(float2(grid)+0.5)/float2(64,36);
        const int2 rawAt=min(uint2(uv*float2(rw,rh)),uint2(rw-1,rh-1));
        const int2 at=min(uint2(uv*float2(w,h)),uint2(w-1,h-1));
        float3 a,b;
        if (group.x==0) { a=tRaw.Load(int3(rawAt,0)).rgb; b=tPreviousRaw.Load(int3(rawAt,0)).rgb; }
        else if (group.x==1) { a=tReal.Load(int3(at,0)).rgb; b=tPreviousReal.Load(int3(at,0)).rgb; }
        else {
            if (group.x<4) a=tG0.Load(int3(at,0)).rgb;
            else if (group.x<6) a=tG1.Load(int3(at,0)).rgb;
            else a=tG2.Load(int3(at,0)).rgb;
            b=(group.x&1) ? tPreviousReal.Load(int3(at,0)).rgb : tReal.Load(int3(at,0)).rgb;
        }
        sum+=all(isfinite(a)) && all(isfinite(b)) ? dot(abs(a-b),float3(85,85,85)) : 255;
    }
    parts[tid]=sum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride=128;stride>0;stride>>=1) {
        if (tid<stride) parts[tid]+=parts[tid+stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid==0) result[group.x]=parts[0]/2304.0;
}
)";
        Ptr<ID3DBlob> code, errors;
        const auto hr = D3DCompile(source, sizeof(source)-1, "FrameContentProbe", nullptr, nullptr, "main", "cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr);
        Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader_));
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=8*sizeof(float); desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride=sizeof(float);
        Check(device->CreateBuffer(&desc,nullptr,&result_));
        Check(device->CreateUnorderedAccessView(result_.Get(),nullptr,&resultView_));
        desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;
        desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateBuffer(&desc,nullptr,&readback_));
        D3D11_QUERY_DESC query{}; query.Query=D3D11_QUERY_EVENT;
        Check(device->CreateQuery(&query,&ready_));
        device_=device;
    }
    bool EnsureCopy(ID3D11Texture2D* source, Ptr<ID3D11Texture2D>& copy) {
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        if (copy) {
            D3D11_TEXTURE2D_DESC old{}; copy->GetDesc(&old);
            if (desc.Width==old.Width && desc.Height==old.Height && desc.Format==old.Format) return false;
            copy.Reset();
        }
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags=desc.CPUAccessFlags=0; desc.Usage=D3D11_USAGE_DEFAULT;
        Check(device_->CreateTexture2D(&desc,nullptr,&copy));
        return true;
    }
public:
    struct Metrics {
        // RGB code-value deltas in 0..255 units: raw step, real step,
        // then each generated frame versus current and previous real endpoints.
        std::array<float,8> delta{};
        unsigned generated=0;
        unsigned long long samples=0, unchangedInputs=0;
    } metrics;
    void Invalidate() { havePrevious_=validPending_=false; metrics={}; }
    bool Pending() const { return pending_; }
    bool Collect(ID3D11DeviceContext* context) {
        if (!pending_) return false;
        BOOL done=FALSE;
        const auto hr=context->GetData(ready_.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr==S_FALSE || (hr==S_OK && !done)) return false;
        Check(hr);
        D3D11_MAPPED_SUBRESOURCE map{};
        const auto mapped=context->Map(readback_.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
        if (mapped==DXGI_ERROR_WAS_STILL_DRAWING) return false;
        Check(mapped);
        if (validPending_) {
            const auto* values=static_cast<const float*>(map.pData);
            for (unsigned i=0;i<8;++i) metrics.delta[i]=values[i];
            metrics.generated=pendingCount_; ++metrics.samples;
            if (metrics.delta[0]==0) ++metrics.unchangedInputs;
        }
        context->Unmap(readback_.Get(),0);
        pending_=false;
        return validPending_;
    }
    void Submit(ID3D11DeviceContext* context, ID3D11Texture2D* raw, ID3D11Texture2D* real,
                const std::array<ID3D11Texture2D*,3>& generated, unsigned count) {
        Collect(context);
        if (pending_) return;
        Ptr<ID3D11Device> device; context->GetDevice(&device);
        if (!device_) Initialize(device.Get());
        const bool rawChanged=EnsureCopy(raw,previousRaw_), realChanged=EnsureCopy(real,previousReal_);
        if (rawChanged || realChanged) Invalidate();
        if (havePrevious_) {
            const std::array<ID3D11Texture2D*,7> textures={raw,previousRaw_.Get(),real,previousReal_.Get(),
                generated[0],generated[1],generated[2]};
            std::array<Ptr<ID3D11ShaderResourceView>,7> views;
            std::array<ID3D11ShaderResourceView*,7> inputs{};
            for (unsigned i=0;i<inputs.size();++i) if (textures[i]) {
                Check(device_->CreateShaderResourceView(textures[i],nullptr,&views[i])); inputs[i]=views[i].Get();
            }
            ID3D11UnorderedAccessView* out=resultView_.Get();
            context->CSSetShader(shader_.Get(),nullptr,0);
            context->CSSetShaderResources(0,7,inputs.data());
            context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
            context->Dispatch(8,1,1);
            inputs.fill(nullptr); out=nullptr;
            context->CSSetShaderResources(0,7,inputs.data());
            context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
            context->CSSetShader(nullptr,nullptr,0);
            context->CopyResource(readback_.Get(),result_.Get());
            context->End(ready_.Get());
            pending_=validPending_=true; pendingCount_=(std::min)(count,3u);
        }
        context->CopyResource(previousRaw_.Get(),raw);
        context->CopyResource(previousReal_.Get(),real);
        havePrevious_=true;
    }
};
}
