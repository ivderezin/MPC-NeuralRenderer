// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN64
#include "GenerationStage.h"
#include "DlssMultiFrameGeneration.h"
#include "../Processing/FsrApi.h"
#include <ffx/framegeneration/include/ffx_framegeneration.h>
namespace framegen {
struct GenerationStage::Impl {
    processing::FrameGenerator provider = processing::FrameGenerator::Dlss;
    DlssMultiFrameGeneration dlss;
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
                             const std::filesystem::path& directory, bool hdr) {
    impl_ = std::make_unique<Impl>();
    auto& p = *impl_;
    p.provider = provider;
    p.hdr = hdr;
    p.width = width;
    p.height = height;
    p.outputWidth = outWidth;
    p.outputHeight = outHeight;
    if (provider == processing::FrameGenerator::Dlss) {
        p.dlss.Create(device, list, width, height, outWidth, outHeight, hdr);
        return;
    }
    if (provider != processing::FrameGenerator::Fsr)
        throw std::runtime_error("XeSS FG uses its proxy presentation backend");
    p.fsr.Load(directory / L"amd_fidelityfx_framegeneration_dx12.dll");
    ffxCreateContextDescFrameGeneration create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    create.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | (hdr ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0);
    create.displaySize = {outWidth, outHeight};
    create.maxRenderSize = {width, height};
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
    return impl_->provider == processing::FrameGenerator::Dlss ? impl_->dlss.MaxGenerated() : 1;
}
void GenerationStage::Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth,
                               ID3D12Resource* motion, const std::array<ID3D12Resource*, 3>& output, unsigned count,
                               bool reset, float frameTimeMs) {
    auto& p = *impl_;
    if (p.provider == processing::FrameGenerator::Dlss) {
        ngx::Check(p.dlss.Evaluate(list, color, depth, motion, output, count, reset), "Dispatch DLSS MFG");
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
}
unsigned GenerationStage::Collect() {
    return impl_->provider == processing::FrameGenerator::Dlss ? impl_->dlss.Collect() : (impl_->pendingReset ? 0 : 1);
}
}
#endif
