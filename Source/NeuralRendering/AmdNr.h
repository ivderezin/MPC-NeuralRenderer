// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <string>
namespace neural {
class AmdNr {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    AmdNr();
    ~AmdNr();
    // All operations are serialized by the owning render pipeline. Complete its
    // GPU submissions before Reset/destruction, as required by the external API.
    void Create(ID3D12Device* device,const std::filesystem::path& directory);
    void Reset();
    bool Process(ID3D12GraphicsCommandList* list,ID3D12Resource* input,ID3D12Resource* output,
                 ID3D12Resource* motion,ID3D12Resource* flatDepth,unsigned width,unsigned height,int intensity,bool reset);
    void Submitted(ID3D12CommandQueue* queue);
    std::wstring Status() const;
};
}
