// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UpscalePolicy.h"
#include "../Processing/Providers.h"
#include <d3d12.h>
#include <memory>
#include <filesystem>
namespace upscaling {
class UpscaleStage {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    UpscaleStage();
    ~UpscaleStage();
    void Reset();
    void Create(processing::Upscaler provider, ID3D12Device* device, ID3D12GraphicsCommandList* list, Extent source,
                Extent target, const std::filesystem::path& directory, bool motionConfidence = false);
    void Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motion,
                  ID3D12Resource* output, bool reset, float frameTimeMs, ID3D12Resource* distrust = nullptr);
};
}
