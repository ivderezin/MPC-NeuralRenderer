// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
namespace neural {
// EchoStorm's local consensus resolves ambiguous flat/repeating patches.
// The input field is never overwritten; callers choose the repaired or original view.
class MotionRepair {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Texture {
        Ptr<ID3D11Texture2D> texture;
        Ptr<ID3D11ShaderResourceView> srv;
        Ptr<ID3D11UnorderedAccessView> uav;
    };
    struct Constants {
        UINT width, height, cellWidth, cellHeight, tilesX, tilesY, tile, reach;
        UINT encoding = 0;
        float fastMotion = 0;
        UINT cell, scale = 1, fullWidth, fullHeight, enabled = 1, rules = 7;
    };
    static_assert(sizeof(Constants) == 64);
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;
    std::array<Ptr<ID3D11ComputeShader>, 4> shaders_;
    Texture before_, current_, modes_, cells_;
    UINT width_ = 0, height_ = 0;
    Texture MakeTexture(UINT width, UINT height, DXGI_FORMAT format);
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height);
    // Both frames are SDR RGB; vectors are current-to-previous in source pixels.
    // A null distrust view is allowed for the fast block matcher.
    void Dispatch(ID3D11DeviceContext* context, ID3D11ShaderResourceView* before,
                  ID3D11ShaderResourceView* current, ID3D11ShaderResourceView* motion,
                  ID3D11ShaderResourceView* distrust, ID3D11UnorderedAccessView* output,
                  ID3D11UnorderedAccessView* confidence = nullptr, float fastMotion = 0);
};
}
