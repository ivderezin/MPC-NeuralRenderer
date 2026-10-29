// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Ngx/Feature.h"
#include <wrl/client.h>
#include <array>
#include <optional>
#include "DlssSampleCapture.h"
namespace framegen {
// Explicit replay-only experiments. Unset in live playback.
struct DlssReplayControls {
    float motionScaleX=1, motionScaleY=1;
    bool hudlessColor=false, video=false;
    ID3D12Resource* emptyUi=nullptr; // Caller retains through GPU completion.
};
class DlssMultiFrameGeneration : public ngx::Feature {
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,3> disabled_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_, flagClearValue_;
    unsigned inputWidth_ = 0, inputHeight_ = 0, outputWidth_ = 0, outputHeight_ = 0, maxGenerated_ = 1;
    unsigned pendingCount_ = 0;
    bool pendingReset_ = true, hdr_ = false, motionVectorsDilated_ = false;
    std::string guidanceInfo_ = "unspecified";
    unsigned long long frameId_ = 0;
    std::wstring runtimeStatus_;
    DlssSampleCapture capture_;
    std::optional<DlssReplayControls> replay_;
public:
    void Create(ID3D12Device* device, ID3D12GraphicsCommandList* list, unsigned inputWidth, unsigned inputHeight,
        unsigned outputWidth, unsigned outputHeight, bool hdr = false);
    // Diagnostic overrides; production retains the SDK preprocessing default.
    void SetMotionVectorsDilated(bool value) { motionVectorsDilated_ = value; }
    void SetReplayControls(const DlssReplayControls& value) { replay_=value; }
    void SetGuidanceInfo(const std::string& value) { guidanceInfo_ = value; }
    unsigned MaxGenerated() const { return maxGenerated_; }
    NVSDK_NGX_Result Evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth,
        ID3D12Resource* motion, const std::array<ID3D12Resource*,3>& output, unsigned count, bool reset);
    unsigned Collect();
    std::wstring RuntimeStatus() const { return runtimeStatus_+capture_.Status(); }
};
}
