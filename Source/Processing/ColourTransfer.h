// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
namespace processing {
class ColourTransfer {
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    unsigned stride_ = 0;

  public:
    void Create(ID3D12Device* device);
    void Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* source, ID3D12Resource* target, bool encode);
    static Microsoft::WRL::ComPtr<ID3D12Resource> LinearTexture(ID3D12Device* device, unsigned width, unsigned height);
};
}
