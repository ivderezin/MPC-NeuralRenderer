// SPDX-License-Identifier: GPL-3.0-or-later
#include "NeuralPass.h"
#ifdef _WIN64
#include "FrameHistory.h"
#include "AsyncOutput.h"
#include "MotionShaders.h"
#include "../FrameGeneration/EchoFrameGeneration.h"
#include "MotionRepair.h"
#include "../FrameGeneration/FrameContentProbe.h"
#include "../FrameGeneration/ExactInputRepeat.h"
#include "../Upscaling/UpscaleStage.h"
#include "../FrameGeneration/GenerationStage.h"
#include "../FrameGeneration/GenerationPolicy.h"
#include "../Processing/HdrComposite.h"
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
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <nvsdk_ngx.h>
#include "../Ngx/Feature.h"
#include "../EchoStorm/engine/ngx_users.h"

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
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Module {
    HMODULE value = nullptr;
    ~Module() {
        if (value)
            FreeLibrary(value);
    }
};
std::filesystem::path ModuleDirectory() {
    HMODULE self = nullptr;
    Check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self)
              ? S_OK
              : HRESULT_FROM_WIN32(GetLastError()),
          "Locate renderer");
    std::array<wchar_t, 32768> path{};
    auto length = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Renderer path too long");
    return std::filesystem::path(path.data()).parent_path();
}
template <class T> T Export(HMODULE module, const char* name) {
    auto address = GetProcAddress(module, name);
    if (!address)
        throw std::runtime_error(std::string("Missing export: ") + name);
    return reinterpret_cast<T>(address);
}
using InitFn = NVSDK_NGX_Result(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version,
                                          const NVSDK_NGX_Parameter*);
using CreateFn = NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*,
                                            NVSDK_NGX_Handle**);
using EvaluateFn = NVSDK_NGX_Result(__cdecl*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
                                              const NVSDK_NGX_Parameter*, void*);
using ReleaseFn = NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Handle*);
using ShutdownFn = NVSDK_NGX_Result(__cdecl*)(ID3D12Device*);

// Feature 18's signed runtime ABI is version-sensitive; contain native faults at the call boundary.
NVSDK_NGX_Result CallInit(InitFn fn, const wchar_t* path, ID3D12Device* device) {
#ifdef _MSC_VER
    __try {
#endif
        return fn(0x0876232Cull, path, device, NVSDK_NGX_Version_API, nullptr);
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NVSDK_NGX_Result_Fail;
    }
#endif
}
NVSDK_NGX_Result CallCreate(CreateFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Parameter* params,
                            NVSDK_NGX_Handle** feature) {
#ifdef _MSC_VER
    __try {
#endif
        return fn(list, static_cast<NVSDK_NGX_Feature>(18), params, feature);
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NVSDK_NGX_Result_Fail;
    }
#endif
}
NVSDK_NGX_Result CallEvaluate(EvaluateFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Handle* feature,
                              NVSDK_NGX_Parameter* params) {
#ifdef _MSC_VER
    __try {
#endif
        return fn(list, feature, params, nullptr);
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NVSDK_NGX_Result_Fail;
    }
#endif
}
void CallRelease(ReleaseFn fn, NVSDK_NGX_Handle* feature) {
#ifdef _MSC_VER
    __try {
#endif
        fn(feature);
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
}
void CallShutdown(ShutdownFn fn, ID3D12Device* device) {
#ifdef _MSC_VER
    __try {
#endif
        fn(device);
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
}
struct SharedTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D12Resource> resource;
    void Create(ID3D11Device* device11, ID3D12Device* device12, UINT width, UINT height, DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Check(device11->CreateTexture2D(&desc, nullptr, &texture), "Create shared texture");
        Check(device11->CreateShaderResourceView(texture.Get(), nullptr, &srv), "Create shared SRV");
        Check(device11->CreateUnorderedAccessView(texture.Get(), nullptr, &uav), "Create shared UAV");
        ComPtr<IDXGIResource1> dxgi;
        Check(texture.As(&dxgi), "Query shared texture");
        Handle handle;
        Check(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                       &handle.value),
              "Share texture");
        Check(device12->OpenSharedHandle(handle.value, IID_PPV_ARGS(&resource)), "Open D3D12 texture");
    }
};
ComPtr<ID3D11ComputeShader> Compile(ID3D11Device* device, const char* source) {
    ComPtr<ID3DBlob> code, errors;
    auto result = D3DCompile(source, strlen(source), "MPC-NeuralRenderer", nullptr, nullptr, "main", "cs_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result) && errors)
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    Check(result, "Compile compute shader");
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader),
          "Create compute shader");
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
    ComPtr<ID3D12QueryHeap> timingQueries;
    ComPtr<ID3D12Resource> timingReadback;
    UINT64 timingFrequency = 0;
    LARGE_INTEGER counterFrequency{}, lastCompletion{};
    double gpuMilliseconds = 0, preparationMilliseconds = 0, completedFps = 0, initMilliseconds = 0;
    ComPtr<ID3D11Query> preparationClock, preparationStart, preparationEnd;
    AsyncOutput asyncOutput;
    ComPtr<ID3D11Fence> fence11;
    Handle fenceEvent;
    UINT64 fenceValue = 0, submitted = 0;
    UINT width = 0, height = 0;
    RECT rect{};
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D11Texture2D> source, previous;
    ComPtr<ID3D11ShaderResourceView> sourceView, previousView;
    ComPtr<ID3D11ComputeShader> prepare, motion, scaleMotion;
    ComPtr<ID3D11Buffer> constants, scaleConstants;
    SharedTexture color, output, vectors, depth, distrust;
    framegen::EchoFrameGeneration echoMotion;
    neural::MotionRepair motionRepair;
    SharedTexture repairedVectors, upscaleDistrust;
    FrameHistory history;
    framegen::FrameContentProbe contentProbe;
    framegen::ExactInputRepeat repeatGate;
    unsigned long long repeatedInputs=0;
    long long lastDistinctTimestamp=0;
    bool haveDistinctTimestamp=false;
    bool diagnosticsEnabled = false, diagnosticsFailed = false;
    Settings options;
    upscaling::Extent target{}, monitor{};
    double refreshHz = 60;
    upscaling::UpscaleStage superResolution;
    framegen::GenerationStage frameGeneration;
    // nrUsable: NR on the source frame; nrPostUsable: NR on the upscaled frame.
    bool nrUsable = false, nrPostUsable = false, srUsable = false, fgUsable = false, fgNeedsReset = true,
         recreateFG = false;
    bool fgLimitedByDisplay = false;
    std::wstring nrFailure, nrPostFailure, srFailure, fgFailure;
    SharedTexture postOutput, postVectors, postDepth;
    SharedTexture scaled;
    bool hdr = false;
    SharedTexture hdrOriginal, hdrOutput;
    processing::HdrComposite hdrComposite;
    std::array<SharedTexture, 3> generated;
    // Completed output is double buffered by batch serial, so a queued batch stays intact while the
    // next one is collected.
    std::array<ComPtr<ID3D11Texture2D>, 2> displayReal;
    std::array<std::array<ComPtr<ID3D11Texture2D>, 3>, 2> displayGenerated;
    std::array<unsigned long long, 2> slotSerial{};
    std::array<unsigned, 2> slotGenerated{};
    std::array<long long,2> slotTimestamp{},slotDuration{};
    long long pendingTimestamp=0,outputTimestamp=0;
    ID3D11Texture2D* published = nullptr;
    ID3D11Texture2D* pendingReal = nullptr;
    bool pendingScaled = false, pendingHasFG = false;
    unsigned generatedCount = 0, discardedBatches = 0;
    unsigned long long serial = 0;
    long long lastInputTimestamp = 0, pendingDuration = 0, outputDuration = 0;
    Module snippet;
    struct NrFeature {
        NVSDK_NGX_Parameter* params = nullptr;
        NVSDK_NGX_Handle* handle = nullptr;
        UINT width = 0, height = 0;
    };
    NrFeature nrPre, nrPost;
    CreateFn create = nullptr;
    EvaluateFn evaluate = nullptr;
    ReleaseFn release = nullptr;
    ShutdownFn shutdown = nullptr;

    void WaitForSubmission() {
        if (!submitted || fence12->GetCompletedValue() >= submitted)
            return;
        Check(fence12->SetEventOnCompletion(submitted, fenceEvent.value), "Fence completion event");
        if (WaitForSingleObject(fenceEvent.value, INFINITE) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU fence wait failed");
    }
    ~Impl() {
        try {
            WaitForSubmission();
        } catch (...) {
        }
        superResolution.Reset();
        frameGeneration.Reset();
        for (auto* nr : {&nrPre, &nrPost}) {
            if (nr->handle && release)
                CallRelease(release, nr->handle);
            if (nr->params)
                NVSDK_NGX_D3D12_DestroyParameters(nr->params);
        }
        if (snippetInitialized && shutdown)
            CallShutdown(shutdown, device12.Get());
        if (coreInitialized && nr::ngxusers::Leaving())
            NVSDK_NGX_D3D12_Shutdown1(device12.Get());
        if (lease)
            SessionActive.clear();
    }
    UINT64 Completed() const {
        const auto completed = fence12->GetCompletedValue();
        if (completed == UINT64_MAX) {
            Check(device12->GetDeviceRemovedReason(), "NR device removed");
            throw std::runtime_error("NR fence unavailable");
        }
        return completed;
    }
    void Invalidate() {
        contentProbe.Invalidate();
        repeatGate.Invalidate(); haveDistinctTimestamp=false;
        history.Clear();
        asyncOutput.Invalidate();
        lastCompletion = {};
        completedFps = 0;
        generatedCount = 0;
        published = nullptr;
        fgNeedsReset = true;
    }
    void ReadTiming() {
        const D3D12_RANGE readRange{0, sizeof(UINT64) * 2};
        void* data = nullptr;
        Check(timingReadback->Map(0, &readRange, &data), "Read NR timing");
        const auto* timestamps = static_cast<const UINT64*>(data);
        gpuMilliseconds =
            1000.0 * static_cast<double>(timestamps[1] - timestamps[0]) / static_cast<double>(timingFrequency);
        const D3D12_RANGE writtenRange{0, 0};
        timingReadback->Unmap(0, &writtenRange);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
        UINT64 start = 0, end = 0;
        if (context11->GetData(preparationClock.Get(), &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            !clock.Disjoint && clock.Frequency &&
            context11->GetData(preparationStart.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context11->GetData(preparationEnd.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
            preparationMilliseconds = 1000.0 * static_cast<double>(end - start) / static_cast<double>(clock.Frequency);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (lastCompletion.QuadPart && now.QuadPart > lastCompletion.QuadPart) {
            const double fps = static_cast<double>(counterFrequency.QuadPart) /
                               static_cast<double>(now.QuadPart - lastCompletion.QuadPart);
            completedFps = completedFps ? completedFps * 0.8 + fps * 0.2 : fps;
        }
        lastCompletion = now;
    }
    std::wstring Status() const {
        std::wostringstream message;
        message << L"Motion: " << neural::Name(options.motionMethod) << (options.motionMethod==MotionMethod::EchoStorm ? L" (native D3D12)" : L"") << L"\n";
        if (hdr) message << L"HDR: PQ / BT.2020, SDR proxy + linear-light residual, 10-bit output\n";
        if (options.enabled) {
            if (!nrUsable && !nrPostUsable)
                message << L"NR: " << (nrFailure.empty() ? nrPostFailure : nrFailure);
            else {
                message << L"NR: " << (asyncOutput.Available() ? L"Active" : L"Starting");
                if (nrUsable)
                    message << L" " << width << L"x" << height << (srUsable ? L" before upscaling" : L"");
                if (nrPostUsable)
                    message << (nrUsable ? L" +" : L"") << L" " << target.width << L"x" << target.height
                            << L" after upscaling";
                if (!nrPostFailure.empty())
                    message << L" (after-upscaling pass: " << nrPostFailure << L")";
            }
        } else
            message << (options.echoPipeline ? L"NR stage: independent presentation composition" : L"NR: Off");
        message << L"\n" << processing::Name(options.upscaler) << L" upscaling: ";
        if (srUsable)
            message << width << L"x" << height << L" -> " << target.width << L"x" << target.height;
        else if (!srFailure.empty())
            message << srFailure;
        else
            message << ((options.upscaler != processing::Upscaler::Off) ? L"not needed (native size / downscale)"
                                                                        : L"Off");
        if (options.FsrNative())
            message << L" (AMDLSS5 bridge inputs; external NR unverified)";
        if (options.ManualGeneration() && options.frameGenerator == processing::FrameGenerator::Dlss && options.motionMethod == MotionMethod::EchoStorm)
            message << L"\nDLSS FG guidance: native EchoStorm, final-frame resolution";
        if (options.frameGenerator != processing::FrameGenerator::Xess) {
            message << L"\n" << processing::Name(options.frameGenerator) << L": ";
            if (fgUsable && fgLimitedByDisplay)
                message << L"bypass (display refresh limit)";
            else if (fgUsable)
                message << L"up to " << (std::min)(options.frameMultiplier, frameGeneration.MaxGenerated() + 1)
                        << L"x, latest batch " << generatedCount << L" generated, discarded batches "
                        << discardedBatches;
            else
                message << (options.ManualGeneration() ? fgFailure : L"Off");
        }
        if (options.frameGenerator==processing::FrameGenerator::Dlss && options.ManualGeneration())
            message << (options.dlssFgGuard ? L"\nDLSS post-FG guard: on" : L"\nDLSS post-FG guard: off (SDK output)");
        if (asyncOutput.Available())
            message << std::fixed << std::setprecision(1) << L"\nProcessing: " << completedFps << L" fps, GPU "
                    << gpuMilliseconds << L" ms, prepare " << preparationMilliseconds << L" ms (async), init "
                    << initMilliseconds << L" ms";
        if (options.frameGenerator==processing::FrameGenerator::Dlss && !frameGeneration.RuntimeStatus().empty())
            message << L"\n" << frameGeneration.RuntimeStatus() << L"\nDLSS FG: finite flat-depth / orthographic capture fallback";
        if (options.echoPipeline) message << L"\nExact repeated input batches suppressed: " << repeatedInputs;
        if (diagnosticsEnabled && contentProbe.metrics.samples) {
            const auto& data=contentProbe.metrics;
            message << std::fixed << std::setprecision(3) << L"\nContent samples: raw delta " << data.delta[0]
                    << L", real delta " << data.delta[1] << L" /255; unchanged input "
                    << data.unchangedInputs << L"/" << data.samples;
            if (data.generated) {
                message << L"\nFG sampled delta (current/previous real): ";
                for (unsigned i=0;i<data.generated;++i)
                    message << (i ? L", " : L"") << data.delta[2+i*2] << L"/" << data.delta[3+i*2];
            }
        } else if (diagnosticsEnabled && diagnosticsFailed)
            message << L"\nContent samples: unavailable (probe failed; processing unchanged)";
        return message.str();
    }
    void Begin() {
        if (asyncOutput.Busy(Completed()))
            throw std::logic_error("NR allocator is still in use");
        Check(allocator->Reset(), "Reset command allocator");
        Check(list->Reset(allocator.Get(), nullptr), "Reset command list");
    }
    void Submit(bool producesOutput) {
        Check(list->Close(), "Close command list");
        ID3D12CommandList* commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        const auto signal = ++fenceValue;
        Check(queue->Signal(fence12.Get(), signal), "Signal D3D12 fence");
        submitted = signal;
        asyncOutput.Submit(signal, producesOutput);
    }
    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* frame, RECT videoRect,
                    Settings settings, upscaling::Extent targetSize, upscaling::Extent monitorSize,
                    double displayRefresh, bool hdrInput) {
        options = settings;
        hdr = hdrInput;
        target = targetSize;
        monitor = monitorSize;
        refreshHz = displayRefresh;
        if (SessionActive.test_and_set())
            throw std::runtime_error("Another neural renderer is active in this process");
        lease = true;
        device11 = device;
        Check(context->QueryInterface(IID_PPV_ARGS(&context11)), "D3D11.4 context required");
        ComPtr<IDXGIDevice> dxgi;
        Check(device->QueryInterface(IID_PPV_ARGS(&dxgi)), "Query DXGI device");
        ComPtr<IDXGIAdapter> adapter;
        Check(dxgi->GetAdapter(&adapter), "Get GPU adapter");
        DXGI_ADAPTER_DESC adapterDesc{};
        Check(adapter->GetDesc(&adapterDesc), "Get GPU description");

        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)), "Create D3D12 device");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        Check(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create command queue");
        Check(queue->GetTimestampFrequency(&timingFrequency), "Get NR timing frequency");
        if (!timingFrequency || !QueryPerformanceFrequency(&counterFrequency))
            throw std::runtime_error("NR timing unavailable");
        D3D12_QUERY_HEAP_DESC queryDesc{};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = 2;
        Check(device12->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&timingQueries)), "Create NR timing queries");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC readbackDesc{};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Width = sizeof(UINT64) * 2;
        readbackDesc.Height = readbackDesc.DepthOrArraySize = readbackDesc.MipLevels = 1;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Check(device12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&timingReadback)),
              "Create NR timing readback");
        Check(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
              "Create allocator");
        Check(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&list)),
              "Create command list");
        Check(list->Close(), "Close initial list");
        Check(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)), "Create shared fence");
        Handle fenceHandle;
        Check(device12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &fenceHandle.value),
              "Share fence");
        ComPtr<ID3D11Device5> device5;
        Check(device->QueryInterface(IID_PPV_ARGS(&device5)), "D3D11.5 device required");
        Check(device5->OpenSharedFence(fenceHandle.value, IID_PPV_ARGS(&fence11)), "Open D3D11 fence");
        fenceEvent.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(fenceEvent.value ? S_OK : HRESULT_FROM_WIN32(GetLastError()), "Create fence event");

        rect = videoRect;
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
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
        if (options.motionMethod == MotionMethod::EchoStorm || options.ManualGeneration()) {
            repairedVectors.Create(device, device12.Get(), width, height, DXGI_FORMAT_R16G16_FLOAT);
            if (options.motionMethod == MotionMethod::Fast) motionRepair.Initialize(device, width, height);
        }
        if (options.motionMethod == MotionMethod::EchoStorm) {
            distrust.Create(device, device12.Get(), width, height, DXGI_FORMAT_R8_UNORM);
            upscaleDistrust.Create(device, device12.Get(), width, height, DXGI_FORMAT_R8_UNORM);
            echoMotion.Create(device12.Get(), width, height, false, true);
        }
        depth.Create(device, device12.Get(), width, height, DXGI_FORMAT_R32_FLOAT);
        color.texture->GetDesc(&desc);
        desc.MiscFlags = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Check(device->CreateTexture2D(&desc, nullptr, &previous), "Create previous frame");
        Check(device->CreateShaderResourceView(previous.Get(), nullptr, &previousView), "Create previous SRV");
        if (hdr) {
            hdrOriginal.Create(device, device12.Get(), width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
            hdrComposite.Create(device12.Get());
        }
        prepare = Compile(device, PrepareShader.c_str());
        motion = Compile(device, MotionShader);
        scaleMotion = Compile(device, ScaleMotionShader);
        D3D11_QUERY_DESC preparationQuery{};
        preparationQuery.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        Check(device->CreateQuery(&preparationQuery, &preparationClock), "Create preparation clock query");
        preparationQuery.Query = D3D11_QUERY_TIMESTAMP;
        Check(device->CreateQuery(&preparationQuery, &preparationStart), "Create preparation start query");
        Check(device->CreateQuery(&preparationQuery, &preparationEnd), "Create preparation end query");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 32;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Check(device->CreateBuffer(&buffer, nullptr, &constants), "Create constants");
        buffer.ByteWidth = 16;
        Check(device->CreateBuffer(&buffer, nullptr, &scaleConstants), "Create motion scale constants");
        const float zero[4]{};
        context11->ClearUnorderedAccessViewFloat(depth.uav.Get(), zero);
        context11->ClearUnorderedAccessViewFloat(vectors.uav.Get(), zero);

        auto directory = ModuleDirectory();
        std::wstring ngxFailure;
        const auto modelDir = directory.wstring();
        if (options.enabled || options.upscaler == processing::Upscaler::Dlss ||
            (options.ManualGeneration() && options.frameGenerator == processing::FrameGenerator::Dlss)) {
            try {
                if (adapterDesc.VendorId != 0x10de)
                    throw std::runtime_error("NVIDIA GPU required for NR / DLSS");
                std::array<wchar_t, MAX_PATH> temp{};
                auto tempLength = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
                if (!tempLength || tempLength >= temp.size())
                    throw std::runtime_error("NGX cache path unavailable");
                auto cache = std::filesystem::path(temp.data()) / L"MPC-NeuralRenderer";
                std::filesystem::create_directories(cache);
                const wchar_t* paths[] = {modelDir.c_str()};
                NVSDK_NGX_FeatureCommonInfo info{};
                info.PathListInfo.Path = paths;
                info.PathListInfo.Length = 1;
                CheckNgx(NVSDK_NGX_D3D12_Init_with_ProjectID("80b3e1a9-7dab-4bb3-b64c-5d923d8cbf40",
                                                             NVSDK_NGX_ENGINE_TYPE_CUSTOM, "MPC-NeuralRenderer-0.1.0",
                                                             cache.c_str(), device12.Get(), &info),
                         "Initialize driver NGX");
                coreInitialized = true;
                nr::ngxusers::Joined();
            } catch (const std::exception& error) {
                ngxFailure = ngx::Message(error);
            }
        }
        Begin();
        bool nrLoaded = false;
        if (options.enabled) {
            try {
                if (!coreInitialized)
                    throw std::runtime_error(std::string(ngxFailure.begin(), ngxFailure.end()));
                const auto model = directory / L"nvngx_dlssnr.dll";
                snippet.value = LoadLibraryExW(model.c_str(), nullptr,
                                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                Check(snippet.value ? S_OK : HRESULT_FROM_WIN32(GetLastError()),
                      "Load nvngx_dlssnr.dll beside renderer");
                auto init = Export<InitFn>(snippet.value, "NVSDK_NGX_D3D12_Init_Ext");
                create = Export<CreateFn>(snippet.value, "NVSDK_NGX_D3D12_CreateFeature");
                evaluate = Export<EvaluateFn>(snippet.value, "NVSDK_NGX_D3D12_EvaluateFeature");
                release = Export<ReleaseFn>(snippet.value, "NVSDK_NGX_D3D12_ReleaseFeature");
                shutdown = Export<ShutdownFn>(snippet.value, "NVSDK_NGX_D3D12_Shutdown1");
                CheckNgx(CallInit(init, modelDir.c_str(), device12.Get()), "Initialize NR model");
                snippetInitialized = true;
                nrLoaded = true;
            } catch (const std::exception& error) {
                nrFailure = ngx::Message(error);
            }
        }
        if ((options.upscaler != processing::Upscaler::Off) &&
            (upscaling::ShouldUpscale({width, height}, target, monitor) || options.FsrNative())) {
            try {
                scaled.Create(device, device12.Get(), target.width, target.height, DXGI_FORMAT_R8G8B8A8_UNORM);
                if (options.upscaler == processing::Upscaler::Dlss && !coreInitialized)
                    throw std::runtime_error(std::string(ngxFailure.begin(), ngxFailure.end()));
                auto runtimeDirectory = directory;
                if (options.FsrNative()) {
                    std::array<wchar_t, 32768> executable{};
                    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
                    if (!length || length >= executable.size())
                        throw std::runtime_error("Player path unavailable");
                    const auto playerDirectory = std::filesystem::path(executable.data()).parent_path();
                    if (std::filesystem::exists(playerDirectory / L"amd_fidelityfx_upscaler_dx12.dll"))
                        runtimeDirectory = playerDirectory;
                }
                superResolution.Create(options.upscaler, device12.Get(), list.Get(), {width, height}, target,
                                       runtimeDirectory, options.motionMethod == MotionMethod::EchoStorm);
                srUsable = true;
            } catch (const std::exception& error) {
                srFailure = ngx::Message(error);
            }
        }
        if (nrLoaded) {
            // Without an upscaled frame the after-upscaling pass falls back to the source frame.
            const bool upscaled = srUsable && (target.width > width || target.height > height);
            const bool post = upscaled && options.nrStage != NrStage::BeforeUpscale;
            const bool pre = !upscaled || options.nrStage != NrStage::AfterUpscale;
            if (pre) {
                try {
                    CreateNr(nrPre, width, height);
                    nrUsable = true;
                } catch (const std::exception& error) {
                    nrFailure = ngx::Message(error);
                }
            }
            if (post) {
                try {
                    postOutput.Create(device, device12.Get(), target.width, target.height, DXGI_FORMAT_R8G8B8A8_UNORM);
                    postVectors.Create(device, device12.Get(), target.width, target.height, DXGI_FORMAT_R16G16_FLOAT);
                    postDepth.Create(device, device12.Get(), target.width, target.height, DXGI_FORMAT_R32_FLOAT);
                    context11->ClearUnorderedAccessViewFloat(postDepth.uav.Get(), zero);
                    context11->ClearUnorderedAccessViewFloat(postVectors.uav.Get(), zero);
                    CreateNr(nrPost, target.width, target.height);
                    nrPostUsable = true;
                } catch (const std::exception& error) {
                    nrPostFailure = ngx::Message(error);
                }
            }
        }
        if (hdr)
            hdrOutput.Create(device, device12.Get(), srUsable ? target.width : width,
                srUsable ? target.height : height, DXGI_FORMAT_R10G10B10A2_UNORM);
        if (options.ManualGeneration()) {
            try {
                CreateFrameGeneration();
            } catch (const std::exception& error) {
                fgFailure = ngx::Message(error);
            }
        }
        Submit(false);
    }
    void CreateFrameGeneration() {
        frameGeneration.Reset();
        const auto outWidth = srUsable ? target.width : width;
        const auto outHeight = srUsable ? target.height : height;
        if (options.frameGenerator == processing::FrameGenerator::Dlss && !coreInitialized)
            throw std::runtime_error("Driver NGX initialization failed");
        frameGeneration.Create(options.frameGenerator, device12.Get(), list.Get(), width, height, outWidth, outHeight,
                               ModuleDirectory(), hdr, options.motionMethod == MotionMethod::EchoStorm, options.dlssFgGuard);
        for (unsigned i = 0; i < 3; ++i) {
            generated[i].Create(device11.Get(), device12.Get(), outWidth, outHeight, hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
            for (auto& slot : displayGenerated)
                DisplayCopy(slot[i], generated[i].texture.Get());
        }
        fgUsable = true;
        fgNeedsReset = true;
        recreateFG = false;
    }
    void CreateNr(NrFeature& nr, UINT w, UINT h) {
        CheckNgx(NVSDK_NGX_D3D12_AllocateParameters(&nr.params), "Allocate NGX parameters");
        nr.width = w;
        nr.height = h;
        auto* params = nr.params;
        for (const auto* key :
             {"DLSSNR.Width", "DLSSNR.InputWidth", "DLSSNR.OutputWidth", "DLSSNR.Output.Width", "Width", "OutWidth"})
            params->Set(key, w);
        for (const auto* key : {"DLSSNR.Height", "DLSSNR.InputHeight", "DLSSNR.OutputHeight", "DLSSNR.Output.Height",
                                "Height", "OutHeight"})
            params->Set(key, h);
        params->Set("DLSSNR.Upscaling", 0u);
        params->Set("DLSSNR.Scale", 1.0f);
        params->Set("DLSSNR.ScalingRatio", 1.0f);
        params->Set("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&ScalingRatio));
        params->Set("DLSSNR.Hint.Render.Preset", 0u);
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
        params->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
        params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        CheckNgx(CallCreate(create, list.Get(), params, &nr.handle), "Create NR feature");
        if (!nr.handle)
            throw std::runtime_error("NR returned no feature handle");
    }
    NVSDK_NGX_Result EvaluateNr(NrFeature& nr, ID3D12Resource* input, ID3D12Resource* result,
                                ID3D12Resource* motionVectors, ID3D12Resource* depthInput, bool reset) {
        auto* params = nr.params;
        params->Set("DLSSNR.Color", input);
        params->Set("DLSSNR.Output", result);
        params->Set("DLSSNR.MVec", motionVectors);
        params->Set("DLSSNR.Depth", depthInput);
        for (const auto* name : {"Color", "Output", "MVec", "Depth"})
            Subrect(params, name, nr.width, nr.height);
        params->Set("DLSSNR.MVecScaleX", 1.0f);
        params->Set("DLSSNR.MVecScaleY", 1.0f);
        params->Set("DLSSNR.DepthInverted", 1);
        params->Set("DLSSNR.Enabled", 1);
        params->Set("DLSSNR.Reset", reset ? 1 : 0);
        params->Set("DLSSNR.Style", 0);
        params->Set("DLSSNR.Intensity", options.intensity / 100.0f);
        params->Set("DLSSNR.LocalToneStrength", 1.0f);
        params->Set("DLSSNR.LocalStructureStrength", 1.0f);
        params->Set("DLSSNR.SkinStructureStrength", 1.0f);
        params->Set("DLSSNR.UseAutoMask", 1);
        params->Set("DLSSNR.UICorrection", 1);
        return CallEvaluate(evaluate, list.Get(), nr.handle, params);
    }
    // Creates or reuses a shader-readable copy target matching source.
    void DisplayCopy(ComPtr<ID3D11Texture2D>& target, ID3D11Texture2D* source) {
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if (target) {
            D3D11_TEXTURE2D_DESC current{};
            target->GetDesc(&current);
            if (current.Width == desc.Width && current.Height == desc.Height && current.Format == desc.Format)
                return;
            target.Reset();
        }
        desc.MiscFlags = desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Check(device11->CreateTexture2D(&desc, nullptr, &target), "Create completed display frame");
    }
    void Transition(bool begin, bool outputRead = false) {
        std::array<D3D12_RESOURCE_BARRIER, 4> barriers{};
        const std::array<ID3D12Resource*, 4> resources = {color.resource.Get(), output.resource.Get(),
                                                          vectors.resource.Get(), depth.resource.Get()};
        for (size_t i = 0; i < resources.size(); ++i) {
            auto state = i == 1 && !outputRead ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                               : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            auto& transition = barriers[i].Transition;
            transition.pResource = resources[i];
            transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            transition.StateBefore = begin ? D3D12_RESOURCE_STATE_COMMON : state;
            transition.StateAfter = begin ? state : D3D12_RESOURCE_STATE_COMMON;
        }
        list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        if (distrust.resource)
            ngx::Transition(list.Get(), distrust.resource.Get(),
                begin ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                begin ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON);
        if (repairedVectors.resource)
            ngx::Transition(list.Get(), repairedVectors.resource.Get(),
                begin ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                begin ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON);
        if (upscaleDistrust.resource)
            ngx::Transition(list.Get(), upscaleDistrust.resource.Get(),
                begin ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                begin ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON);
    }
    ID3D11Texture2D* Run(ID3D11Texture2D* frame, long long timestamp, float defaultFrameTimeMs) {
        if (diagnosticsEnabled && !diagnosticsFailed) {
            try { contentProbe.Collect(context11.Get()); }
            catch (...) { diagnosticsFailed=true; }
        }
        if (!nrUsable && !nrPostUsable && !srUsable && !fgUsable && !recreateFG) {
            asyncOutput.Invalidate();
            published = nullptr;
            generatedCount = 0;
            return nullptr;
        }
        const auto action = history.Classify(timestamp);
        if (action == FrameHistory::Action::Reset) {
            contentProbe.Invalidate();
            asyncOutput.Invalidate();
            published = nullptr;
            generatedCount = 0;
        }
        const auto completed = Completed();
        if (asyncOutput.Busy(completed))
            return asyncOutput.Available() ? published : nullptr;
        if (asyncOutput.Collect(completed)) {
            // Already completed: acquire the shared resource without waiting for inference.
            Check(context11->Wait(fence11.Get(), submitted), "Acquire completed NR output");
            const bool repeated=options.echoPipeline && repeatGate.Same(context11.Get());
            if (repeated) {
                if (pendingHasFG) frameGeneration.Collect();
                ++repeatedInputs;
            } else {
            ++serial;
            const auto slot = static_cast<size_t>(serial & 1);
            DisplayCopy(displayReal[slot], pendingReal);
            published = displayReal[slot].Get();
            context11->CopyResource(published, pendingReal);
            generatedCount = pendingHasFG ? frameGeneration.Collect() : 0;
            if (pendingHasFG && !generatedCount)
                ++discardedBatches;
            for (unsigned i = 0; i < generatedCount; ++i)
                context11->CopyResource(displayGenerated[slot][i].Get(), generated[i].texture.Get());
            if (diagnosticsEnabled && !diagnosticsFailed) {
                std::array<ID3D11Texture2D*,3> probeFrames{};
                for (unsigned i=0;i<generatedCount;++i) probeFrames[i]=displayGenerated[slot][i].Get();
                try { contentProbe.Submit(context11.Get(), color.texture.Get(), published, probeFrames, generatedCount); }
                catch (...) { diagnosticsFailed=true; }
            }
            slotSerial[slot] = serial;
            slotGenerated[slot] = generatedCount;
            slotTimestamp[slot] = pendingTimestamp;
            slotDuration[slot] = pendingDuration;
            outputTimestamp = pendingTimestamp;
            outputDuration = pendingDuration;
            ReadTiming();
            lastDistinctTimestamp=pendingTimestamp;haveDistinctTimestamp=true;
            }
        }
        if (action == FrameHistory::Action::Repeat)
            return asyncOutput.Available() ? published : nullptr;
        Begin();
        bool reset = action == FrameHistory::Action::Reset;
        if (recreateFG) {
            try {
                CreateFrameGeneration();
            } catch (const std::exception& error) {
                recreateFG = false;
                fgFailure = ngx::Message(error);
            }
        }
        const float frameTimeMs =
            !reset && timestamp > (options.echoPipeline && haveDistinctTimestamp ? lastDistinctTimestamp : lastInputTimestamp)
                ? std::clamp(static_cast<float>(timestamp - (options.echoPipeline && haveDistinctTimestamp ? lastDistinctTimestamp : lastInputTimestamp)) / 10000.0f, 1.0f, 500.0f)
                : defaultFrameTimeMs;
        pendingDuration = static_cast<long long>(frameTimeMs * 10000.0f);
        pendingTimestamp = timestamp;
        lastInputTimestamp = timestamp;
        const UINT data[] = {
            width, height, static_cast<UINT>(rect.left), static_cast<UINT>(rect.top), reset ? 1u : 0u, hdr ? 1u : 0u, 0, 0};
        context11->OMSetRenderTargets(0, nullptr, nullptr);
        context11->Begin(preparationClock.Get());
        context11->End(preparationStart.Get());
        context11->CopyResource(source.Get(), frame);
        if (options.echoPipeline) repeatGate.Record(context11.Get(),source.Get(),sourceView.Get(),rect,reset);
        context11->UpdateSubresource(constants.Get(), 0, nullptr, data, 0, 0);
        ID3D11Buffer* cb[] = {constants.Get()};
        context11->CSSetConstantBuffers(0, 1, cb);
        ID3D11ShaderResourceView* input[] = {sourceView.Get(), nullptr};
        ID3D11UnorderedAccessView* target[] = {color.uav.Get()};
        context11->CSSetShader(prepare.Get(), nullptr, 0);
        context11->CSSetShaderResources(0, 2, input);
        ID3D11UnorderedAccessView* preparationTargets[] = {color.uav.Get(), hdr ? hdrOriginal.uav.Get() : nullptr};
        context11->CSSetUnorderedAccessViews(0, 2, preparationTargets, nullptr);
        context11->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        preparationTargets[0] = preparationTargets[1] = nullptr;
        context11->CSSetUnorderedAccessViews(0, 2, preparationTargets, nullptr);
        target[0] = nullptr;
        context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
        if (reset)
            context11->CopyResource(previous.Get(), color.texture.Get());
        if (options.motionMethod == MotionMethod::Fast) {
            input[0] = color.srv.Get();
            input[1] = previousView.Get();
            target[0] = vectors.uav.Get();
            context11->CSSetShader(motion.Get(), nullptr, 0);
            context11->CSSetShaderResources(0, 2, input);
            context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
            context11->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
            input[0] = input[1] = nullptr;
            target[0] = nullptr;
            cb[0] = nullptr;
            context11->CSSetShaderResources(0, 2, input);
            context11->CSSetUnorderedAccessViews(0, 1, target, nullptr);
            context11->CSSetConstantBuffers(0, 1, cb);
            context11->CSSetShader(nullptr, nullptr, 0);
        }
        // Resolve flat/repeating patches before any temporal consumer uses the field.
        if (repairedVectors.resource && options.motionMethod == MotionMethod::Fast)
            motionRepair.Dispatch(context11.Get(), previousView.Get(), color.srv.Get(),
                vectors.srv.Get(), distrust.srv.Get(), repairedVectors.uav.Get(), upscaleDistrust.uav.Get(),
                srUsable ? static_cast<float>(width) * (this->target.width == width && this->target.height == height ? .001f : .0005f) : 0.f);
        auto& temporalVectors = options.motionMethod == MotionMethod::EchoStorm ? repairedVectors : vectors;
        if (nrPostUsable && srUsable && options.motionMethod == MotionMethod::Fast) {
            // The after-upscaling NR pass needs motion at the output resolution.
            const UINT scaleData[] = {width, height, this->target.width, this->target.height};
            context11->UpdateSubresource(scaleConstants.Get(), 0, nullptr, scaleData, 0, 0);
            ID3D11Buffer* scaleBuffer[] = {scaleConstants.Get()};
            ID3D11ShaderResourceView* scaleInput[] = {temporalVectors.srv.Get()};
            ID3D11UnorderedAccessView* scaleOutput[] = {postVectors.uav.Get()};
            context11->CSSetConstantBuffers(0, 1, scaleBuffer);
            context11->CSSetShader(scaleMotion.Get(), nullptr, 0);
            context11->CSSetShaderResources(0, 1, scaleInput);
            context11->CSSetUnorderedAccessViews(0, 1, scaleOutput, nullptr);
            context11->Dispatch((this->target.width + 7) / 8, (this->target.height + 7) / 8, 1);
            scaleBuffer[0] = nullptr;
            scaleInput[0] = nullptr;
            scaleOutput[0] = nullptr;
            context11->CSSetShaderResources(0, 1, scaleInput);
            context11->CSSetUnorderedAccessViews(0, 1, scaleOutput, nullptr);
            context11->CSSetConstantBuffers(0, 1, scaleBuffer);
            context11->CSSetShader(nullptr, nullptr, 0);
        }
        context11->CopyResource(previous.Get(), color.texture.Get());
        context11->End(preparationEnd.Get());
        context11->End(preparationClock.Get());
        const auto ready = ++fenceValue;
        Check(context11->Signal(fence11.Get(), ready), "Signal D3D11 fence");
        context11->Flush();
        Check(queue->Wait(fence12.Get(), ready), "D3D12 wait");
        Transition(true);
        list->EndQuery(timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (options.motionMethod == MotionMethod::EchoStorm && (nrUsable || nrPostUsable || srUsable)) {
            echoMotion.Dispatch(list.Get(),color.resource.Get(),{},0,reset);
            const float fastMotion=srUsable ? static_cast<float>(width)*(this->target.width==width && this->target.height==height ? .001f : .0005f) : 0.f;
            echoMotion.WriteGuidance(list.Get(),repairedVectors.resource.Get(),upscaleDistrust.resource.Get(),fastMotion);
        }
        ID3D12Resource* finalColor = color.resource.Get();
        pendingReal = color.texture.Get();
        pendingScaled = false;
        pendingHasFG = false;
        bool outputRead = false;
        if (nrUsable) {
            const auto result = EvaluateNr(nrPre, color.resource.Get(), output.resource.Get(), temporalVectors.resource.Get(),
                                           depth.resource.Get(), reset);
            if (result == NVSDK_NGX_Result_Success) {
                ngx::Transition(list.Get(), output.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                outputRead = true;
                finalColor = output.resource.Get();
                pendingReal = output.texture.Get();
            } else {
                nrUsable = false;
                reset = true;
                fgNeedsReset = true;
                try {
                    CheckNgx(result, "Evaluate NR");
                } catch (const std::exception& error) {
                    nrFailure = ngx::Message(error);
                }
            }
        }
        if (srUsable) {
            ngx::Transition(list.Get(), scaled.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            try {
                superResolution.Dispatch(list.Get(), finalColor, depth.resource.Get(), temporalVectors.resource.Get(),
                                         scaled.resource.Get(), reset, frameTimeMs, upscaleDistrust.resource.Get());
                ngx::Transition(list.Get(), scaled.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                finalColor = scaled.resource.Get();
                pendingReal = scaled.texture.Get();
                pendingScaled = true;
            } catch (const std::exception& error) {
                ngx::Transition(list.Get(), scaled.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_COMMON);
                srUsable = false;
                fgUsable = false;
                recreateFG = options.ManualGeneration();
                fgFailure = L"reinitializing at native resolution";
                srFailure = ngx::Message(error);
            }
        }
        bool postActive = false, postRead = false;
        if (nrPostUsable && pendingScaled) {
            postActive = true;
            ngx::Transition(list.Get(), postOutput.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ngx::Transition(list.Get(),postVectors.resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (options.motionMethod == MotionMethod::EchoStorm) echoMotion.WriteGuidance(list.Get(),postVectors.resource.Get());
            ngx::Transition(list.Get(),postDepth.resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            const auto result = EvaluateNr(nrPost, scaled.resource.Get(), postOutput.resource.Get(),
                                           postVectors.resource.Get(), postDepth.resource.Get(), reset);
            if (result == NVSDK_NGX_Result_Success) {
                ngx::Transition(list.Get(), postOutput.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                postRead = true;
                finalColor = postOutput.resource.Get();
                pendingReal = postOutput.texture.Get();
            } else {
                nrPostUsable = false;
                try {
                    CheckNgx(result, "Evaluate NR after upscaling");
                } catch (const std::exception& error) {
                    nrPostFailure = ngx::Message(error);
                }
            }
        }
        if (hdr) {
            D3D11_TEXTURE2D_DESC hdrDesc{};
            hdrOutput.texture->GetDesc(&hdrDesc);
            const auto hdrWidth = pendingScaled ? this->target.width : width;
            const auto hdrHeight = pendingScaled ? this->target.height : height;
            if (hdrDesc.Width != hdrWidth || hdrDesc.Height != hdrHeight)
                hdrOutput.Create(device11.Get(), device12.Get(), hdrWidth, hdrHeight, DXGI_FORMAT_R10G10B10A2_UNORM);
            ngx::Transition(list.Get(), hdrOriginal.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ngx::Transition(list.Get(), hdrOutput.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            hdrComposite.Dispatch(list.Get(), finalColor, hdrOriginal.resource.Get(), color.resource.Get(), hdrOutput.resource.Get());
            ngx::Transition(list.Get(), hdrOutput.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ngx::Transition(list.Get(), hdrOriginal.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COMMON);
            finalColor = hdrOutput.resource.Get();
            pendingReal = hdrOutput.texture.Get();
        }
        if (fgUsable) {
            const auto count = framegen::GeneratedLimit(options.frameMultiplier, frameGeneration.MaxGenerated(),
                                                        refreshHz, frameTimeMs, options.limitGenerationToDisplay);
            fgLimitedByDisplay = count == 0;
            if (count) {
                std::array<ID3D12Resource*, 3> outputs{};
                for (unsigned i = 0; i < count; ++i) {
                    outputs[i] = generated[i].resource.Get();
                    ngx::Transition(list.Get(), outputs[i], D3D12_RESOURCE_STATE_COMMON,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }
                try {
                    frameGeneration.Dispatch(list.Get(), finalColor, depth.resource.Get(), repairedVectors.resource.Get(),
                                             outputs, count, reset || fgNeedsReset, frameTimeMs);
                    pendingHasFG = true;
                    fgNeedsReset = false;
                } catch (const std::exception& error) {
                    fgUsable = false;
                    fgFailure = ngx::Message(error);
                }
                for (unsigned i = 0; i < count; ++i)
                    ngx::Transition(list.Get(), outputs[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_COMMON);
            } else
                fgNeedsReset = true;
        }
        if (hdr)
            ngx::Transition(list.Get(), hdrOutput.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COMMON);
        if (pendingScaled)
            ngx::Transition(list.Get(), scaled.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_COMMON);
        list->EndQuery(timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        list->ResolveQueryData(timingQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timingReadback.Get(), 0);
        if (postActive) {
            ngx::Transition(list.Get(), postOutput.resource.Get(),
                            postRead ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                     : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COMMON);
            for (auto* resource : {postVectors.resource.Get(), postDepth.resource.Get()})
                ngx::Transition(list.Get(), resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_COMMON);
        }
        Transition(false, outputRead);
        Submit(true);
        history.Commit(timestamp);
        return asyncOutput.Available() ? published : nullptr;
    }
};
unsigned NeuralPass::GeneratedCount() const {
    return impl_ && impl_->asyncOutput.Available() ? impl_->generatedCount : 0;
}
unsigned long long NeuralPass::OutputSerial() const {
    return impl_ && impl_->asyncOutput.Available() ? impl_->serial : 0;
}
long long NeuralPass::OutputDuration() const {
    return impl_ ? impl_->outputDuration : 0;
}
long long NeuralPass::OutputTimestamp() const { return impl_ ? impl_->outputTimestamp : 0; }
long long NeuralPass::PresentationTimestamp(unsigned long long batch,unsigned index) const {
    if (!impl_) return 0;
    const auto slot=static_cast<size_t>(batch&1);
    if (impl_->slotSerial[slot]!=batch) return 0;
    const auto count=impl_->slotGenerated[slot];
    const auto remaining=index<count ? count-index : 0;
    return impl_->slotTimestamp[slot]-impl_->slotDuration[slot]*remaining/(count+1);
}
ID3D11Texture2D* NeuralPass::PresentationFrame(unsigned long long batch, unsigned index) const {
    if (!impl_ || !impl_->asyncOutput.Available())
        return nullptr;
    const auto slot = static_cast<size_t>(batch & 1);
    if (impl_->slotSerial[slot] != batch)
        return nullptr;
    return index < impl_->slotGenerated[slot] ? impl_->displayGenerated[slot][index].Get()
                                               : impl_->displayReal[slot].Get();
}
NeuralPass::NeuralPass() = default;
NeuralPass::~NeuralPass() = default;
void NeuralPass::Configure(Settings settings) {
    settings.intensity = std::clamp(settings.intensity, 0, 200);
    settings.frameMultiplier = std::clamp(settings.frameMultiplier, 1u, 4u);
    settings.presentThread = false; // presentation mode does not affect processing
    if (settings_ == settings)
        return;
    auto resources=settings;
    resources.dlssFgGuard=settings_.dlssFgGuard;
    if (resources==settings_) {
        settings_=settings;
        if (impl_) { impl_->options.dlssFgGuard=settings.dlssFgGuard; impl_->frameGeneration.SetDlssGuard(settings.dlssFgGuard); }
        return;
    }
    settings_ = settings;
    ResetDevice();
}
void NeuralPass::ResetDevice() {
    impl_.reset();
    failed_ = false;
    status_ = Enabled() ? L"Waiting for SDR frame" : L"Off";
}
void NeuralPass::ClearHistory() {
    if (impl_)
        impl_->Invalidate();
}
ID3D11Texture2D* NeuralPass::Process(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* frame,
                                     const RECT& rect, long long timestamp, bool hdr, SIZE targetSize, SIZE monitorSize,
                                     double refreshHz, float frameTimeMs, bool diagnostics) {
    if (!Enabled())
        return nullptr;
    const upscaling::Extent requestedTarget{static_cast<unsigned>((std::max)(1L, targetSize.cx)),
                                            static_cast<unsigned>((std::max)(1L, targetSize.cy))};
    const upscaling::Extent monitor{static_cast<unsigned>((std::max)(1L, monitorSize.cx)),
                                    static_cast<unsigned>((std::max)(1L, monitorSize.cy))};
    const upscaling::Extent sourceSize{static_cast<unsigned>(rect.right - rect.left),
                                       static_cast<unsigned>(rect.bottom - rect.top)};
    const auto target = (settings_.upscaler != processing::Upscaler::Off) &&
                                upscaling::ShouldUpscale(sourceSize, requestedTarget, monitor)
                            ? requestedTarget
                            : sourceSize;
    if (!settings_.enabled && !settings_.ManualGeneration() && !settings_.FsrNative() &&
        (settings_.upscaler == processing::Upscaler::Off || !upscaling::ShouldUpscale(sourceSize, target, monitor))) {
        if (impl_)
            ResetDevice();
        status_ =
            std::wstring(L"NR: Off\n") + processing::Name(settings_.upscaler) + L" upscaling: native size / downscale";
        return nullptr;
    }
    frameTimeMs = std::isfinite(frameTimeMs) ? std::clamp(frameTimeMs, 1.0f, 500.0f) : 16.6667f;
    if (!std::isfinite(refreshHz) || refreshHz <= 0)
        refreshHz = 60;
    D3D11_TEXTURE2D_DESC desc{};
    frame->GetDesc(&desc);
    // PrepareShader samples normalized RGB and converts it into the 8-bit SDR model input.
    // Source bit depth is independent of transfer function: 10-bit SDR is valid here.
    if (!IsSdrInputFormat(desc.Format)) {
        Bypass(L"Bypass: unsupported RGB internal format");
        return nullptr;
    }
    if (rect.left < 0 || rect.top < 0 || rect.right > static_cast<LONG>(desc.Width) ||
        rect.bottom > static_cast<LONG>(desc.Height) || rect.right <= rect.left || rect.bottom <= rect.top ||
        desc.SampleDesc.Count != 1) {
        Bypass(L"Bypass: video rectangle outside frame");
        return nullptr;
    }
    try {
        if (impl_) {
            D3D11_TEXTURE2D_DESC old{};
            impl_->source->GetDesc(&old);
            if (impl_->device11.Get() != device || !EqualRect(&impl_->rect, &rect) || old.Width != desc.Width ||
                old.Height != desc.Height || impl_->sourceFormat != desc.Format || impl_->target != target || impl_->hdr != hdr)
                impl_.reset();
        }
        if (!impl_) {
            LARGE_INTEGER start{}, end{}, frequency{};
            QueryPerformanceCounter(&start);
            impl_ = std::make_unique<Impl>();
            impl_->Initialize(device, context, frame, rect, settings_, target, monitor, refreshHz, hdr);
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&frequency);
            impl_->initMilliseconds = 1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) / frequency.QuadPart;
        }
        impl_->refreshHz = refreshHz;
        if (impl_->diagnosticsEnabled != diagnostics) impl_->contentProbe.Invalidate();
        impl_->diagnosticsEnabled=diagnostics;
        auto result = impl_->Run(frame, timestamp, frameTimeMs);
        status_ = impl_->Status();
        return result;
    } catch (const std::exception& error) {
        impl_.reset();
        failed_ = true;
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
NeuralPass::NeuralPass() {
    status_ = L"Unsupported: use the x64 renderer";
}
NeuralPass::~NeuralPass() = default;
void NeuralPass::Configure(Settings settings) {
    settings_ = settings;
    failed_ = false;
}
void NeuralPass::ResetDevice() {
    failed_ = false;
}
void NeuralPass::ClearHistory() {}
ID3D11Texture2D* NeuralPass::Process(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, const RECT&, long long,
                                     bool, SIZE, SIZE, double, float, bool) {
    return nullptr;
}
unsigned NeuralPass::GeneratedCount() const {
    return 0;
}
unsigned long long NeuralPass::OutputSerial() const {
    return 0;
}
long long NeuralPass::OutputDuration() const {
    return 0;
}
long long NeuralPass::OutputTimestamp() const { return 0; }
long long NeuralPass::PresentationTimestamp(unsigned long long,unsigned) const { return 0; }
ID3D11Texture2D* NeuralPass::PresentationFrame(unsigned long long, unsigned) const {
    return nullptr;
}
}
#endif
