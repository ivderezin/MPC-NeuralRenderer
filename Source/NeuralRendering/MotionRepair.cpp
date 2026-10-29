// SPDX-License-Identifier: GPL-3.0-or-later
#include "MotionRepair.h"
#include "MotionRepairShaders.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
namespace neural {
namespace {
void CheckRepair(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": HRESULT " +
        std::to_string(static_cast<unsigned long>(result)));
}
}
MotionRepair::Texture MotionRepair::MakeTexture(UINT width, UINT height, DXGI_FORMAT format) {
    Texture result;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.Format = format;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    CheckRepair(device_->CreateTexture2D(&desc, nullptr, &result.texture), "Create FG repair texture");
    CheckRepair(device_->CreateShaderResourceView(result.texture.Get(), nullptr, &result.srv), "Create FG repair SRV");
    CheckRepair(device_->CreateUnorderedAccessView(result.texture.Get(), nullptr, &result.uav), "Create FG repair UAV");
    return result;
}
void MotionRepair::Initialize(ID3D11Device* device, UINT width, UINT height) {
    if (!width || !height) throw std::invalid_argument("FG repair extent must be nonzero");
    if (device_.Get() != device) {
        const char* entries[] = {"Luma", "TileMode", "Decide", "Apply"};
        // Commit the device only after every shader is ready, so a failed compile can be retried.
        for (unsigned pass = 0; pass < shaders_.size(); ++pass) {
            Ptr<ID3DBlob> code, errors;
            const auto hr = D3DCompile(MotionRepairShader, sizeof(MotionRepairShader) - 1, "FG motion repair",
                nullptr, nullptr, entries[pass], "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0, &code, &errors);
            if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
            CheckRepair(hr, "Compile FG motion repair");
            shaders_[pass].Reset();
            CheckRepair(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                &shaders_[pass]), "Create FG repair shader");
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(Constants); desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constants_.Reset();
        CheckRepair(device->CreateBuffer(&desc, nullptr, &constants_), "Create FG repair constants");
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler_.Reset();
        CheckRepair(device->CreateSamplerState(&sampler, &sampler_), "Create FG repair sampler");
        device_ = device;
    }
    width_ = width; height_ = height;
    const UINT cell = width >= 1920 ? 4u : 2u, tile = width >= 1920 ? 64u : 32u;
    before_ = MakeTexture((width + cell - 1) / cell, (height + cell - 1) / cell, DXGI_FORMAT_R32_FLOAT);
    current_ = MakeTexture((width + cell - 1) / cell, (height + cell - 1) / cell, DXGI_FORMAT_R32_FLOAT);
    modes_ = MakeTexture((width + tile - 1) / tile, (height + tile - 1) / tile, DXGI_FORMAT_R16G16B16A16_FLOAT);
    cells_ = MakeTexture((width + cell - 1) / cell, (height + cell - 1) / cell, DXGI_FORMAT_R16G16B16A16_FLOAT);
}
void MotionRepair::Dispatch(ID3D11DeviceContext* context, ID3D11ShaderResourceView* before,
    ID3D11ShaderResourceView* current, ID3D11ShaderResourceView* motion,
    ID3D11ShaderResourceView* distrust, ID3D11UnorderedAccessView* output,
    ID3D11UnorderedAccessView* confidence, float fastMotion) {
    if (!width_ || !before || !current || !motion || !output)
        throw std::invalid_argument("FG repair requires initialized frames and separate motion input/output");
    const UINT cell = width_ >= 1920 ? 4u : 2u, tile = width_ >= 1920 ? 64u : 32u;
    const Constants c{width_, height_, (width_ + cell - 1) / cell, (height_ + cell - 1) / cell,
        (width_ + tile - 1) / tile, (height_ + tile - 1) / tile, tile, width_ >= 1920 ? 2u : 3u,
        0, fastMotion, cell, 1, width_, height_, 1, 7};
    context->UpdateSubresource(constants_.Get(), 0, nullptr, &c, 0, 0);
    ID3D11Buffer* buffer = constants_.Get();
    ID3D11SamplerState* sampler = sampler_.Get();
    context->CSSetConstantBuffers(0, 1, &buffer);
    context->CSSetSamplers(0, 1, &sampler);
    for (unsigned pass = 0; pass < shaders_.size(); ++pass) {
        ID3D11ShaderResourceView* inputs[8]{};
        ID3D11UnorderedAccessView* outputs[6]{};
        if (pass == 0) {
            inputs[0] = before; inputs[1] = current;
            outputs[0] = before_.uav.Get(); outputs[1] = current_.uav.Get();
        } else if (pass == 1) {
            inputs[2] = motion; inputs[3] = distrust; outputs[2] = modes_.uav.Get();
        } else if (pass == 2) {
            inputs[2] = motion; inputs[4] = before_.srv.Get(); inputs[5] = current_.srv.Get();
            inputs[6] = modes_.srv.Get(); outputs[3] = cells_.uav.Get();
        } else {
            inputs[2] = motion; inputs[3] = distrust; inputs[7] = cells_.srv.Get();
            outputs[4] = output; outputs[5] = confidence;
        }
        context->CSSetShader(shaders_[pass].Get(), nullptr, 0);
        context->CSSetShaderResources(0, 8, inputs);
        context->CSSetUnorderedAccessViews(0, 6, outputs, nullptr);
        if (pass == 1) context->Dispatch(c.tilesX, c.tilesY, 1); // one 256-thread group per tile
        else context->Dispatch(((pass == 3 ? width_ : c.cellWidth) + 7) / 8,
                               ((pass == 3 ? height_ : c.cellHeight) + 7) / 8, 1);
        for (auto& view : inputs) view = nullptr;
        for (auto& view : outputs) view = nullptr;
        context->CSSetShaderResources(0, 8, inputs);
        context->CSSetUnorderedAccessViews(0, 6, outputs, nullptr);
    }
    buffer = nullptr; sampler = nullptr;
    context->CSSetConstantBuffers(0, 1, &buffer);
    context->CSSetSamplers(0, 1, &sampler);
    context->CSSetShader(nullptr, nullptr, 0);
}
}
