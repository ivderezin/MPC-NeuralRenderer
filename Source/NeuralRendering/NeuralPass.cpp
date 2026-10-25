// SPDX-License-Identifier: GPL-3.0-or-later
#include "NeuralPass.h"
#ifdef _WIN64
#include "FrameHistory.h"
#include "MotionShaders.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <nvsdk_ngx.h>

#pragma comment(lib, "nvsdk_ngx_s.lib")

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace neural {
namespace {
using Microsoft::WRL::ComPtr;
std::atomic_flag SessionActive = ATOMIC_FLAG_INIT;
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << ": 0x" << std::hex << static_cast<unsigned long>(result);
        throw std::runtime_error(message.str());
    }
}
void CheckNgx(NVSDK_NGX_Result result, const char* operation) {
    if (result != NVSDK_NGX_Result_Success) {
        std::ostringstream message;
        message << operation << ": NGX 0x" << std::hex << static_cast<unsigned>(result);
        throw std::runtime_error(message.str());
    }
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Module {
    HMODULE value = nullptr;
    ~Module() { if (value) FreeLibrary(value); }
};
std::filesystem::path ModuleDirectory() {
    HMODULE self = nullptr;
    Check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self) ? S_OK : HRESULT_FROM_WIN32(GetLastError()), "Locate renderer");
    std::array<wchar_t, 32768> path{};
    auto length = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Renderer path too long");
    return std::filesystem::path(path.data()).parent_path();
}
template<class T> T Export(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(std::string("Missing export: ") + name);
    return reinterpret_cast<T>(address);
}
using InitFn = NVSDK_NGX_Result(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
using CreateFn = NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFn = NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, void*);
using ReleaseFn = NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Handle*);
using ShutdownFn = NVSDK_NGX_Result(__cdecl*)(ID3D12Device*);

// Feature 18's signed runtime ABI is version-sensitive; contain native faults at the call boundary.
NVSDK_NGX_Result CallInit(InitFn fn, const wchar_t* path, ID3D12Device* device) {
#ifdef _MSC_VER
    __try {
#endif
        return fn(0x0876232Cull, path, device, NVSDK_NGX_Version_API, nullptr);
#ifdef _MSC_VER
    } __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_Fail; }
#endif
}
NVSDK_NGX_Result CallCreate(CreateFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Parameter* params, NVSDK_NGX_Handle** feature) {
#ifdef _MSC_VER
    __try {
#endif
        return fn(list, static_cast<NVSDK_NGX_Feature>(18), params, feature);
#ifdef _MSC_VER
    } __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_Fail; }
#endif
}
NVSDK_NGX_Result CallEvaluate(EvaluateFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Handle* feature, NVSDK_NGX_Parameter* params) {
#ifdef _MSC_VER
    __try {
#endif
        return fn(list, feature, params, nullptr);
#ifdef _MSC_VER
    } __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_Fail; }
#endif
}
void CallRelease(ReleaseFn fn, NVSDK_NGX_Handle* feature) {
#ifdef _MSC_VER
    __try {
#endif
        fn(feature);
#ifdef _MSC_VER
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif
}
void CallShutdown(ShutdownFn fn, ID3D12Device* device) {
#ifdef _MSC_VER
    __try {
#endif
        fn(device);
#ifdef _MSC_VER
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif
}
struct SharedTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D12Resource> resource;
    void Create(ID3D11Device* device11, ID3D12Device* device12, UINT width, UINT height, DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = format; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Check(device11->CreateTexture2D(&desc, nullptr, &texture), "Create shared texture");
        Check(device11->CreateShaderResourceView(texture.Get(), nullptr, &srv), "Create shared SRV");
        Check(device11->CreateUnorderedAccessView(texture.Get(), nullptr, &uav), "Create shared UAV");
        ComPtr<IDXGIResource1> dxgi;
        Check(texture.As(&dxgi), "Query shared texture");
        Handle handle;
        Check(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle.value), "Share texture");
        Check(device12->OpenSharedHandle(handle.value, IID_PPV_ARGS(&resource)), "Open D3D12 texture");
    }
};
ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const char* source) {
    ComPtr<ID3DBlob> code, errors;
    auto result = D3DCompile(source, strlen(source), "MPC-NeuralRenderer", nullptr, nullptr, "main", "cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    Check(result, "Compile compute shader");
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader), "Create compute shader");
    return shader;
}
void Subrect(NVSDK_NGX_Parameter* params, const char* name, UINT width, UINT height) {
    std::string prefix = std::string("DLSSNR.") + name + "Subrect";
    params->Set((prefix + "BaseX").c_str(), 0u);
    params->Set((prefix + "BaseY").c_str(), 0u);
    params->Set((prefix + "Width").c_str(), width);
    params->Set((prefix + "Height").c_str(), height);
}
NVSDK_NGX_Result __cdecl ScalingRatio(NVSDK_NGX_Parameter* params) {
    params->Set("DLSSNR.ScalingRatio", 1.0f);
    return NVSDK_NGX_Result_Success;
}
}

struct NeuralPass::Impl {
    bool lease = false, coreInitialized = false, snippetInitialized = false;
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence12;
    ComPtr<ID3D11Fence> fence11;
    Handle fenceEvent;
    UINT64 fenceValue = 0, submitted = 0;
    UINT width = 0, height = 0;
    RECT rect{};
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D11Texture2D> source, previous;
    ComPtr<ID3D11ShaderResourceView> sourceView, previousView;
    ComPtr<ID3D11ComputeShader> prepare, motion;
    ComPtr<ID3D11Buffer> constants;
    SharedTexture color, output, vectors, depth;
    FrameHistory history;
    Module snippet;
    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    CreateFn create = nullptr;
    EvaluateFn evaluate = nullptr;
    ReleaseFn release = nullptr;
    ShutdownFn shutdown = nullptr;

    void WaitForSubmission() {
        if (!submitted || fence12->GetCompletedValue() >= submitted) return;
        Check(fence12->SetEventOnCompletion(submitted, fenceEvent.value), "Fence completion event");
        if (WaitForSingleObject(fenceEvent.value, INFINITE) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU fence wait failed");
    }
    ~Impl() {
        try { WaitForSubmission(); } catch (...) {}
        if (feature && release) CallRelease(release, feature);
        if (params) NVSDK_NGX_D3D12_DestroyParameters(params);
        if (snippetInitialized && shutdown) CallShutdown(shutdown, device12.Get());
        if (coreInitialized) NVSDK_NGX_D3D12_Shutdown1(device12.Get());
        if (lease) SessionActive.clear();
    }
    void Begin() {
        WaitForSubmission();
        Check(allocator->Reset(), "Reset command allocator");
        Check(list->Reset(allocator.Get(), nullptr), "Reset command list");
    }
    void Submit() {
        Check(list->Close(), "Close command list");
        ID3D12CommandList* commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        submitted = ++fenceValue;
        Check(queue->Signal(fence12.Get(), submitted), "Signal D3D12 fence");
        Check(context11->Wait(fence11.Get(), submitted), "D3D11 wait");
    }
    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* frame, RECT videoRect) {
        if (SessionActive.test_and_set()) throw std::runtime_error("Another neural renderer is active in this process");
        lease = true;
        device11 = device;
        Check(context->QueryInterface(IID_PPV_ARGS(&context11)), "D3D11.4 context required");
        ComPtr<IDXGIDevice> dxgi;
        Check(device->QueryInterface(IID_PPV_ARGS(&dxgi)), "Query DXGI device");
        ComPtr<IDXGIAdapter> adapter;
        Check(dxgi->GetAdapter(&adapter), "Get GPU adapter");
        DXGI_ADAPTER_DESC adapterDesc{};
        Check(adapter->GetDesc(&adapterDesc), "Get GPU description");
        if (adapterDesc.VendorId != 0x10de) throw std::runtime_error("NVIDIA GPU required");
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)), "Create D3D12 device");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        Check(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create command queue");
        Check(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create allocator");
        Check(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create command list");
        Check(list->Close(), "Close initial list");
        Check(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)), "Create shared fence");
        Handle fenceHandle;
        Check(device12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &fenceHandle.value), "Share fence");
        ComPtr<ID3D11Device5> device5;
        Check(device->QueryInterface(IID_PPV_ARGS(&device5)), "D3D11.5 device required");
        Check(device5->OpenSharedFence(fenceHandle.value, IID_PPV_ARGS(&fence11)), "Open D3D11 fence");
        fenceEvent.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(fenceEvent.value ? S_OK : HRESULT_FROM_WIN32(GetLastError()), "Create fence event");

        rect = videoRect;
        width = rect.right - rect.left; height = rect.bottom - rect.top;
        D3D11_TEXTURE2D_DESC desc{};
        frame->GetDesc(&desc);
        sourceFormat = desc.Format;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        Check(device->CreateTexture2D(&desc, nullptr, &source), "Create frame snapshot");
        Check(device->CreateShaderResourceView(source.Get(), nullptr, &sourceView), "Create snapshot SRV");
        color.Create(device, device12.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
        output.Create(device, device12.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
        vectors.Create(device, device12.Get(), width, height, DXGI_FORMAT_R16G16_FLOAT);
        depth.Create(device, device12.Get(), width, height, DXGI_FORMAT_R32_FLOAT);
        color.texture->GetDesc(&desc);
        desc.MiscFlags = 0; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Check(device->CreateTexture2D(&desc, nullptr, &previous), "Create previous frame");
        Check(device->CreateShaderResourceView(previous.Get(), nullptr, &previousView), "Create previous SRV");
        prepare = Compile(device, PrepareShader);
        motion = Compile(device, MotionShader);
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 32; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Check(device->CreateBuffer(&buffer, nullptr, &constants), "Create constants");
        const float zero[4]{};
        context11->ClearUnorderedAccessViewFloat(depth.uav.Get(), zero);
        context11->ClearUnorderedAccessViewFloat(vectors.uav.Get(), zero);

        auto directory = ModuleDirectory();
        const auto model = directory / L"nvngx_dlssnr.dll";
        snippet.value = LoadLibraryExW(model.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        Check(snippet.value ? S_OK : HRESULT_FROM_WIN32(GetLastError()), "Load nvngx_dlssnr.dll beside renderer");
        auto init = Export<InitFn>(snippet.value, "NVSDK_NGX_D3D12_Init_Ext");
        create = Export<CreateFn>(snippet.value, "NVSDK_NGX_D3D12_CreateFeature");
        evaluate = Export<EvaluateFn>(snippet.value, "NVSDK_NGX_D3D12_EvaluateFeature");
        release = Export<ReleaseFn>(snippet.value, "NVSDK_NGX_D3D12_ReleaseFeature");
        shutdown = Export<ShutdownFn>(snippet.value, "NVSDK_NGX_D3D12_Shutdown1");
        std::array<wchar_t, MAX_PATH> temp{};
        auto tempLength = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        if (!tempLength || tempLength >= temp.size()) throw std::runtime_error("NGX cache path unavailable");
        auto cache = std::filesystem::path(temp.data()) / L"MPC-NeuralRenderer";
        std::filesystem::create_directories(cache);
        const auto modelDir = directory.wstring();
        const wchar_t* paths[] = {modelDir.c_str()};
        NVSDK_NGX_FeatureCommonInfo info{};
        info.PathListInfo.Path = paths; info.PathListInfo.Length = 1;
        CheckNgx(NVSDK_NGX_D3D12_Init_with_ProjectID("80b3e1a9-7dab-4bb3-b64c-5d923d8cbf40", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
            "MPC-NeuralRenderer-0.1.0", cache.c_str(), device12.Get(), &info), "Initialize driver NGX");
        coreInitialized = true;
        CheckNgx(NVSDK_NGX_D3D12_AllocateParameters(&params), "Allocate NGX parameters");
        CheckNgx(CallInit(init, modelDir.c_str(), device12.Get()), "Initialize NR model");
        snippetInitialized = true;
        for (const auto* key : {"DLSSNR.Width", "DLSSNR.InputWidth", "DLSSNR.OutputWidth", "DLSSNR.Output.Width", "Width", "OutWidth"}) params->Set(key, width);
        for (const auto* key : {"DLSSNR.Height", "DLSSNR.InputHeight", "DLSSNR.OutputHeight", "DLSSNR.Output.Height", "Height", "OutHeight"}) params->Set(key, height);
        params->Set("DLSSNR.Upscaling", 0u);
        params->Set("DLSSNR.Scale", 1.0f);
        params->Set("DLSSNR.ScalingRatio", 1.0f);
        params->Set("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&ScalingRatio));
        params->Set("DLSSNR.Hint.Render.Preset", 0u);
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
        params->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
        params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        Begin();
        CheckNgx(CallCreate(create, list.Get(), params, &feature), "Create NR feature");
        if (!feature) throw std::runtime_error("NR returned no feature handle");
        Submit();
    }
    void Transition(bool begin) {
        std::array<D3D12_RESOURCE_BARRIER, 4> barriers{};
        const std::array<ID3D12Resource*,4> resources = {color.resource.Get(), output.resource.Get(), vectors.resource.Get(), depth.resource.Get()};
        for (size_t i=0; i<resources.size(); ++i) {
            auto state = i == 1 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            auto& transition = barriers[i].Transition;
            transition.pResource = resources[i];
            transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            transition.StateBefore = begin ? D3D12_RESOURCE_STATE_COMMON : state;
            transition.StateAfter = begin ? state : D3D12_RESOURCE_STATE_COMMON;
        }
        list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
    ID3D11Texture2D* Run(ID3D11Texture2D* frame, long long timestamp, int intensity) {
        const auto action = history.Classify(timestamp);
        if (action == FrameHistory::Action::Repeat) return output.texture.Get();
        Begin();
        const bool reset = action == FrameHistory::Action::Reset;
        const UINT data[] = {width, height, static_cast<UINT>(rect.left), static_cast<UINT>(rect.top), reset ? 1u : 0u, 0, 0, 0};
        context11->OMSetRenderTargets(0, nullptr, nullptr);
        context11->CopyResource(source.Get(), frame);
        context11->UpdateSubresource(constants.Get(), 0, nullptr, data, 0, 0);
        ID3D11Buffer* cb[] = {constants.Get()};
        context11->CSSetConstantBuffers(0, 1, cb);
        ID3D11ShaderResourceView* input[] = {sourceView.Get(), nullptr};
        ID3D11UnorderedAccessView* target[] = {color.uav.Get()};
        context11->CSSetShader(prepare.Get(), nullptr, 0);
        context11->CSSetShaderResources(0, 2, input);
        context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
        context11->Dispatch((width+7)/8, (height+7)/8, 1);
        target[0] = nullptr;
        context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
        if (reset) context11->CopyResource(previous.Get(), color.texture.Get());
        input[0] = color.srv.Get(); input[1] = previousView.Get();
        target[0] = vectors.uav.Get();
        context11->CSSetShader(motion.Get(), nullptr, 0);
        context11->CSSetShaderResources(0, 2, input);
        context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
        context11->Dispatch((width+7)/8, (height+7)/8, 1);
        input[0] = input[1] = nullptr; target[0] = nullptr; cb[0] = nullptr;
        context11->CSSetShaderResources(0, 2, input);
        context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
        context11->CSSetConstantBuffers(0, 1, cb);
        context11->CSSetShader(nullptr, nullptr, 0);
        context11->CopyResource(previous.Get(), color.texture.Get());
        const auto ready = ++fenceValue;
        Check(context11->Signal(fence11.Get(), ready), "Signal D3D11 fence");
        context11->Flush();
        Check(queue->Wait(fence12.Get(), ready), "D3D12 wait");
        Transition(true);
        params->Set("DLSSNR.Color", color.resource.Get());
        params->Set("DLSSNR.Output", output.resource.Get());
        params->Set("DLSSNR.MVec", vectors.resource.Get());
        params->Set("DLSSNR.Depth", depth.resource.Get());
        for (const auto* name : {"Color", "Output", "MVec", "Depth"}) Subrect(params, name, width, height);
        params->Set("DLSSNR.MVecScaleX", 1.0f); params->Set("DLSSNR.MVecScaleY", 1.0f);
        params->Set("DLSSNR.DepthInverted", 1);
        params->Set("DLSSNR.Enabled", 1);
        params->Set("DLSSNR.Reset", reset ? 1 : 0);
        params->Set("DLSSNR.Style", 0);
        params->Set("DLSSNR.Intensity", intensity / 100.0f);
        params->Set("DLSSNR.LocalToneStrength", 1.0f);
        params->Set("DLSSNR.LocalStructureStrength", 1.0f);
        params->Set("DLSSNR.SkinStructureStrength", 1.0f);
        params->Set("DLSSNR.UseAutoMask", 1);
        params->Set("DLSSNR.UICorrection", 1);
        auto result = CallEvaluate(evaluate, list.Get(), feature, params);
        Transition(false);
        // Submit the cleanup barriers even if the runtime reports an evaluation failure.
        Submit();
        CheckNgx(result, "Evaluate NR");
        history.Commit(timestamp);
        return output.texture.Get();
    }
};
NeuralPass::NeuralPass() = default;
NeuralPass::~NeuralPass() = default;
void NeuralPass::Configure(Settings settings) {
    settings.intensity = std::clamp(settings.intensity, 0, 100);
    if (settings_ == settings) return;
    settings_ = settings;
    ResetDevice();
}
void NeuralPass::ResetDevice() {
    impl_.reset(); failed_ = false;
    status_ = settings_.enabled ? L"Waiting for SDR frame" : L"Off";
}
void NeuralPass::ClearHistory() { if (impl_) impl_->history.Clear(); }
ID3D11Texture2D* NeuralPass::Process(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* frame, const RECT& rect, long long timestamp, bool sdr) {
    if (!settings_.enabled || failed_) return nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    frame->GetDesc(&desc);
    if (!sdr || (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)) {
        ClearHistory(); status_ = L"Bypass: SDR 8-bit output required"; return nullptr;
    }
    if (rect.left < 0 || rect.top < 0 || rect.right > static_cast<LONG>(desc.Width) || rect.bottom > static_cast<LONG>(desc.Height)
        || rect.right <= rect.left || rect.bottom <= rect.top || desc.SampleDesc.Count != 1) {
        ClearHistory(); status_ = L"Bypass: video rectangle outside output"; return nullptr;
    }
    try {
        if (impl_) {
            D3D11_TEXTURE2D_DESC old{};
            impl_->source->GetDesc(&old);
            if (impl_->device11.Get() != device || !EqualRect(&impl_->rect, &rect) || old.Width != desc.Width || old.Height != desc.Height || impl_->sourceFormat != desc.Format)
                impl_.reset();
        }
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
            impl_->Initialize(device, context, frame, rect);
        }
        auto result = impl_->Run(frame, timestamp, settings_.intensity);
        status_ = L"Active (estimated motion, zero depth)";
        return result;
    } catch (const std::exception& error) {
        impl_.reset(); failed_ = true;
        const std::string message = error.what();
        status_ = L"Bypass: " + std::wstring(message.begin(), message.end());
        OutputDebugStringW((L"MPC Neural Renderer: " + status_ + L"\n").c_str());
        return nullptr;
    }
}
}

#else
namespace neural {
struct NeuralPass::Impl {};
NeuralPass::NeuralPass() { status_ = L"Unsupported: use the x64 renderer"; }
NeuralPass::~NeuralPass() = default;
void NeuralPass::Configure(Settings settings) { settings_ = settings; failed_ = false; }
void NeuralPass::ResetDevice() { failed_ = false; }
void NeuralPass::ClearHistory() {}
ID3D11Texture2D* NeuralPass::Process(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, const RECT&, long long, bool) { return nullptr; }
}
#endif
