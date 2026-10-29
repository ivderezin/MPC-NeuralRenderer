// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
namespace framegen {
class EchoGuard {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D12Device> device_;
    Ptr<ID3D12RootSignature> root_;
    Ptr<ID3D12PipelineState> meanPipeline_,guardPipeline_;
    Ptr<ID3D12DescriptorHeap> heap_;
    Ptr<ID3D12Resource> mean_,before_,scratch_;
    UINT stride_=0,width_=0,height_=0;
    bool hdr_=false,haveBefore_=false,motionOutliers_=false;
public:
    void Create(ID3D12Device* device,UINT width,UINT height,bool hdr,bool motionOutliers=false);
    void Dispatch(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* motion,
                  const std::array<ID3D12Resource*,3>& output,unsigned count,bool reset);
};
}
