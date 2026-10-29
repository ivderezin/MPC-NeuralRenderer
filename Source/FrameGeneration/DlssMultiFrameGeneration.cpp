// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN64
#include "DlssMultiFrameGeneration.h"
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <algorithm>
#include <limits>
#include <cstring>
#include <sstream>
#include "DlssCaptureCamera.h"
#include "../Processing/LoadedModules.h"
namespace framegen {
namespace {
Microsoft::WRL::ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, D3D12_HEAP_TYPE type, unsigned size, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags = flags;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    ngx::CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)), "Create MFG buffer");
    return resource;
}
}
void DlssMultiFrameGeneration::Create(ID3D12Device* device, ID3D12GraphicsCommandList* list,
    unsigned inputWidth, unsigned inputHeight, unsigned outputWidth, unsigned outputHeight, bool hdr) {
    Allocate();
    hdr_ = hdr;
    frameId_ = 0; pendingCount_ = 0; pendingReset_ = true;
    inputWidth_ = inputWidth; inputHeight_ = inputHeight;
    outputWidth_ = outputWidth; outputHeight_ = outputHeight;
    NVSDK_NGX_Parameter* capabilities = nullptr;
    ngx::Check(NVSDK_NGX_D3D12_GetCapabilityParameters(&capabilities), "Query MFG capabilities");
    unsigned maximum = 1;
    if (capabilities) capabilities->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &maximum);
    maxGenerated_ = std::clamp(maximum, 1u, 3u);
    NVSDK_NGX_DLSSG_Create_Params create{};
    create.Width = outputWidth; create.Height = outputHeight;
    create.RenderWidth = inputWidth; create.RenderHeight = inputHeight;
    create.NativeBackbufferFormat = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    ngx::Check(NGX_D3D12_CREATE_DLSSG(list, 1, 1, &handle_, params_, &create), "Create DLSS MFG");
    if (!handle_) throw std::runtime_error("DLSS MFG returned no handle");
    runtimeStatus_=processing::LoadedDlssgStatus();
    maximum = maxGenerated_;
    if (params_->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &maximum) == NVSDK_NGX_Result_Success)
        maxGenerated_ = std::clamp(maximum, 1u, 3u);
    readback_ = Buffer(device,D3D12_HEAP_TYPE_READBACK,12,D3D12_RESOURCE_STATE_COPY_DEST);
    flagClearValue_ = Buffer(device,D3D12_HEAP_TYPE_UPLOAD,4,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* data = nullptr;
    const D3D12_RANGE empty{0,0};
    ngx::CheckHr(flagClearValue_->Map(0,&empty,&data),"Initialize MFG validity flag");
    *static_cast<unsigned*>(data) = 0;
    flagClearValue_->Unmap(0,nullptr);
    for (auto& flag : disabled_) flag = Buffer(device,D3D12_HEAP_TYPE_DEFAULT,4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}
NVSDK_NGX_Result DlssMultiFrameGeneration::Evaluate(ID3D12GraphicsCommandList* list, ID3D12Resource* color,
    ID3D12Resource* depth, ID3D12Resource* motion, const std::array<ID3D12Resource*,3>& output, unsigned count, bool reset) {
    count = std::clamp(count,1u,maxGenerated_);
    pendingCount_ = count; pendingReset_ = reset;
    params_->Set(NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, ++frameId_);
    NVSDK_NGX_DLSSG_Opt_Eval_Params options{};
    options.multiFrameCount = count;
    options.colorBuffersHDR = hdr_;
    options.motionVectorsDilated = motionVectorsDilated_;
    options.mvecScale[0] = 1.0f / inputWidth_; options.mvecScale[1] = 1.0f / inputHeight_;
    options.cameraUp[1] = options.cameraRight[0] = options.cameraFwd[2] = 1;
    options.cameraNear = DlssCaptureCamera::Near; options.cameraFar = DlssCaptureCamera::Far; options.cameraFOV = 1.04719755f;
    options.cameraAspectRatio = static_cast<float>(outputWidth_) / outputHeight_;
    options.depthInverted = true; options.cameraMotionIncluded = true;
    options.orthoProjection = true; options.reset = reset;
    options.motionVectorsInvalidValue = (std::numeric_limits<float>::max)();
    for (unsigned i=0;i<4;++i) {
        options.clipToLensClip[i][i] = 1;
        options.clipToPrevClip[i][i] = options.prevClipToClip[i][i] = 1;
    }
    const DlssCaptureCamera camera(options.cameraAspectRatio);
    std::memcpy(options.cameraViewToClip,camera.viewToClip.data(),sizeof(options.cameraViewToClip));
    std::memcpy(options.clipToCameraView,camera.clipToView.data(),sizeof(options.clipToCameraView));
    options.mvecsSubrectSize = options.depthSubrectSize = {inputWidth_,inputHeight_};
    options.backbufferSubrectSize = options.outputInterpSubrectSize = {outputWidth_,outputHeight_};
    if(replay_) {
        options.mvecScale[0]=replay_->motionScaleX;options.mvecScale[1]=replay_->motionScaleY;
        options.notRenderingGameFrames=replay_->video;
        if(replay_->hudlessColor)options.hudLessSubrectSize={outputWidth_,outputHeight_};
        if(replay_->emptyUi)options.uiSubrectSize={outputWidth_,outputHeight_};
    }
    if(capture_.Before(list,color,depth,motion,frameId_,reset,runtimeStatus_)) {
        try {
            std::ostringstream text;text.precision(std::numeric_limits<float>::max_digits10);
            text<<"guidance_source="<<guidanceInfo_<<"\ninput_size="<<inputWidth_<<'x'<<inputHeight_<<"\noutput_size="<<outputWidth_<<'x'<<outputHeight_
                <<"\nmvec_scale="<<options.mvecScale[0]<<','<<options.mvecScale[1]<<"\nreset="<<reset
                <<"\ndepth_inverted="<<options.depthInverted<<"\northo="<<options.orthoProjection<<"\ncamera_motion_included="<<options.cameraMotionIncluded
                <<"\nmvec_dilated="<<options.motionVectorsDilated<<"\nmvec_invalid="<<options.motionVectorsInvalidValue<<"\nnot_rendering_game_frames="<<options.notRenderingGameFrames
                <<"\nnear="<<options.cameraNear<<"\nfar="<<options.cameraFar<<"\nfov="<<options.cameraFOV<<"\nhdr="<<hdr_<<'\n';
            auto matrix=[&](const char* name,const float (&m)[4][4]) {
                text<<name<<'=';for(const auto& row:m)for(float value:row)text<<value<<',';text<<'\n';
            };
            matrix("view_to_clip",options.cameraViewToClip);matrix("clip_to_view",options.clipToCameraView);
            matrix("clip_to_previous",options.clipToPrevClip);matrix("previous_to_clip",options.prevClipToClip);
            capture_.Context(text.str());
        }catch(const std::exception&) {capture_.Context("SDK context unavailable");}
    }
    for (unsigned i=0;i<count;++i) {
        ngx::Transition(list,disabled_[i].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(disabled_[i].Get(),0,flagClearValue_.Get(),0,4);
        ngx::Transition(list,disabled_[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        NVSDK_NGX_D3D12_DLSSG_Eval_Params evaluate{};
        evaluate.pBackbuffer = color;
        if(replay_) {
            evaluate.pHudless=replay_->hudlessColor ? color : nullptr;
            evaluate.pUI=replay_->emptyUi;
        }
        evaluate.pDepth = depth; evaluate.pMVecs = motion;
        evaluate.pOutputInterpFrame = output[i]; evaluate.pOutputDisableInterpolation = disabled_[i].Get();
        options.multiFrameIndex = i+1;
        const auto result = NGX_D3D12_EVALUATE_DLSSG(list,handle_,params_,&evaluate,&options);
        if (result != NVSDK_NGX_Result_Success) { pendingCount_ = 0; return result; }
        ngx::Transition(list,disabled_[i].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback_.Get(),i*4,disabled_[i].Get(),0,4);
        ngx::Transition(list,disabled_[i].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    capture_.After(list,output,count);
    if (frameId_==1) runtimeStatus_=processing::LoadedDlssgStatus();
    return NVSDK_NGX_Result_Success;
}
unsigned DlssMultiFrameGeneration::Collect() {
    if (pendingReset_ || !pendingCount_) return 0;
    void* data = nullptr;
    const D3D12_RANGE range{0,12};
    ngx::CheckHr(readback_->Map(0,&range,&data),"Read MFG validity flags");
    const auto* bytes = static_cast<const unsigned char*>(data);
    bool valid = true;
    std::array<unsigned,3> flags{};
    for (unsigned i=0;i<pendingCount_;++i) { valid = valid && bytes[i*4] == 0;std::memcpy(&flags[i],bytes+i*4,4); }
    capture_.Collect(flags);
    const D3D12_RANGE empty{0,0};
    readback_->Unmap(0,&empty);
    return valid ? pendingCount_ : 0;
}
}
#endif
