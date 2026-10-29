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
    void Create(ID3D12Device* device, unsigned width, unsigned height, bool hdr);
    void Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
                  const std::array<ID3D12Resource*,3>& output, unsigned count, bool reset);
    unsigned Collect() const;
};
}
