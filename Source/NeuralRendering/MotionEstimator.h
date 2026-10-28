// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
namespace neural {
// D3D11 port of EchoStorm's coarse-to-fine estimator. Input: source-sized SDR RGB.
// Output: current-to-previous source-pixel displacement and distrust (0 reliable .. 1 unreliable).
class MotionEstimator {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Texture {
        Ptr<ID3D11Texture2D> texture;
        Ptr<ID3D11ShaderResourceView> srv;
        Ptr<ID3D11UnorderedAccessView> uav;
    };
    struct Level {
        UINT width = 0, height = 0, gridWidth = 0, gridHeight = 0;
        Texture luma[2], grid;
    };
    struct Constants {
        UINT width = 0, height = 0, gridWidth = 0, gridHeight = 0, coarseWidth = 0, coarseHeight = 0;
        UINT radius = 0, flags = 0;
        float lambda = .003f, bias = .004f, stability = 0, strayCap = 2, fastMotion = 0, meanWeight = 1, gradWeight = 0;
        UINT padding = 0;
    };
    static_assert(sizeof(Constants) == 64);
    enum Pass { Luma, Down, Global, Search, Median, Pixel, PassCount };
    Ptr<ID3D11Device> device_;
    std::array<Ptr<ID3D11ComputeShader>, PassCount> shaders_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;
    std::vector<Level> levels_;
    Texture filtered_, global_;
    UINT current_ = 0;
    bool havePrevious_ = false;
    Texture MakeTexture(UINT width, UINT height, DXGI_FORMAT format);
    void Run(ID3D11DeviceContext* context, Pass pass, const Constants& constants,
        const std::array<ID3D11ShaderResourceView*,4>& inputs, ID3D11UnorderedAccessView* output,
        UINT width, UINT height, ID3D11UnorderedAccessView* secondOutput = nullptr);
public:
    void Initialize(ID3D11Device* device, UINT width, UINT height);
    void Dispatch(ID3D11DeviceContext* context, ID3D11ShaderResourceView* frame,
        ID3D11UnorderedAccessView* motion, ID3D11UnorderedAccessView* distrust, bool reset);
    unsigned Levels() const { return static_cast<unsigned>(levels_.size()); }
};
}
