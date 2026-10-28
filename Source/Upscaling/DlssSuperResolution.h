// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Ngx/Feature.h"
#include "UpscalePolicy.h"
namespace upscaling {
class DlssSuperResolution : public ngx::Feature {
    Extent source_;
public:
    void Create(ID3D12GraphicsCommandList* list, Extent source, Extent target);
    NVSDK_NGX_Result Evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
        ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, bool reset, float frameTimeMs, ID3D12Resource* distrust = nullptr);
};
}
