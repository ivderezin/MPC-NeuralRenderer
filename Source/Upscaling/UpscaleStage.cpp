// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN64
#include "UpscaleStage.h"
#include <exception>
#include "DlssSuperResolution.h"
#include "MotionAdaptive.h"
#include "../Processing/FsrApi.h"
#include "../Processing/ColourTransfer.h"
#include "../Processing/ResourceBarrier.h"
#include <ffx/upscalers/include/ffx_upscale.h>
#include <xess/xess_d3d12.h>
#include <algorithm>
#include <limits>
namespace upscaling {
struct UpscaleStage::Impl {
    processing::Upscaler provider = processing::Upscaler::Off;
    DlssSuperResolution dlss;
    processing::FsrApi fsr;
    ffxContext fsrContext = nullptr;
    processing::RuntimeLibrary xess;
    xess_context_handle_t xessContext = nullptr;
    decltype(&xessDestroyContext) xessDestroy = nullptr;
    decltype(&xessD3D12Execute) xessExecute = nullptr;
    processing::ColourTransfer transfer;
    Microsoft::WRL::ComPtr<ID3D12Resource> inputLinear, outputLinear;
    Extent source{}, target{};
    bool motionConfidence = false;
    MotionAdaptive adaptive;
    Microsoft::WRL::ComPtr<ID3D12Resource> upscaled;
    ~Impl() {
        if (fsrContext)
            fsr.destroy(&fsrContext, nullptr);
        if (xessContext && xessDestroy)
            xessDestroy(xessContext);
    }
    void Create(ID3D12Device* device, ID3D12GraphicsCommandList* list, const std::filesystem::path& directory) {
        using namespace processing;
        if (motionConfidence) {
            adaptive.Create(device);
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            heap.CreationNodeMask = heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = target.width; desc.Height = target.height;
            desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            CheckHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&upscaled)), "Create motion-adaptive SR texture");
        }
        if (provider == Upscaler::Dlss) {
            dlss.Create(list, source, target);
            return;
        }
        transfer.Create(device);
        inputLinear = ColourTransfer::LinearTexture(device, source.width, source.height);
        outputLinear = ColourTransfer::LinearTexture(device, target.width, target.height);
        if (provider == Upscaler::Fsr) {
            fsr.Load(directory / L"amd_fidelityfx_upscaler_dx12.dll");
            ffxCreateContextDescUpscale create{};
            create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            create.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
                           FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
            create.maxRenderSize = {source.width, source.height};
            create.maxUpscaleSize = {target.width, target.height};
            ffxCreateBackendDX12Desc backend{};
            backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            backend.device = device;
            ffxOverrideVersion version{};
            version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            version.versionId = fsr.Fsr3Version(device, create.header.type);
            ffxCreateContextDescUpscaleVersion apiVersion{};
            apiVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
            apiVersion.version = FFX_UPSCALER_VERSION;
            create.header.pNext = &backend.header;
            backend.header.pNext = &version.header;
            version.header.pNext = &apiVersion.header;
            FsrApi::Check(fsr.create(&fsrContext, &create.header, nullptr), "Create FSR upscaler");
        } else if (provider == Upscaler::Xess) {
            xess.Load(directory / L"libxess.dll");
            xessDestroy = xess.Get<decltype(xessDestroy)>("xessDestroyContext");
            xessExecute = xess.Get<decltype(xessExecute)>("xessD3D12Execute");
            CheckIntel(xess.Get<decltype(&xessD3D12CreateContext)>("xessD3D12CreateContext")(device, &xessContext),
                       "Create XeSS context");
            auto getResolution = xess.Get<decltype(&xessGetOptimalInputResolution)>("xessGetOptimalInputResolution");
            xess_d3d12_init_params_t create{};
            create.outputResolution = {target.width, target.height};
            create.qualitySetting = XESS_QUALITY_SETTING_QUALITY;
            double best = (std::numeric_limits<double>::max)();
            for (auto quality :
                 {XESS_QUALITY_SETTING_ULTRA_PERFORMANCE, XESS_QUALITY_SETTING_PERFORMANCE,
                  XESS_QUALITY_SETTING_BALANCED, XESS_QUALITY_SETTING_QUALITY, XESS_QUALITY_SETTING_ULTRA_QUALITY,
                  XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, XESS_QUALITY_SETTING_AA}) {
                xess_2d_t recommended{}, minimum{}, maximum{};
                if (getResolution(xessContext, &create.outputResolution, quality, &recommended, &minimum, &maximum) < 0)
                    continue;
                if (source.width < minimum.x || source.height < minimum.y || source.width > maximum.x ||
                    source.height > maximum.y)
                    continue;
                const double distance = std::abs(static_cast<double>(recommended.x) - source.width) +
                                        std::abs(static_cast<double>(recommended.y) - source.height);
                if (distance < best) {
                    best = distance;
                    create.qualitySetting = quality;
                }
            }
            if (best == (std::numeric_limits<double>::max)())
                throw std::runtime_error("XeSS does not support the source/target size pair");
            create.initFlags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE |
                (motionConfidence ? XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK : 0);
            create.creationNodeMask = create.visibleNodeMask = 1;
            CheckIntel(xess.Get<decltype(&xessD3D12Init)>("xessD3D12Init")(xessContext, &create), "Initialize XeSS");
            CheckIntel(xess.Get<decltype(&xessSetVelocityScale)>("xessSetVelocityScale")(xessContext, 1, 1),
                       "Set XeSS motion scale");
            if (motionConfidence)
                CheckIntel(xess.Get<decltype(&xessSetMaxResponsiveMaskValue)>("xessSetMaxResponsiveMaskValue")(
                    xessContext, 1.f), "Enable full XeSS responsive mask range");
        } else
            throw std::runtime_error("No upscaler selected");
    }
    void Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motion,
                  ID3D12Resource* output, bool reset, float frameTimeMs, ID3D12Resource* distrust) {
        using namespace processing;
        ID3D12Resource* providerOutput = output;
        if (motionConfidence) {
            if (!distrust) throw std::invalid_argument("Motion-adaptive SR requires a distrust mask");
            providerOutput = upscaled.Get();
            Transition(list, providerOutput, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        if (provider == Upscaler::Dlss) {
            const auto result = dlss.Evaluate(list, color, depth, motion, providerOutput, reset, frameTimeMs, distrust);
            if (motionConfidence) {
                Transition(list, providerOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if (result == NVSDK_NGX_Result_Success) adaptive.Dispatch(list, providerOutput, color, distrust, output);
                Transition(list, providerOutput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            }
            ngx::Check(result, "Evaluate DLSS SR");
            return;
        }
        Transition(list, inputLinear.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transfer.Dispatch(list, color, inputLinear.Get(), false);
        Transition(list, inputLinear.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list, outputLinear.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // Complete the resource transitions even when a provider reports an error.
        std::exception_ptr failure;
        try {
            if (provider == Upscaler::Fsr) {
                ffxDispatchDescUpscale desc{};
                desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
                desc.commandList = list;
                desc.color = ffxApiGetResourceDX12(inputLinear.Get());
                desc.depth = ffxApiGetResourceDX12(depth);
                desc.motionVectors = ffxApiGetResourceDX12(motion);
                desc.reactive = ffxApiGetResourceDX12(distrust);
                desc.output = ffxApiGetResourceDX12(outputLinear.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
                desc.renderSize = {source.width, source.height};
                desc.upscaleSize = {target.width, target.height};
                desc.motionVectorScale = {1, 1};
                desc.preExposure = 1;
                desc.reset = reset;
                desc.frameTimeDelta = frameTimeMs;
                desc.cameraNear = 0.1f;
                desc.cameraFar = 1000;
                desc.cameraFovAngleVertical = 1.04719755f;
                desc.viewSpaceToMetersFactor = 1;
                FsrApi::Check(fsr.dispatch(&fsrContext, &desc.header), "Dispatch FSR upscaler");
            } else {
                xess_d3d12_execute_params_t desc{};
                desc.pColorTexture = inputLinear.Get();
                desc.pOutputTexture = outputLinear.Get();
                desc.pDepthTexture = depth;
                desc.pVelocityTexture = motion;
                desc.pResponsivePixelMaskTexture = distrust;
                desc.inputWidth = source.width;
                desc.inputHeight = source.height;
                desc.resetHistory = reset ? 1 : 0;
                desc.exposureScale = 1;
                CheckIntel(xessExecute(xessContext, list, &desc), "Dispatch XeSS upscaler");
            }
        } catch (...) {
            failure = std::current_exception();
        }
        Transition(list, outputLinear.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (!failure)
            transfer.Dispatch(list, outputLinear.Get(), providerOutput, true);
        if (motionConfidence) {
            Transition(list, providerOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (!failure) adaptive.Dispatch(list, providerOutput, color, distrust, output);
            Transition(list, providerOutput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        }
        Transition(list, outputLinear.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COMMON);
        Transition(list, inputLinear.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COMMON);
        if (failure)
            std::rethrow_exception(failure);
    }
};
UpscaleStage::UpscaleStage() = default;
UpscaleStage::~UpscaleStage() = default;
void UpscaleStage::Reset() {
    impl_.reset();
}
void UpscaleStage::Create(processing::Upscaler provider, ID3D12Device* device, ID3D12GraphicsCommandList* list,
                          Extent source, Extent target, const std::filesystem::path& directory, bool motionConfidence) {
    impl_ = std::make_unique<Impl>();
    impl_->provider = provider;
    impl_->source = source;
    impl_->target = target;
    impl_->motionConfidence = motionConfidence;
    impl_->Create(device, list, directory);
}
void UpscaleStage::Dispatch(ID3D12GraphicsCommandList* list, ID3D12Resource* color, ID3D12Resource* depth,
                            ID3D12Resource* motion, ID3D12Resource* output, bool reset, float frameTimeMs, ID3D12Resource* distrust) {
    impl_->Dispatch(list, color, depth, motion, output, reset, frameTimeMs, distrust);
}
}
#endif
