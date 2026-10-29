// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <memory>
#include <array>
namespace framegen {
class EchoFrameGeneration {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    EchoFrameGeneration();
    ~EchoFrameGeneration();
    void Create(ID3D12Device* device, unsigned width, unsigned height, bool hdr, bool motionOnly = false, DXGI_FORMAT inputFormat = DXGI_FORMAT_UNKNOWN);
    void Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
                  const std::array<ID3D12Resource*,3>& output, unsigned count, bool reset);
    // Motion-only mode runs the same native estimator/repair on final FG colours.
    // The result is output-size, in output pixels, and left in UAV state.
    ID3D12Resource* Motion() const;
    // Export native repaired vectors and an SR mask on the same D3D12 list.
    // Destinations enter/leave NON_PIXEL_SHADER_RESOURCE. At most four exports
    // per Dispatch; the caller completes the batch before reusing descriptors.
    void WriteGuidance(ID3D12GraphicsCommandList* list, ID3D12Resource* motion,
                       ID3D12Resource* confidence = nullptr, float fastMotion = 0);
    unsigned Collect() const;
};
}
