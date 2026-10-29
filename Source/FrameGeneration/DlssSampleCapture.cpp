// SPDX-License-Identifier: GPL-3.0-or-later
#include "DlssSampleCapture.h"
#include "../Processing/ResourceBarrier.h"
#include "../Processing/RuntimeLibrary.h"
#include <wrl/client.h>
#include <wincodec.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")
namespace framegen {
namespace {
using Microsoft::WRL::ComPtr;
using processing::CheckHr;
using processing::Transition;
std::atomic<bool> requested{false},openRequested{true};
std::atomic<unsigned long long> sequence{0};
enum class State { Armed,Recording,Recorded,Writing,Saved,Failed };
std::string Utf8(std::wstring_view text) {
    if(text.empty())return {};
    const auto size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
unsigned PixelBytes(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R32_FLOAT:return 4;
    default:throw std::runtime_error("Unsupported DLSS capture format");
    }
}
float Half(unsigned bits) {
    const auto exp=(bits>>10)&31u,mantissa=bits&1023u;
    if(exp==31)return mantissa ? NAN : (bits&32768u ? -INFINITY : INFINITY);
    const float v=exp ? std::ldexp(1.f+static_cast<float>(mantissa)/1024.f,static_cast<int>(exp)-15)
        : std::ldexp(static_cast<float>(mantissa),-24);
    return bits&32768u ? -v : v;
}
void WriteFile(const std::filesystem::path& path,const void* data,size_t bytes) {
    std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);
    file.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes));
}
void Png(IWICImagingFactory* factory,const std::filesystem::path& path,UINT width,UINT height,const std::vector<unsigned char>& bgr) {
    ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    CheckHr(factory->CreateStream(&stream),"Create capture stream");
    CheckHr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open capture PNG");
    CheckHr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create capture PNG encoder");
    CheckHr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize capture PNG");
    CheckHr(encoder->CreateNewFrame(&frame,nullptr),"Create capture PNG frame");
    CheckHr(frame->Initialize(nullptr),"Initialize capture PNG frame");
    CheckHr(frame->SetSize(width,height),"Size capture PNG");
    auto format=GUID_WICPixelFormat24bppBGR;
    CheckHr(frame->SetPixelFormat(&format),"Set capture PNG format");
    if(format!=GUID_WICPixelFormat24bppBGR)throw std::runtime_error("Unexpected capture PNG format");
    CheckHr(frame->WritePixels(height,width*3,static_cast<UINT>(bgr.size()),const_cast<BYTE*>(bgr.data())),"Write capture PNG");
    CheckHr(frame->Commit(),"Commit capture PNG frame");CheckHr(encoder->Commit(),"Commit capture PNG");
}
}
struct DlssSampleCapture::Impl {
    struct Plane {
        std::string name;UINT width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        ComPtr<ID3D12Resource> readback;std::vector<unsigned char> data;
    };
    std::atomic<State> state{State::Armed};
    ComPtr<ID3D12Resource> previous;
    unsigned long long previousId=0,currentId=0;
    std::vector<Plane> planes;
    unsigned generated=0;bool openFolder=false;
    std::array<unsigned,3> disabled{};
    std::filesystem::path directory;
    std::wstring runtime;
    std::string context;
    mutable std::mutex mutex;std::string error;
    std::thread writer;
    ~Impl(){if(writer.joinable())writer.join();}
    void Fail(const char* message) noexcept {
        try {std::lock_guard lock(mutex);error=message;}catch(...){}
        state.store(State::Failed,std::memory_order_release);
    }
    void Read(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES incoming,const char* name) {
        const auto desc=resource->GetDesc();PixelBytes(desc.Format);
        ComPtr<ID3D12Device> device;CheckHr(resource->GetDevice(IID_PPV_ARGS(&device)),"Get capture device");
        Plane plane;plane.name=name;plane.width=static_cast<UINT>(desc.Width);plane.height=desc.Height;plane.format=desc.Format;
        device->GetCopyableFootprints(&desc,0,1,0,&plane.footprint,nullptr,nullptr,&plane.bytes);
        UINT64 allocated=plane.bytes;
        for(const auto& existing:planes)allocated+=existing.bytes;
        if(allocated>256ull*1024*1024)throw std::runtime_error("DLSS sample exceeds the 256 MiB readback budget");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=plane.bytes;
        buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&plane.readback)),"Create capture readback");
        // Keep resources alive even if a subsequent capture allocation fails.
        planes.push_back(std::move(plane));auto& stored=planes.back();
        Transition(list,resource,incoming,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=stored.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=stored.footprint;
        src.pResource=resource;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Transition(list,resource,D3D12_RESOURCE_STATE_COPY_SOURCE,incoming);
    }
    void Baseline(ID3D12GraphicsCommandList* list,ID3D12Resource* color,unsigned long long frame) {
        if(!previous) {
            ComPtr<ID3D12Device> device;CheckHr(color->GetDevice(IID_PPV_ARGS(&device)),"Get baseline device");
            auto desc=color->GetDesc();desc.Flags=D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&previous)),"Create capture baseline");
        }
        Transition(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(previous.Get(),color);Transition(list,color,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        previousId=frame;
    }
    void Save() {
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        struct ComScope {bool uninit;~ComScope(){if(uninit)CoUninitialize();}} scope{SUCCEEDED(com)};
        CheckHr(com,"Initialize capture image writer");
        ComPtr<IWICImagingFactory> factory;CheckHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create capture image factory");
        std::filesystem::create_directories(directory);
        std::ostringstream manifest;
        manifest<<"DLSS FG diagnostic sample v1\nprevious_frame_id="<<previousId<<"\ncurrent_frame_id="<<currentId<<"\ngenerated_count="<<generated<<"\n"<<context<<"\n"<<Utf8(runtime)<<"\n";
        manifest<<"motion_direction=current_to_previous; units=pixels before SDK normalization\nraw_layout=little_endian, tightly_packed, 4_bytes_per_pixel\nHDR previews are quantized PQ, not tone-mapped; use raw data for exact values.\n";
        for(unsigned i=0;i<generated;++i)manifest<<"generated_"<<i+1<<"_disable_flag="<<disabled[i]<<'\n';
        for(const auto& plane:planes) {
            manifest<<plane.name<<": width="<<plane.width<<", height="<<plane.height<<", dxgi_format="<<static_cast<unsigned>(plane.format)<<'\n';
            // Opaque SDR is exactly reconstructible from lossless RGB PNG plus
            // the format/alpha metadata. Keep raw data for every other plane.
            bool opaque=plane.format==DXGI_FORMAT_R8G8B8A8_UNORM || plane.format==DXGI_FORMAT_B8G8R8A8_UNORM;
            if(opaque)for(size_t at=3;at<plane.data.size();at+=4)if(plane.data[at]!=255){opaque=false;break;}
            manifest<<plane.name<<"_opaque_sdr="<<opaque<<'\n';
            if(!opaque)WriteFile(directory/(plane.name+".raw"),plane.data.data(),plane.data.size());
            std::vector<unsigned char> preview(static_cast<size_t>(plane.width)*plane.height*3);
            std::ostringstream csv;
            if(plane.format==DXGI_FORMAT_R16G16_FLOAT)csv<<"x,y,vx,vy\n";
            for(UINT y=0;y<plane.height;++y)for(UINT x=0;x<plane.width;++x) {
                const auto index=static_cast<size_t>(y)*plane.width+x;
                const auto* p=plane.data.data()+index*4;auto* out=preview.data()+index*3;
                if(plane.format==DXGI_FORMAT_R16G16_FLOAT) {
                    unsigned short v[2];std::memcpy(v,p,4);const float vx=Half(v[0]),vy=Half(v[1]);
                    auto encode=[](float f){return std::isfinite(f) ? static_cast<unsigned char>(std::clamp(128.f+f*2.f,0.f,255.f)) : static_cast<unsigned char>(0);};
                    out[0]=128;out[1]=encode(vy);out[2]=encode(vx);
                    if(x%(std::max)(1u,plane.width/64)==0 && y%(std::max)(1u,plane.height/36)==0)csv<<x<<','<<y<<','<<vx<<','<<vy<<'\n';
                } else if(plane.format==DXGI_FORMAT_R32_FLOAT) {
                    float d;std::memcpy(&d,p,4);const auto c=std::isfinite(d) ? static_cast<unsigned char>(std::clamp(d*255.f,0.f,255.f)) : static_cast<unsigned char>(0);out[0]=out[1]=out[2]=c;
                } else if(plane.format==DXGI_FORMAT_R10G10B10A2_UNORM) {
                    unsigned v;std::memcpy(&v,p,4);out[2]=static_cast<unsigned char>((v&1023u)*255/1023);out[1]=static_cast<unsigned char>(((v>>10)&1023u)*255/1023);out[0]=static_cast<unsigned char>(((v>>20)&1023u)*255/1023);
                } else {
                    const bool bgra=plane.format==DXGI_FORMAT_B8G8R8A8_UNORM;out[0]=p[bgra ? 0 : 2];out[1]=p[1];out[2]=p[bgra ? 2 : 0];
                }
            }
            Png(factory.Get(),directory/(plane.name+".png"),plane.width,plane.height,preview);
            if(plane.format==DXGI_FORMAT_R16G16_FLOAT) {const auto text=csv.str();WriteFile(directory/"motion.csv",text.data(),text.size());}
        }
        const auto text=manifest.str();WriteFile(directory/"manifest.txt",text.data(),text.size());
        planes.clear();state.store(State::Saved,std::memory_order_release);
        if(openFolder)ShellExecuteW(nullptr,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
};
DlssSampleCapture::DlssSampleCapture()=default;
DlssSampleCapture::~DlssSampleCapture()=default;
void DlssSampleCapture::Request(bool openFolder){openRequested.store(openFolder);requested.store(true);}
bool DlssSampleCapture::Before(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* depth,
    ID3D12Resource* motion,unsigned long long frame,bool reset,std::wstring_view runtime) noexcept {
    try {
        if(impl_) {const auto state=impl_->state.load();if(state==State::Writing || state==State::Recorded || state==State::Recording)return false;}
        if(requested.exchange(false)) {
            impl_=std::make_unique<Impl>();auto& p=*impl_;p.openFolder=openRequested.load();p.runtime=runtime;
            wchar_t temp[32768]{};const auto n=GetTempPathW(32768,temp);if(!n || n>=32768)throw std::runtime_error("Capture temporary directory unavailable");
            p.directory=std::filesystem::path(temp)/L"MPC-NeuralRenderer"/(L"DLSS-FG-sample-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence));
        }
        if(!impl_ || impl_->state.load()!=State::Armed)return false;
        auto& p=*impl_;
        if(!p.previous || reset || frame!=p.previousId+1) {p.Baseline(list,color,frame);return false;}
        p.state.store(State::Recording);p.currentId=frame;
        p.Read(list,p.previous.Get(),D3D12_RESOURCE_STATE_COPY_DEST,"previous");
        p.Read(list,color,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"current");
        p.Read(list,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"motion");
        p.Read(list,depth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"depth");return true;
    }catch(const std::exception& e){if(impl_)impl_->Fail(e.what());return false;}
}
void DlssSampleCapture::Context(std::string text) noexcept {try{if(impl_)impl_->context=std::move(text);}catch(const std::exception& e){if(impl_)impl_->Fail(e.what());}}
void DlssSampleCapture::After(ID3D12GraphicsCommandList* list,const std::array<ID3D12Resource*,3>& outputs,unsigned count) noexcept {
    if(!impl_ || impl_->state.load()!=State::Recording)return;
    try {auto& p=*impl_;p.generated=(std::min)(count,3u);
        for(unsigned i=0;i<p.generated;++i) {const auto name="generated_"+std::to_string(i+1);p.Read(list,outputs[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,name.c_str());}
        p.state.store(State::Recorded);
    }catch(const std::exception& e){impl_->Fail(e.what());}
}
void DlssSampleCapture::Collect(const std::array<unsigned,3>& disabled) noexcept {
    if(!impl_ || impl_->state.load()!=State::Recorded)return;
    try {auto& p=*impl_;p.disabled=disabled;
        for(auto& plane:p.planes) {
            plane.data.resize(static_cast<size_t>(plane.width)*plane.height*4);void* mapped=nullptr;
            const D3D12_RANGE range{0,static_cast<SIZE_T>(plane.bytes)};CheckHr(plane.readback->Map(0,&range,&mapped),"Map completed DLSS sample");
            for(UINT y=0;y<plane.height;++y)std::memcpy(plane.data.data()+static_cast<size_t>(y)*plane.width*4,
                static_cast<unsigned char*>(mapped)+plane.footprint.Offset+static_cast<size_t>(y)*plane.footprint.Footprint.RowPitch,static_cast<size_t>(plane.width)*4);
            const D3D12_RANGE empty{0,0};plane.readback->Unmap(0,&empty);plane.readback.Reset();
        }
        p.previous.Reset();p.state.store(State::Writing);
        p.writer=std::thread([ptr=&p]{try{ptr->Save();}catch(const std::exception& e){ptr->Fail(e.what());}});
    }catch(const std::exception& e){impl_->Fail(e.what());}
}
std::wstring DlssSampleCapture::Status() const {
    if(!impl_)return requested.load() ? L"\nDLSS sample: waiting for a frame pair" : L"";
    switch(impl_->state.load(std::memory_order_acquire)) {
    case State::Saved:return L"\nDLSS sample saved: "+impl_->directory.wstring();
    case State::Failed:{std::lock_guard lock(impl_->mutex);return L"\nDLSS sample failed: "+std::wstring(impl_->error.begin(),impl_->error.end());}
    case State::Writing:return L"\nDLSS sample: writing PNG/raw data in background";
    default:return L"\nDLSS sample: collecting one consecutive SDK frame pair";
    }
}
std::filesystem::path DlssSampleCapture::Directory() const {return impl_ ? impl_->directory : std::filesystem::path{};}
bool DlssSampleCapture::Completed() const {return impl_ && (impl_->state.load()==State::Saved || impl_->state.load()==State::Failed);}
}
