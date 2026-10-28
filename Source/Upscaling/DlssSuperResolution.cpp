// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN64
#include "DlssSuperResolution.h"
#include <nvsdk_ngx_helpers_d3d.h>
#include <algorithm>
namespace upscaling {
void DlssSuperResolution::Create(ID3D12GraphicsCommandList* list, Extent source, Extent target) {
    Allocate(); source_ = source;
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = source.width; create.Feature.InHeight = source.height;
    create.Feature.InTargetWidth = target.width; create.Feature.InTargetHeight = target.height;
    const double ratio = (std::min)(static_cast<double>(source.width)/target.width,static_cast<double>(source.height)/target.height);
    create.Feature.InPerfQualityValue = ratio >= 0.65 ? NVSDK_NGX_PerfQuality_Value_MaxQuality
        : ratio >= 0.57 ? NVSDK_NGX_PerfQuality_Value_Balanced
        : ratio >= 0.45 ? NVSDK_NGX_PerfQuality_Value_MaxPerf : NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted
        | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    ngx::Check(NGX_D3D12_CREATE_DLSS_EXT(list, 1, 1, &handle_, params_, &create), "Create DLSS SR");
    if (!handle_) throw std::runtime_error("DLSS SR returned no handle");
}
NVSDK_NGX_Result DlssSuperResolution::Evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
    ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, bool reset, float frameTimeMs, ID3D12Resource* distrust) {
    NVSDK_NGX_D3D12_DLSS_Eval_Params evaluate{};
    evaluate.Feature.pInColor = color; evaluate.Feature.pInOutput = output;
    evaluate.pInBiasCurrentColorMask = distrust;
    evaluate.pInDepth = depth; evaluate.pInMotionVectors = motion;
    evaluate.InRenderSubrectDimensions = {source_.width, source_.height};
    evaluate.InMVScaleX = evaluate.InMVScaleY = 1;
    evaluate.InReset = reset ? 1 : 0;
    evaluate.InFrameTimeDeltaInMsec = frameTimeMs;
    evaluate.InPreExposure = evaluate.InExposureScale = 1;
    return NGX_D3D12_EVALUATE_DLSS_EXT(list, handle_, params_, &evaluate);
}
}
#endif
