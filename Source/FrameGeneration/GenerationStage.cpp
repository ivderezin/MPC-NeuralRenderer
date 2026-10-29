// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN64
#include "GenerationStage.h"
#include "DlssMultiFrameGeneration.h"
#include "EchoFrameGeneration.h"
#include "EchoGuard.h"
#include "DlssCaptureCamera.h"
#include "../Processing/FsrApi.h"
#include <ffx/framegeneration/include/ffx_framegeneration.h>
#include <cstring>
#include <algorithm>
namespace framegen {
struct GenerationStage::Impl {
    processing::FrameGenerator provider = processing::FrameGenerator::Dlss;
    DlssMultiFrameGeneration dlss;
    EchoFrameGeneration echo, guidance;
    Microsoft::WRL::ComPtr<ID3D12Resource> guideDepth, depthUpload;
    bool nativeEchoMotion=false,dlssGuard=true;
    EchoGuard guard;
    processing::FsrApi fsr;
    ffxContext context = nullptr;
    unsigned width = 0, height = 0, outputWidth = 0, outputHeight = 0;
    uint64_t frameId = 0;
    bool pendingReset = true, hdr = false;
    ~Impl() {
        if (context)
            fsr.destroy(&context, nullptr);
    }
};
GenerationStage::GenerationStage() = default;
GenerationStage::~GenerationStage() = default;
void GenerationStage::Reset() {
    impl_.reset();
}
void GenerationStage::Create(processing::FrameGenerator provider, ID3D12Device* device, ID3D12GraphicsCommandList* list,
                             unsigned width, unsigned height, unsigned outWidth, unsigned outHeight,
                             const std::filesystem::path& directory, bool hdr, bool nativeEchoMotion, bool dlssGuard) {
    impl_ = std::make_unique<Impl>();
    auto& p = *impl_;
    p.provider = provider;
    p.dlssGuard=dlssGuard;
    p.hdr = hdr;
    p.width = width;
    p.height = height;
    p.outputWidth = outWidth;
    p.outputHeight = outHeight;
    if (provider == processing::FrameGenerator::EchoStorm) {
        p.echo.Create(device,outWidth,outHeight,hdr);
        return;
    }
    p.nativeEchoMotion=nativeEchoMotion;
    if (nativeEchoMotion || provider==processing::FrameGenerator::Dlss) {
        if (nativeEchoMotion) p.guidance.Create(device,outWidth,outHeight,hdr,true);
        const auto depthWidth=nativeEchoMotion ? outWidth : width;
        const auto depthHeight=nativeEchoMotion ? outHeight : height;
        // Vendor FG receives vectors at final colour resolution. Depth is still
        // explicitly a flat fallback; capture provides no game depth.
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=depthWidth;d.Height=depthHeight;
        d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32_FLOAT;
        ngx::CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p.guideDepth)),"Create final-resolution FG depth");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;d={};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
        d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ngx::CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&p.depthUpload)),"Create FG depth initialization");
        void* data=nullptr;const D3D12_RANGE empty{0,0};
        ngx::CheckHr(p.depthUpload->Map(0,&empty,&data),"Initialize FG depth");
        std::memset(data,0,static_cast<size_t>(bytes));
        if(provider==processing::FrameGenerator::Dlss) for(unsigned y=0;y<depthHeight;++y) {
            auto* row=reinterpret_cast<float*>(static_cast<unsigned char*>(data)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch);
            std::fill_n(row,depthWidth,DlssCaptureCamera::Depth);
        }
        p.depthUpload->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=p.guideDepth.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=p.depthUpload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        ngx::Transition(list,p.guideDepth.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (nativeEchoMotion) { p.width=outWidth;p.height=outHeight; }
    if (provider == processing::FrameGenerator::Dlss) {
        p.guard.Create(device,outWidth,outHeight,hdr,true);
        p.dlss.Create(device,list,p.width,p.height,outWidth,outHeight,hdr);
        p.dlss.SetGuidanceInfo(nativeEchoMotion ? "native_echo_d3d12_final_frame" : "fast_d3d11_repaired");
        return;
    }
    if (provider != processing::FrameGenerator::Fsr)
        throw std::runtime_error("XeSS FG uses its proxy presentation backend");
    p.guard.Create(device,outWidth,outHeight,hdr);
    p.fsr.Load(directory / L"amd_fidelityfx_framegeneration_dx12.dll");
    ffxCreateContextDescFrameGeneration create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    create.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | (hdr ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0);
    create.displaySize = {outWidth, outHeight};
    create.maxRenderSize = {p.width, p.height};
    create.backBufferFormat = hdr ? FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM : FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = device;
    ffxOverrideVersion version{};
    version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    version.versionId = p.fsr.Fsr3Version(device, create.header.type);
    ffxCreateContextDescFrameGenerationVersion api{};
    api.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    api.version = FFX_FRAMEGENERATION_VERSION;
    create.header.pNext = &backend.header;
    backend.header.pNext = &version.header;
    version.header.pNext = &api.header;
    processing::FsrApi::Check(p.fsr.create(&p.context, &create.header, nullptr), "Create FSR frame generation");
}
unsigned GenerationStage::MaxGenerated() const {
    if (impl_->provider == processing::FrameGenerator::EchoStorm) return 3;
    return impl_->provider == processing::FrameGenerator::Dlss ? impl_->dlss.MaxGenerated() : 1;
}
void GenerationStage::Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth,
                               ID3D12Resource* motion, const std::array<ID3D12Resource*, 3>& output, unsigned count,
                               bool reset, float frameTimeMs) {
    auto& p = *impl_;
    if (p.provider == processing::FrameGenerator::EchoStorm) {
        p.echo.Dispatch(list,color,output,count,reset);
        return;
    }
    if (p.nativeEchoMotion) {
        p.guidance.Dispatch(list,color,{},0,reset);
        motion=p.guidance.Motion();depth=p.guideDepth.Get();
        ngx::Transition(list,motion,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (p.provider == processing::FrameGenerator::Dlss) {
        depth=p.guideDepth.Get();
        const auto result=p.dlss.Evaluate(list, color, depth, motion, output, count, reset);
        if (result==NVSDK_NGX_Result_Success) p.guard.Dispatch(list,color,motion,output,p.dlssGuard ? count : 0u,reset);
        if (p.nativeEchoMotion) ngx::Transition(list,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::Check(result,"Dispatch DLSS MFG");
        return;
    }
    ++p.frameId;
    p.pendingReset = reset;
    ffxConfigureDescFrameGeneration config{};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.frameGenerationEnabled = true;
    config.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    config.generationRect = {0, 0, static_cast<int32_t>(p.outputWidth), static_cast<int32_t>(p.outputHeight)};
    config.frameID = p.frameId;
    processing::FsrApi::Check(p.fsr.configure(&p.context, &config.header), "Configure FSR frame generation");
    ffxDispatchDescFrameGenerationPrepareV2 prepare{};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = p.frameId;
    prepare.commandList = list;
    prepare.renderSize = {p.width, p.height};
    prepare.motionVectorScale = {1, 1};
    prepare.frameTimeDelta = frameTimeMs;
    prepare.reset = reset;
    prepare.cameraNear = .1f;
    prepare.cameraFar = 1000;
    prepare.cameraFovAngleVertical = 1.04719755f;
    prepare.viewSpaceToMetersFactor = 1;
    prepare.cameraUp[1] = prepare.cameraRight[0] = prepare.cameraForward[2] = 1;
    prepare.depth = ffxApiGetResourceDX12(depth);
    prepare.motionVectors = ffxApiGetResourceDX12(motion);
    processing::FsrApi::Check(p.fsr.dispatch(&p.context, &prepare.header), "Prepare FSR frame generation");
    ffxDispatchDescFrameGeneration generate{};
    generate.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    generate.commandList = list;
    generate.frameID = p.frameId;
    generate.presentColor = ffxApiGetResourceDX12(color);
    generate.outputs[0] = ffxApiGetResourceDX12(output[0], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    generate.numGeneratedFrames = 1;
    generate.reset = reset;
    generate.generationRect = config.generationRect;
    generate.backbufferTransferFunction = p.hdr ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    generate.minMaxLuminance[1] = p.hdr ? 10000.0f : 80.0f;
    processing::FsrApi::Check(p.fsr.dispatch(&p.context, &generate.header), "Dispatch FSR frame generation");
    p.guard.Dispatch(list,color,motion,output,1,reset);
    if (p.nativeEchoMotion) ngx::Transition(list,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
void GenerationStage::SetDlssGuard(bool enabled) { if(impl_) impl_->dlssGuard=enabled; }
std::wstring GenerationStage::RuntimeStatus() const {
    return impl_ && impl_->provider==processing::FrameGenerator::Dlss ? impl_->dlss.RuntimeStatus() : L"";
}
unsigned GenerationStage::Collect() {
    if (impl_->provider == processing::FrameGenerator::EchoStorm) return impl_->echo.Collect();
    return impl_->provider == processing::FrameGenerator::Dlss ? impl_->dlss.Collect() : (impl_->pendingReset ? 0 : 1);
}
}
#endif
