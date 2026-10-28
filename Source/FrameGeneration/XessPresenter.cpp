// SPDX-License-Identifier: GPL-3.0-or-later
#include "XessPresenter.h"
#include "GenerationPolicy.h"
#include "../NeuralRendering/MotionRepair.h"
#ifdef _WIN64
#include "../Processing/RuntimeLibrary.h"
#include "../Processing/ResourceBarrier.h"
#include "../NeuralRendering/MotionShaders.h"
#include "../NeuralRendering/MotionEstimator.h"
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xell/xell_d3d12.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace framegen {
namespace {
using Microsoft::WRL::ComPtr;
using processing::CheckHr;
using processing::CheckIntel;
using processing::Transition;
struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
};
struct SharedTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D12Resource> resource;
    void Create(ID3D11Device* device11, ID3D12Device* device12, UINT width, UINT height, DXGI_FORMAT format,
                bool unordered = true) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = format;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unordered ? D3D11_BIND_UNORDERED_ACCESS : 0);
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        CheckHr(device11->CreateTexture2D(&desc, nullptr, &texture), "Create XeSS shared texture");
        CheckHr(device11->CreateShaderResourceView(texture.Get(), nullptr, &srv), "Create XeSS shared SRV");
        if (unordered)
            CheckHr(device11->CreateUnorderedAccessView(texture.Get(), nullptr, &uav), "Create XeSS shared UAV");
        ComPtr<IDXGIResource1> dxgi;
        CheckHr(texture.As(&dxgi), "Query shared texture");
        Handle handle;
        CheckHr(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                         &handle.value),
                "Share XeSS texture");
        CheckHr(device12->OpenSharedHandle(handle.value, IID_PPV_ARGS(&resource)), "Open XeSS shared texture");
    }
};
}
struct XessPresenter::Impl {
    processing::RuntimeLibrary fgLibrary, latencyLibrary;
#define FG_EXPORT(name) decltype(&name) name##Fn = nullptr
    FG_EXPORT(xefgSwapChainD3D12CreateContext);
    FG_EXPORT(xefgSwapChainD3D12InitFromSwapChainDesc);
    FG_EXPORT(xefgSwapChainD3D12GetSwapChainPtr);
    FG_EXPORT(xefgSwapChainD3D12TagFrameResource);
    FG_EXPORT(xefgSwapChainTagFrameConstants);
    FG_EXPORT(xefgSwapChainSetLatencyReduction);
    FG_EXPORT(xefgSwapChainGetProperties);
    FG_EXPORT(xefgSwapChainSetNumInterpolatedFrames);
    FG_EXPORT(xefgSwapChainSetPresentId);
    FG_EXPORT(xefgSwapChainSetEnabled);
    FG_EXPORT(xefgSwapChainGetLastPresentStatus);
    FG_EXPORT(xefgSwapChainDestroy);
    FG_EXPORT(xellD3D12CreateContext);
    FG_EXPORT(xellSetSleepMode);
    FG_EXPORT(xellSleep);
    FG_EXPORT(xellAddMarkerData);
    FG_EXPORT(xellDestroyContext);
#undef FG_EXPORT
    xefg_swapchain_handle_t fg = nullptr;
    xell_context_handle_t latency = nullptr;
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<IDXGISwapChain4> swapchain;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D11Fence> fence11;
    Handle event;
    UINT64 fenceValue = 0, submitted = 0;
    ComPtr<ID3D11Texture2D> working, previous;
    ComPtr<ID3D11ShaderResourceView> previousView;
    ComPtr<ID3D11ComputeShader> motion;
    ComPtr<ID3D11Buffer> constants;
    SharedTexture scene, vectors, repairedVectors, depth, distrust;
    neural::MotionRepair motionRepair;
    neural::MotionEstimator estimator;
    bool echoMotion = false, echoReset = true;
    unsigned width = 0, height = 0, interpolations = 1, presentId = 0;
    unsigned long long lastToken = 0;
    bool validToken = false, reset = true, enabled = false, failed = false, displayLimited = false;
    unsigned lastPresented = 0;

    ~Impl() {
        try {
            Enable(false);
            Drain();
        } catch (...) {
        }
        swapchain.Reset();
        if (fg)
            xefgSwapChainDestroyFn(fg);
        if (latency)
            xellDestroyContextFn(latency);
    }
    void Enable(bool value) {
        if (value == enabled || !fg)
            return;
        CheckIntel(xefgSwapChainSetEnabledFn(fg, value ? 1 : 0), "Enable XeSS FG");
        enabled = value;
    }
    void Drain() {
        if (!queue || !fence)
            return;
        submitted = ++fenceValue;
        CheckHr(queue->Signal(fence.Get(), submitted), "Drain XeSS presentation");
        CheckHr(fence->SetEventOnCompletion(submitted, event.value), "Wait for XeSS presentation");
        if (WaitForSingleObject(event.value, INFINITE) != WAIT_OBJECT_0)
            throw std::runtime_error("XeSS fence wait failed");
    }
    void Textures(unsigned w, unsigned h) {
        working.Reset();
        previous.Reset();
        previousView.Reset();
        scene = {};
        vectors = {};
        repairedVectors = {};
        distrust = {};
        depth = {};
        width = w;
        height = h;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        CheckHr(device11->CreateTexture2D(&desc, nullptr, &working), "Create XeSS drawing surface");
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        CheckHr(device11->CreateTexture2D(&desc, nullptr, &previous), "Create XeSS previous frame");
        CheckHr(device11->CreateShaderResourceView(previous.Get(), nullptr, &previousView), "Create XeSS previous SRV");
        scene.Create(device11.Get(), device12.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, false);
        vectors.Create(device11.Get(), device12.Get(), w, h, DXGI_FORMAT_R16G16_FLOAT);
        repairedVectors.Create(device11.Get(), device12.Get(), w, h, DXGI_FORMAT_R16G16_FLOAT);
        motionRepair.Initialize(device11.Get(), w, h);
        if (echoMotion) {
            distrust.Create(device11.Get(), device12.Get(), w, h, DXGI_FORMAT_R8_UNORM);
            estimator.Initialize(device11.Get(), w, h);
            echoReset = true;
        }
        depth.Create(device11.Get(), device12.Get(), w, h, DXGI_FORMAT_R32_FLOAT);
        const float zero[4]{};
        context11->ClearUnorderedAccessViewFloat(depth.uav.Get(), zero);
        reset = true;
        validToken = false;
    }
    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, IDXGIFactory2* factory, HWND window,
                    DXGI_SWAP_CHAIN_DESC1 description, unsigned multiplier, double refreshHz, float frameTimeMs,
                    const std::filesystem::path& directory) {
        fgLibrary.Load(directory / L"libxess_fg.dll");
        latencyLibrary.Load(directory / L"libxell.dll");
#define LOAD_FG(name) name##Fn = fgLibrary.Get<decltype(&name)>(#name)
        LOAD_FG(xefgSwapChainD3D12CreateContext);
        LOAD_FG(xefgSwapChainD3D12InitFromSwapChainDesc);
        LOAD_FG(xefgSwapChainD3D12GetSwapChainPtr);
        LOAD_FG(xefgSwapChainD3D12TagFrameResource);
        LOAD_FG(xefgSwapChainTagFrameConstants);
        LOAD_FG(xefgSwapChainSetLatencyReduction);
        LOAD_FG(xefgSwapChainGetProperties);
        LOAD_FG(xefgSwapChainSetNumInterpolatedFrames);
        LOAD_FG(xefgSwapChainSetPresentId);
        LOAD_FG(xefgSwapChainSetEnabled);
        LOAD_FG(xefgSwapChainGetLastPresentStatus);
        LOAD_FG(xefgSwapChainDestroy);
#undef LOAD_FG
#define LOAD_LL(name) name##Fn = latencyLibrary.Get<decltype(&name)>(#name)
        LOAD_LL(xellD3D12CreateContext);
        LOAD_LL(xellSetSleepMode);
        LOAD_LL(xellSleep);
        LOAD_LL(xellAddMarkerData);
        LOAD_LL(xellDestroyContext);
#undef LOAD_LL
        device11 = device;
        CheckHr(context->QueryInterface(IID_PPV_ARGS(&context11)), "Query XeSS D3D11.4 context");
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        CheckHr(device->QueryInterface(IID_PPV_ARGS(&dxgi)), "Get XeSS adapter device");
        CheckHr(dxgi->GetAdapter(&adapter), "Get XeSS adapter");
        CheckHr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)),
                "Create XeSS presentation device");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        CheckHr(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create XeSS presentation queue");
        CheckHr(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence)), "Create XeSS shared fence");
        Handle shared;
        CheckHr(device12->CreateSharedHandle(fence.Get(), nullptr, GENERIC_ALL, nullptr, &shared.value),
                "Share XeSS fence");
        ComPtr<ID3D11Device5> device5;
        CheckHr(device->QueryInterface(IID_PPV_ARGS(&device5)), "Query XeSS D3D11.5 device");
        CheckHr(device5->OpenSharedFence(shared.value, IID_PPV_ARGS(&fence11)), "Open XeSS D3D11 fence");
        event.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event.value)
            throw std::runtime_error("Create XeSS fence event failed");
        CheckIntel(xefgSwapChainD3D12CreateContextFn(device12.Get(), &fg), "Create XeSS FG context");
        CheckIntel(xellD3D12CreateContextFn(device12.Get(), &latency), "Create XeLL context");
        xell_sleep_params_t sleep{};
        sleep.bLowLatencyMode = 1;
        CheckIntel(xellSetSleepModeFn(latency, &sleep), "Configure XeLL");
        CheckIntel(xefgSwapChainSetLatencyReductionFn(fg, latency), "Attach XeLL to XeSS FG");
        xefg_swapchain_properties_t properties{};
        CheckIntel(xefgSwapChainGetPropertiesFn(fg, &properties), "Query XeSS FG capabilities");
        const auto limit = GeneratedLimit(multiplier, properties.maxSupportedInterpolations, refreshHz, frameTimeMs);
        displayLimited = limit == 0;
        interpolations = (std::min)({multiplier - 1, properties.maxSupportedInterpolations, (std::max)(1u, limit)});
        if (!interpolations)
            throw std::runtime_error("XeSS FG: device supports no interpolation");
        xefg_swapchain_d3d12_init_params_t init{};
        init.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH | XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV;
        init.maxInterpolatedFrames = interpolations;
        init.creationNodeMask = init.visibleNodeMask = 1;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.BufferCount = 3;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.Flags = 0;
        CheckIntel(
            xefgSwapChainD3D12InitFromSwapChainDescFn(fg, window, &description, nullptr, queue.Get(), factory, &init),
            "Initialize XeSS FG swap chain");
        CheckIntel(xefgSwapChainD3D12GetSwapChainPtrFn(fg, IID_PPV_ARGS(&swapchain)), "Get XeSS proxy swap chain");
        CheckIntel(xefgSwapChainSetNumInterpolatedFramesFn(fg, interpolations), "Set XeSS FG multiplier");
        CheckHr(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "Create XeSS allocator");
        CheckHr(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&list)),
                "Create XeSS command list");
        CheckHr(list->Close(), "Close initial XeSS command list");
        ComPtr<ID3DBlob> shader, errors;
        CheckHr(D3DCompile(neural::MotionShader, std::strlen(neural::MotionShader), "XeSS motion", nullptr, nullptr,
                           "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors),
                "Compile XeSS motion");
        CheckHr(device->CreateComputeShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &motion),
                "Create XeSS motion shader");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 32;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        CheckHr(device->CreateBuffer(&buffer, nullptr, &constants), "Create XeSS motion constants");
        Textures(description.Width, description.Height);
    }
    void Marker(xell_latency_marker_type_t marker) {
        CheckIntel(xellAddMarkerDataFn(latency, presentId, marker), "XeLL frame marker");
    }
    HRESULT Present(unsigned syncInterval, bool running, unsigned long long token, float frameTimeMs) {
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX)
            return device12->GetDeviceRemovedReason();
        if (completed < submitted)
            return S_FALSE;
        if (running && validToken && lastToken == token && !failed)
            return S_FALSE;
        const bool fresh = running && (!validToken || lastToken != token) && !failed;
        const bool useFG = fresh && frameTimeMs > 0 && !displayLimited;
        if (!fresh)
            reset = true;
        Enable(useFG);
        ++presentId;
        CheckIntel(xellSleepFn(latency, presentId), "XeLL sleep");
        Marker(XELL_SIMULATION_START);
        Marker(XELL_SIMULATION_END);
        Marker(XELL_RENDERSUBMIT_START);
        context11->OMSetRenderTargets(0, nullptr, nullptr);
        context11->CopyResource(scene.texture.Get(), working.Get());
        if (reset)
            context11->CopyResource(previous.Get(), scene.texture.Get());
        if (useFG) {
            if (echoMotion) {
                estimator.Dispatch(context11.Get(), scene.srv.Get(), vectors.uav.Get(), distrust.uav.Get(), reset || echoReset);
                echoReset = false;
            } else {
                const UINT params[] = {width, height, 0, 0, reset ? 1u : 0u, 0, 0, 0};
                context11->UpdateSubresource(constants.Get(), 0, nullptr, params, 0, 0);
                ID3D11Buffer* cb[] = {constants.Get()};
                ID3D11ShaderResourceView* input[] = {scene.srv.Get(), previousView.Get()};
                ID3D11UnorderedAccessView* output[] = {vectors.uav.Get()};
                context11->CSSetConstantBuffers(0, 1, cb);
                context11->CSSetShader(motion.Get(), nullptr, 0);
                context11->CSSetShaderResources(0, 2, input);
                context11->CSSetUnorderedAccessViews(0, 1, output, nullptr);
                context11->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                input[0] = input[1] = nullptr;
                output[0] = nullptr;
                cb[0] = nullptr;
                context11->CSSetShaderResources(0, 2, input);
                context11->CSSetUnorderedAccessViews(0, 1, output, nullptr);
                context11->CSSetConstantBuffers(0, 1, cb);
                context11->CSSetShader(nullptr, nullptr, 0);
            }
            motionRepair.Dispatch(context11.Get(), previousView.Get(), scene.srv.Get(),
                vectors.srv.Get(), distrust.srv.Get(), repairedVectors.uav.Get());
        } else echoReset = true;

        context11->CopyResource(previous.Get(), scene.texture.Get());
        CheckHr(context11->Signal(fence11.Get(), ++fenceValue), "Signal XeSS source fence");
        context11->Flush();
        CheckHr(queue->Wait(fence.Get(), fenceValue), "Wait for XeSS source");
        CheckHr(allocator->Reset(), "Reset XeSS allocator");
        CheckHr(list->Reset(allocator.Get(), nullptr), "Reset XeSS command list");
        ComPtr<ID3D12Resource> buffer;
        CheckHr(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer)),
                "Get XeSS presentation buffer");
        Transition(list.Get(), scene.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list.Get(), buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(buffer.Get(), scene.resource.Get());
        Transition(list.Get(), buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        Transition(list.Get(), scene.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        if (useFG) {
            for (auto* resource : {repairedVectors.resource.Get(), depth.resource.Get()})
                Transition(list.Get(), resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
            xefg_swapchain_d3d12_resource_data_t data{};
            data.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
            data.resourceSize = {width, height};
            data.incomingState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            data.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
            data.pResource = repairedVectors.resource.Get();
            CheckIntel(xefgSwapChainD3D12TagFrameResourceFn(fg, list.Get(), presentId, &data), "Tag XeSS motion");
            data.type = XEFG_SWAPCHAIN_RES_DEPTH;
            data.pResource = depth.resource.Get();
            CheckIntel(xefgSwapChainD3D12TagFrameResourceFn(fg, list.Get(), presentId, &data), "Tag XeSS depth");
            for (auto* resource : {repairedVectors.resource.Get(), depth.resource.Get()})
                Transition(list.Get(), resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            xefg_swapchain_frame_constant_data_t constantsData{};
            for (unsigned i = 0; i < 4; ++i)
                constantsData.viewMatrix[i * 5] = constantsData.projectionMatrix[i * 5] = 1;
            constantsData.motionVectorScaleX = constantsData.motionVectorScaleY = 1;
            constantsData.resetHistory = reset ? 1 : 0;
            constantsData.frameRenderTime = frameTimeMs;
            CheckIntel(xefgSwapChainTagFrameConstantsFn(fg, presentId, &constantsData), "Tag XeSS frame constants");
        }
        CheckHr(list->Close(), "Close XeSS frame commands");
        ID3D12CommandList* commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        submitted = ++fenceValue;
        CheckHr(queue->Signal(fence.Get(), submitted), "Signal XeSS copy completion");
        Marker(XELL_RENDERSUBMIT_END);
        CheckIntel(xefgSwapChainSetPresentIdFn(fg, presentId), "Set XeSS present ID");
        Marker(XELL_PRESENT_START);
        const HRESULT result = swapchain->Present(syncInterval, 0);
        Marker(XELL_PRESENT_END);
        if (SUCCEEDED(result) && !failed) {
            xefg_swapchain_present_status_t status{};
            CheckIntel(xefgSwapChainGetLastPresentStatusFn(fg, &status), "Query XeSS FG presentation");
            CheckIntel(status.frameGenResult, "XeSS frame interpolation");
            lastPresented = status.framesPresented;
        }
        lastToken = token;
        validToken = true;
        reset = !useFG;
        return result;
    }
};
XessPresenter::XessPresenter() = default;
XessPresenter::~XessPresenter() = default;
void XessPresenter::Reset() {
    impl_.reset();
    status_.clear();
}
HRESULT XessPresenter::Create(ID3D11Device* device, ID3D11DeviceContext* context, IDXGIFactory2* factory, HWND window,
                              DXGI_SWAP_CHAIN_DESC1 description, unsigned multiplier, double refreshHz,
                              float frameTimeMs, const std::filesystem::path& directory, IDXGISwapChain1** swapchain, bool echoMotion) {
    Reset();
    try {
        auto instance = std::make_unique<Impl>();
        instance->echoMotion = echoMotion;
        instance->Initialize(device, context, factory, window, description, multiplier, refreshHz, frameTimeMs,
                             directory);
        CheckHr(instance->swapchain.CopyTo(swapchain), "Return XeSS swap chain");
        status_ =
            L"XeSS FG: up to " + std::to_wstring(instance->interpolations + 1) + L"x (" + std::wstring(echoMotion ? L"EchoStorm motion" : L"fast motion") + L", zero depth)";
        impl_ = std::move(instance);
        return S_OK;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        status_ = L"XeSS FG: " + std::wstring(message.begin(), message.end());
        return E_FAIL;
    }
}
HRESULT XessPresenter::BackBuffer(ID3D11Texture2D** texture) const {
    return impl_ ? impl_->working.CopyTo(texture) : E_UNEXPECTED;
}
void XessPresenter::ClearHistory() {
    if (impl_) {
        impl_->reset = true;
        impl_->validToken = false;
    }
}
HRESULT XessPresenter::Resize(unsigned width, unsigned height) {
    if (!impl_)
        return E_UNEXPECTED;
    if (!width || !height)
        return S_FALSE;
    try {
        auto& p = *impl_;
        p.Enable(false);
        p.Drain();
        p.context11->OMSetRenderTargets(0, nullptr, nullptr);
        CheckHr(p.swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "Resize XeSS swap chain");
        p.Textures(width, height);
        return S_OK;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        status_ = L"XeSS FG: " + std::wstring(message.begin(), message.end());
        return E_FAIL;
    }
}
HRESULT XessPresenter::Present(unsigned syncInterval, bool running, unsigned long long token, float frameTimeMs) {
    if (!impl_)
        return E_UNEXPECTED;
    try {
        const auto result = impl_->Present(syncInterval, running, token, frameTimeMs);
        if (!impl_->failed) {
            status_ = impl_->displayLimited ? L"XeSS FG: bypass (display refresh limit)"
                : L"XeSS FG: up to " + std::to_wstring(impl_->interpolations + 1)
                    + L"x, latest present " + std::to_wstring(impl_->lastPresented) + L" frames (" + std::wstring(impl_->echoMotion ? L"EchoStorm motion" : L"fast motion") + L", zero depth)";
        }
        return result;
    } catch (const std::exception& error) {
        impl_->failed = true;
        impl_->reset = true;
        const std::string message = error.what();
        status_ = L"XeSS FG: " + std::wstring(message.begin(), message.end());
        return E_FAIL;
    }
}
}
#else
namespace framegen {
struct XessPresenter::Impl {};
XessPresenter::XessPresenter() = default;
XessPresenter::~XessPresenter() = default;
void XessPresenter::Reset() {}
void XessPresenter::ClearHistory() {}
HRESULT XessPresenter::Create(ID3D11Device*, ID3D11DeviceContext*, IDXGIFactory2*, HWND, DXGI_SWAP_CHAIN_DESC1,
                              unsigned, double, float, const std::filesystem::path&, IDXGISwapChain1**, bool) {
    return E_NOTIMPL;
}
HRESULT XessPresenter::BackBuffer(ID3D11Texture2D**) const {
    return E_NOTIMPL;
}
HRESULT XessPresenter::Resize(unsigned, unsigned) {
    return E_NOTIMPL;
}
HRESULT XessPresenter::Present(unsigned, bool, unsigned long long, float) {
    return E_NOTIMPL;
}
}
#endif
