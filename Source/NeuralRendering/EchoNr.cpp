// SPDX-License-Identifier: GPL-3.0-or-later
#include "EchoNr.h"
#ifdef _WIN64
#include "../EchoStorm/engine/nr_engine.h"
#include "../EchoStorm/addon/bridge.h"
#include "../EchoStorm/addon/compose11.h"
#include "../EchoStorm/addon/auto_quality.h"
#include "../Processing/RuntimeLibrary.h"
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <thread>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <sstream>
namespace neural {
using Microsoft::WRL::ComPtr;
struct EchoNr::Impl {
    NrEngine engine;
    Bridge bridge;
    Compose11 compose;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> cropped;
    std::thread initializer;
    std::atomic<int> state{0}; // 0 initializing, 1 ready, 2 failed
    HANDLE initialized=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::mutex errorMutex;
    std::string error;
    bool bridgeReady=false,reset=true,hdr=false;
    NrParams params;
    nr::AutoQuality quality;
    unsigned long long index=0,lastQueued=0;
    long long lastTimestamp=0;
    bool haveTimestamp=false;
    unsigned width=0,height=0,workWidth=0,workHeight=0;
    double gpuMs=0;
    struct Stamp { unsigned long long index=0; long long timestamp=0,interval=166667; };
    std::array<Stamp,128> stamps{};
    void Log(const char* text) { std::lock_guard lock(errorMutex); error=text; }
    void Start(ID3D11Device* dev) {
        device=dev;
        ComPtr<IDXGIDevice> dxgi; processing::CheckHr(dev->QueryInterface(IID_PPV_ARGS(&dxgi)),"Query Echo NR device");
        ComPtr<IDXGIAdapter> adapter; processing::CheckHr(dxgi->GetAdapter(&adapter),"Get Echo NR adapter");
        DXGI_ADAPTER_DESC desc{}; processing::CheckHr(adapter->GetDesc(&desc),"Get Echo NR adapter identity");
        const auto directory=processing::ModuleDirectory();
        std::array<wchar_t,32768> temp{};
        const auto n=GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
        if (!n || n>=temp.size() || !initialized) throw std::runtime_error("Echo NR initialization resources unavailable");
        const auto cache=std::filesystem::path(temp.data())/L"MPC-NeuralRenderer";
        std::filesystem::create_directories(cache);
        initializer=std::thread([this,luid=desc.AdapterLuid,directory,cache] {
            try {
                const auto log=[this](const char* message){Log(message);};
                const bool ok=engine.Init(luid,(directory/L"nvngx.dll_mpc_echo_nr.dll").wstring(),
                    (directory/L"nvngx_dlssnr.dll").wstring(),cache.wstring(),directory.wstring(),log);
                state.store(ok ? 1 : 2,std::memory_order_release);
            } catch (const std::exception& e) { Log(e.what()); state.store(2,std::memory_order_release); }
            SetEvent(initialized);
        });
    }
    bool Shutdown() {
        if (initializer.joinable()) {
            if (WaitForSingleObject(initialized,5000)!=WAIT_OBJECT_0) return false;
            initializer.join();
        }
        bridge.Unblock();
        if (!engine.Shutdown()) return false;
        bridge.Shutdown(); compose.Shutdown();
        if (initialized) { CloseHandle(initialized); initialized=nullptr; }
        return true;
    }
};
EchoNr::EchoNr()=default;
EchoNr::~EchoNr() { Reset(); }
void EchoNr::Reset() {
    if (!impl_) return;
    if (!impl_->Shutdown()) {
        // Preserve objects/code a stuck third-party runtime can still reference.
        HMODULE pin=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&processing::ModuleDirectory),&pin);
        impl_.release();
        return;
    }
    impl_.reset();
}
void EchoNr::Invalidate() {
    if (impl_) { impl_->reset=true; impl_->haveTimestamp=false; }
}
void EchoNr::Submit(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* frame,
                    const RECT& rect,long long timestamp,bool hdr,const Settings& settings) {
    if (!settings.enabled || !settings.echoPipeline) return;
    try {
        if (impl_ && impl_->device.Get()!=device) Reset();
        if (!impl_) { impl_=std::make_unique<Impl>(); impl_->Start(device); }
        auto& p=*impl_;
        if (p.state.load(std::memory_order_acquire)!=1) return;
        if (!p.bridgeReady) {
            auto log=[&p](const char* message){p.Log(message);};
            if (!p.bridge.Init(device,context,&p.engine,log) || !p.compose.Init(device,log)) {
                p.state.store(2); return;
            }
            p.bridge.ShareMotion(true); p.bridgeReady=true;
        }
        if (p.haveTimestamp && timestamp==p.lastTimestamp) return;
        if (p.haveTimestamp && (timestamp<p.lastTimestamp || timestamp-p.lastTimestamp>5000000)) p.reset=true;
        const auto w=static_cast<unsigned>(rect.right-rect.left),h=static_cast<unsigned>(rect.bottom-rect.top);
        if (!w || !h) return;
        D3D11_TEXTURE2D_DESC desc{}; frame->GetDesc(&desc);
        if (rect.left || rect.top || desc.Width!=w || desc.Height!=h) {
            D3D11_TEXTURE2D_DESC current{}; if (p.cropped) p.cropped->GetDesc(&current);
            if (!p.cropped || current.Width!=w || current.Height!=h || current.Format!=desc.Format) {
                p.cropped.Reset(); desc.Width=w; desc.Height=h; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags=desc.CPUAccessFlags=0; desc.Usage=D3D11_USAGE_DEFAULT;
                processing::CheckHr(device->CreateTexture2D(&desc,nullptr,&p.cropped),"Create Echo NR crop");
            }
            const D3D11_BOX box{static_cast<UINT>(rect.left),static_cast<UINT>(rect.top),0,
                static_cast<UINT>(rect.right),static_cast<UINT>(rect.bottom),1};
            context->CopySubresourceRegion(p.cropped.Get(),0,0,0,0,frame,0,&box); frame=p.cropped.Get();
        }
        if (p.width!=w || p.height!=h || p.hdr!=hdr) { p.reset=true; p.width=w; p.height=h; p.hdr=hdr; }
        p.engine.SetFrameEncoding(hdr ? 2u : 0u,203.f);
        p.params.intensity=std::clamp(settings.intensity/100.f,0.f,1.f);
        p.params.composeIntensity=(std::max)(1.f,settings.intensity/100.f);
        // Upstream's working-size control: keep full 1080p, reduce 4K to 1080p.
        const auto ceiling=(std::min)(1.f,1920.f/static_cast<float>(w));
        if (!p.engine.Busy()) {
            const auto stats=p.engine.Stats(); p.gpuMs=stats.nrMs;
            const nr::AutoQuality::Settings budget{true,5.f,.25f,0.f};
            p.quality.Update(GetTickCount64(),static_cast<float>(p.gpuMs),static_cast<float>(p.bridge.IntervalMs()),ceiling,budget);
            p.bridge.SetRunEvery(p.quality.RunEvery());
        }
        p.params.workingScale=p.quality.Scale()>0 ? (std::min)(ceiling,p.quality.Scale()) : ceiling;
        if (!p.bridge.Ensure(w,h,desc.Format)) return;
        ++p.index;
        if (p.bridge.Submit(frame,nullptr,0,0,p.params,p.reset,p.index,false)) {
            long long interval=166667;
            if (p.lastQueued) {
                const auto& previous=p.stamps[p.lastQueued%p.stamps.size()];
                if (previous.index==p.lastQueued && timestamp>previous.timestamp) interval=timestamp-previous.timestamp;
            }
            p.stamps[p.index%p.stamps.size()]={p.index,timestamp,interval};
            p.lastQueued=p.index; p.reset=false;
        }
        p.lastTimestamp=timestamp; p.haveTimestamp=true;
    } catch (const std::exception& e) { if (impl_) {impl_->Log(e.what());impl_->state.store(2);} }
}
void EchoNr::Compose(ID3D11DeviceContext* context,ID3D11Texture2D* target,const RECT& viewport,
                     long long timestamp,bool generated) {
    if (!impl_ || !impl_->bridgeReady || impl_->state.load()!=1) return;
    auto& p=*impl_;
    ID3D11ShaderResourceView* delta=nullptr;
    const auto frame=p.bridge.NewestDelta(&delta,&p.workWidth,&p.workHeight);
    if (!frame || !delta) return;
    const auto& stamp=p.stamps[frame%p.stamps.size()];
    if (stamp.index!=frame || stamp.interval<=0) return;
    D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
    Compose11::Args args;
    args.target=target; args.delta=delta; args.motion=p.bridge.MotionOf(frame);
    args.offset=std::clamp(static_cast<float>(timestamp-stamp.timestamp)/static_cast<float>(stamp.interval),-4.f,8.f);
    args.intensity=p.params.composeIntensity; args.maxDelta=p.params.maxDelta;
    args.darkGuard=p.params.darkGuard; args.hiProtect=p.params.hiProtect; args.ghostGuard=p.params.ghostGuard;
    args.sharpen=p.params.sharpen; args.isGen=generated; args.encoding=p.hdr ? 2u : 0u; args.whiteNits=203.f;
    args.viewport[0]=static_cast<float>(viewport.left)/desc.Width;
    args.viewport[1]=static_cast<float>(viewport.top)/desc.Height;
    args.viewport[2]=static_cast<float>(viewport.right-viewport.left)/desc.Width;
    args.viewport[3]=static_cast<float>(viewport.bottom-viewport.top)/desc.Height;
    p.bridge.BeginDeltaUse(frame);
    context->OMSetRenderTargets(0,nullptr,nullptr);
    if (!p.compose.Run(context,args)) p.Log("Echo NR composition failed; base frame retained");
    p.bridge.EndDeltaUse(frame);
}
std::wstring EchoNr::Status() const {
    if (!impl_) return L"Echo NR: waiting for input";
    const auto& p=*impl_;
    const int state=p.state.load(std::memory_order_acquire);
    if (!state) return L"Echo NR: initializing asynchronously; base frames continue";
    if (state==2) { std::lock_guard lock(impl_->errorMutex); return L"Echo NR: "+std::wstring(p.error.begin(),p.error.end()); }
    std::wostringstream text;
    text << L"Echo NR: independent delta, " << p.workWidth << L"x" << p.workHeight << L", model " << p.gpuMs << L" ms";
    return text.str();
}
}
#else
namespace neural {
struct EchoNr::Impl {};
EchoNr::EchoNr()=default; EchoNr::~EchoNr()=default;
void EchoNr::Reset() {} void EchoNr::Invalidate() {}
void EchoNr::Submit(ID3D11Device*,ID3D11DeviceContext*,ID3D11Texture2D*,const RECT&,long long,bool,const Settings&) {}
void EchoNr::Compose(ID3D11DeviceContext*,ID3D11Texture2D*,const RECT&,long long,bool) {}
std::wstring EchoNr::Status() const { return L"Echo NR requires x64"; }
}
#endif
