// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Processing/Providers.h"
#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <array>
namespace framegen {
class GenerationStage {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    GenerationStage();
    ~GenerationStage();
    void Reset();
    void Create(processing::FrameGenerator provider, ID3D12Device* device, ID3D12GraphicsCommandList* list,
                unsigned width, unsigned height, unsigned outputWidth, unsigned outputHeight,
                const std::filesystem::path& directory, bool hdr = false, bool nativeEchoMotion = false, bool dlssGuard = true);
    unsigned MaxGenerated() const;
    void Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motion,
                  const std::array<ID3D12Resource*, 3>& output, unsigned count, bool reset, float frameTimeMs);
    void SetDlssGuard(bool enabled);
    unsigned Collect();
    std::wstring RuntimeStatus() const;
};
}
