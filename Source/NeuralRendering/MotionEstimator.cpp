// SPDX-License-Identifier: GPL-3.0-or-later
#include "MotionEstimator.h"
#include "EchoMotionShaders.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <stdexcept>
#include <string>
namespace neural {
namespace {
void CheckMotion(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation)+": HRESULT "+std::to_string(static_cast<unsigned long>(result)));
}
constexpr UINT GlobalRadius = 20;
}
MotionEstimator::Texture MotionEstimator::MakeTexture(UINT width, UINT height, DXGI_FORMAT format) {
    Texture result;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width; desc.Height=height; desc.Format=format;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    CheckMotion(device_->CreateTexture2D(&desc,nullptr,&result.texture),"Create motion texture");
    CheckMotion(device_->CreateShaderResourceView(result.texture.Get(),nullptr,&result.srv),"Create motion SRV");
    CheckMotion(device_->CreateUnorderedAccessView(result.texture.Get(),nullptr,&result.uav),"Create motion UAV");
    return result;
}
void MotionEstimator::Initialize(ID3D11Device* device, UINT width, UINT height) {
    if (!width || !height) throw std::invalid_argument("Motion extent must be nonzero");
    if (device_.Get()!=device) {
        device_=device;
        const char* sources[]={echo::Luma,echo::Down,echo::Global,echo::Search,echo::Median,echo::Pixel};
        for (unsigned pass=0;pass<PassCount;++pass) {
            const std::string source=std::string(echo::Common)+sources[pass];
            Ptr<ID3DBlob> code,errors;
            const auto hr=D3DCompile(source.data(),source.size(),"EchoMotion",nullptr,nullptr,"main","cs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
            CheckMotion(hr,"Compile EchoStorm motion");
            shaders_[pass].Reset();
            CheckMotion(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shaders_[pass]),"Create motion shader");
        }
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=sizeof(Constants); buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        constants_.Reset(); CheckMotion(device->CreateBuffer(&buffer,nullptr,&constants_),"Create motion constants");
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        sampler_.Reset(); CheckMotion(device->CreateSamplerState(&sampler,&sampler_),"Create motion sampler");
    }
    levels_.clear();
    // Always include half resolution, even for very small/odd frames; at most seven levels.
    for (;;) {
        Level level; level.width=width; level.height=height;
        level.gridWidth=(width+3)/4; level.gridHeight=(height+3)/4;
        for (auto& luma : level.luma) luma=MakeTexture(width,height,DXGI_FORMAT_R16_FLOAT);
        if (!levels_.empty()) level.grid=MakeTexture(level.gridWidth,level.gridHeight,DXGI_FORMAT_R16G16_FLOAT);
        levels_.push_back(std::move(level));
        const UINT nextWidth=(width+1)/2, nextHeight=(height+1)/2;
        if (levels_.size()>=7 || (levels_.size()>=2 && (nextWidth<64 || nextHeight<32))) break;
        width=nextWidth; height=nextHeight;
    }
    filtered_=MakeTexture(levels_[1].gridWidth,levels_[1].gridHeight,DXGI_FORMAT_R16G16_FLOAT);
    global_=MakeTexture(2*GlobalRadius+1,2*GlobalRadius+1,DXGI_FORMAT_R32_FLOAT);
    current_=0; havePrevious_=false;
}
void MotionEstimator::Run(ID3D11DeviceContext* context, Pass pass, const Constants& constants,
    const std::array<ID3D11ShaderResourceView*,4>& inputs, ID3D11UnorderedAccessView* output,
    UINT width, UINT height, ID3D11UnorderedAccessView* secondOutput) {
    context->UpdateSubresource(constants_.Get(),0,nullptr,&constants,0,0);
    ID3D11Buffer* buffers[]={constants_.Get()};
    ID3D11SamplerState* samplers[]={sampler_.Get()};
    ID3D11UnorderedAccessView* outputs[]={output,secondOutput};
    context->CSSetConstantBuffers(0,1,buffers);
    context->CSSetSamplers(0,1,samplers);
    context->CSSetShader(shaders_[pass].Get(),nullptr,0);
    context->CSSetShaderResources(0,static_cast<UINT>(inputs.size()),inputs.data());
    context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);
    context->Dispatch((width+7)/8,(height+7)/8,1);
    const std::array<ID3D11ShaderResourceView*,4> empty{};
    outputs[0]=outputs[1]=nullptr;
    context->CSSetShaderResources(0,static_cast<UINT>(empty.size()),empty.data());
    context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);
}
void MotionEstimator::Dispatch(ID3D11DeviceContext* context, ID3D11ShaderResourceView* frame,
    ID3D11UnorderedAccessView* motion, ID3D11UnorderedAccessView* distrust, bool reset) {
    if (levels_.empty()) throw std::logic_error("Motion estimator is not initialized");
    if (reset) havePrevious_=false;
    const UINT previous=1-current_;
    const auto& full=levels_[0];
    Constants c; c.width=full.width; c.height=full.height;
    Run(context,Luma,c,{frame,nullptr,nullptr,nullptr},full.luma[current_].uav.Get(),c.width,c.height);
    for (size_t k=1;k<levels_.size();++k) {
        const auto& level=levels_[k];
        c={}; c.width=level.width; c.height=level.height;
        c.gridWidth=levels_[k-1].width; c.gridHeight=levels_[k-1].height;
        Run(context,Down,c,{levels_[k-1].luma[current_].srv.Get(),nullptr,nullptr,nullptr},level.luma[current_].uav.Get(),c.width,c.height);
    }
    if (havePrevious_) {
        const auto& top=levels_.back();
        c={}; c.width=top.width; c.height=top.height; c.radius=GlobalRadius;
        Run(context,Global,c,{top.luma[current_].srv.Get(),top.luma[previous].srv.Get(),nullptr,nullptr},global_.uav.Get(),2*GlobalRadius+1,2*GlobalRadius+1);
        for (size_t k=levels_.size()-1;k>0;--k) {
            const auto& level=levels_[k]; const bool coarser=k+1<levels_.size();
            c={}; c.width=level.width; c.height=level.height; c.gridWidth=level.gridWidth; c.gridHeight=level.gridHeight;
            c.coarseWidth=coarser ? levels_[k+1].gridWidth : GlobalRadius;
            c.coarseHeight=coarser ? levels_[k+1].gridHeight : 0;
            c.radius=coarser ? 2u : 4u; c.flags=(coarser ? 1u : 4u)|(k==1 ? 2u : 0u);
            Run(context,Search,c,{level.luma[current_].srv.Get(),level.luma[previous].srv.Get(),
                coarser ? levels_[k+1].grid.srv.Get() : nullptr, coarser ? nullptr : global_.srv.Get()},level.grid.uav.Get(),c.gridWidth,c.gridHeight);
        }
        c={}; c.width=levels_[1].gridWidth; c.height=levels_[1].gridHeight;
        Run(context,Median,c,{levels_[1].grid.srv.Get(),nullptr,nullptr,nullptr},filtered_.uav.Get(),c.width,c.height);
    }
    c={}; c.width=full.width; c.height=full.height;
    c.gridWidth=levels_[1].gridWidth; c.gridHeight=levels_[1].gridHeight; c.flags=havePrevious_ ? 1u : 0u;
    Run(context,Pixel,c,{full.luma[current_].srv.Get(),full.luma[previous].srv.Get(),filtered_.srv.Get(),nullptr},motion,c.width,c.height,distrust);
    ID3D11Buffer* buffer=nullptr; ID3D11SamplerState* sampler=nullptr;
    context->CSSetConstantBuffers(0,1,&buffer); context->CSSetSamplers(0,1,&sampler); context->CSSetShader(nullptr,nullptr,0);
    current_=previous; havePrevious_=true;
}
}
