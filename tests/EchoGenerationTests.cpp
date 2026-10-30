// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/FrameGeneration/EchoFrameGeneration.h"
#include "../Source/FrameGeneration/EchoGuard.h"
#include "../Source/FrameGeneration/DlssSampleCapture.h"
#include <wincodec.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <iterator>
#include "../Source/Processing/ResourceBarrier.h"
#include "../Source/Processing/RuntimeLibrary.h"
#include <dxgi1_4.h>
#include <vector>
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>
#include <functional>
using Microsoft::WRL::ComPtr;
using processing::CheckHr;
using processing::Transition;
void Require(bool v,const char* text) {if(!v)throw std::runtime_error(text);}
struct Gpu12 {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    UINT64 value=0;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    Gpu12() {
        ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;
        CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
        CheckHr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP");
        CheckHr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
        D3D12_COMMAND_QUEUE_DESC q{};CheckHr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        CheckHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)),"allocator");
        CheckHr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        CheckHr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    }
    ~Gpu12() {if(event)CloseHandle(event);}
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
        d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r;CheckHr(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"buffer");return r;
    }
    void Finish(const std::function<void()>& afterSubmit={}) {
        CheckHr(list->Close(),"close");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        if(afterSubmit)afterSubmit();
        CheckHr(queue->Signal(fence.Get(),++value),"signal");CheckHr(fence->SetEventOnCompletion(value,event),"event");
        Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"test GPU timeout");uploads.clear();
        CheckHr(alloc->Reset(),"reset allocator");CheckHr(list->Reset(alloc.Get(),nullptr),"reset list");
    }
    ComPtr<ID3D12Resource> Texture(unsigned w,unsigned h,DXGI_FORMAT format,D3D12_RESOURCE_STATES state,
                                  const void* data=nullptr,unsigned bytesPerPixel=4) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.Format=format;
        d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1;d.Flags=format==DXGI_FORMAT_B8G8R8A8_UNORM ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> r;
        CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,data ? D3D12_RESOURCE_STATE_COPY_DEST : state,nullptr,IID_PPV_ARGS(&r)),"texture");
        if (data) {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
            auto upload=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            void* mapped=nullptr;D3D12_RANGE none{0,0};CheckHr(upload->Map(0,&none,&mapped),"map upload");
            for(unsigned y=0;y<h;++y)std::memcpy(static_cast<char*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,
                static_cast<const char*>(data)+static_cast<size_t>(y)*w*bytesPerPixel,static_cast<size_t>(w)*bytesPerPixel);
            upload->Unmap(0,nullptr);
            D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=r.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            b.pResource=upload.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=footprint;
            list->CopyTextureRegion(&a,0,0,0,&b,nullptr);Transition(list.Get(),r.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);uploads.push_back(upload);
        }
        return r;
    }
    std::vector<unsigned char> Read(ID3D12Resource* r,unsigned bytesPerPixel=4) {
        const auto d=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        auto read=Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(list.Get(),r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=read.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=footprint;
        b.pResource=r;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&a,0,0,0,&b,nullptr);
        Transition(list.Get(),r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Finish();
        void* mapped=nullptr;CheckHr(read->Map(0,nullptr,&mapped),"read pixels");std::vector<unsigned char> result(static_cast<size_t>(d.Width)*d.Height*bytesPerPixel);
        for(UINT y=0;y<d.Height;++y)std::memcpy(result.data()+static_cast<size_t>(y)*d.Width*bytesPerPixel,
            static_cast<char*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,static_cast<size_t>(d.Width)*bytesPerPixel);
        D3D12_RANGE none{0,0};read->Unmap(0,&none);return result;
    }
};
#include "AmdNrIntegrationTests.h"
std::vector<unsigned char> ReadPng(const std::filesystem::path& path,UINT w,UINT h) {
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    CheckHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"test WIC factory");
    CheckHr(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"decode capture PNG");
    CheckHr(decoder->GetFrame(0,&frame),"capture PNG frame");UINT width=0,height=0;CheckHr(frame->GetSize(&width,&height),"capture PNG size");
    Require(width==w && height==h,"Capture dimensions must remain unchanged");
    CheckHr(factory->CreateFormatConverter(&converter),"capture test converter");
    CheckHr(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"convert captured PNG");
    std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);
    CheckHr(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data()),"read captured pixels");return pixels;
}
void TestSampleCapture(Gpu12& gpu) {
    CheckHr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"capture test COM");
    struct ComScope {~ComScope(){CoUninitialize();}} com;
    constexpr unsigned w=8,h=8;
    std::vector<unsigned char> a(w*h*4),b(a.size());
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const auto i=(y*w+x)*4;a[i]=static_cast<unsigned char>(20+x);a[i+1]=static_cast<unsigned char>(70+y);a[i+2]=180;a[i+3]=255;
        b[i]=160;b[i+1]=static_cast<unsigned char>(30+x);b[i+2]=static_cast<unsigned char>(50+y);b[i+3]=255;
    }
    auto first=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,a.data());
    auto second=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,b.data());
    auto made=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,b.data());
    std::vector<unsigned> vectorBits(w*h,0x00003c00u);std::vector<float> depthData(w*h,.5f);
    auto motion=gpu.Texture(w,h,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,vectorBits.data());
    auto depth=gpu.Texture(w,h,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,depthData.data());
    framegen::DlssSampleCapture capture;
    Require(!capture.Before(gpu.list.Get(),first.Get(),depth.Get(),motion.Get(),6,false,L"WARP fixture"),"Unrequested capture must be inert");
    Require(capture.Directory().empty(),"Unrequested capture must not create an output directory");
    framegen::DlssSampleCapture::Request(false);
    Require(!capture.Before(gpu.list.Get(),first.Get(),depth.Get(),motion.Get(),7,false,L"WARP fixture"),"Capture must establish a previous SDK input first");gpu.Finish();
    Require(!capture.Before(gpu.list.Get(),second.Get(),depth.Get(),motion.Get(),9,false,L"WARP fixture"),"A frame ID gap must replace the capture baseline");gpu.Finish();
    Require(capture.Before(gpu.list.Get(),first.Get(),depth.Get(),motion.Get(),10,false,L"WARP fixture"),"Consecutive pair must be captured");
    capture.Context("synthetic capture fixture, not a NVIDIA evaluation");
    std::array<ID3D12Resource*,3> outputs{made.Get(),nullptr,nullptr};capture.After(gpu.list.Get(),outputs,1);gpu.Finish();capture.Collect({0,0,0});
    for(unsigned i=0;i<1000 && !capture.Completed();++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(capture.Completed(),"Capture writer must complete");
    const auto directory=capture.Directory();
    Require(std::filesystem::exists(directory/"manifest.txt"),"Completed capture must include metadata");
    Require(ReadPng(directory/"previous.png",w,h)==b,"Previous SDK input must survive lossless capture");
    Require(ReadPng(directory/"current.png",w,h)==a,"Current SDK input RGB order must survive capture");
    Require(ReadPng(directory/"generated_1.png",w,h)==b,"Captured SDK output must be byte-exact in RGB");
    std::ifstream raw(directory/"motion.raw",std::ios::binary);std::vector<char> rawBytes((std::istreambuf_iterator<char>(raw)),{});raw.close();
    Require(rawBytes.size()==vectorBits.size()*4 && std::memcmp(rawBytes.data(),vectorBits.data(),rawBytes.size())==0,"Half-float motion must be saved exactly without row padding");
    std::ifstream manifestFile(directory/"manifest.txt");std::string manifest((std::istreambuf_iterator<char>(manifestFile)),{});manifestFile.close();
    Require(manifest.find("previous_frame_id=9")!=std::string::npos && manifest.find("current_frame_id=10")!=std::string::npos,"Capture manifest must identify the actual consecutive pair");
    Require(gpu.Read(made.Get())==b,"Diagnostic readback must not change generated pixels");
    std::filesystem::remove_all(directory);
    std::cout<<"One-shot DLSS sample capture, exact PNG/raw data and frame-ID gap passed (WARP)\n";
}
void TestDlssMotionImpulses(Gpu12& gpu) {
    constexpr unsigned w=129,h=65,y=32;
    std::vector<unsigned char> a(w*h*4,255),b(a.size(),255);
    std::vector<float> vectors(w*h*2,0);
    auto paint=[&](std::vector<unsigned char>& pixels,unsigned x,unsigned row,unsigned value) {
        for(unsigned c=0;c<3;++c)pixels[(row*w+x)*4+c]=static_cast<unsigned char>(value);
    };
    for(unsigned row=0;row<h;++row)for(unsigned x=0;x<w;++x) {
        paint(a,x,row,50+x/2);paint(b,x,row,50+(x+8)/2);
        if(x==80)paint(a,x,row,0);
        if(x==72)paint(b,x,row,0);
        if(x>=92 && x<=112) {paint(a,x,row,20);paint(b,x,row,180);}
        vectors[(row*w+x)*2]=x<48 ? 8.f+4.f*static_cast<float>((x/2)%2) : 8.f;
    }
    auto before=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,a.data());
    auto now=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,b.data());
    auto motion=gpu.Texture(w,h,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,vectors.data(),8);
    std::array<std::vector<unsigned char>,3> originals;
    std::array<ComPtr<ID3D12Resource>,3> made,legacyMade;
    std::array<ID3D12Resource*,3> outputs{},legacyOutputs{};
    for(unsigned i=0;i<3;++i) {
        auto& raw=originals[i];raw.resize(w*h*4,137);
        const unsigned shift=2*(i+1);
        for(unsigned row=0;row<h;++row)for(unsigned x=0;x<w;++x) {
            paint(raw,x,row,50+(x+shift)/2);
            if(x==80-shift)paint(raw,x,row,0); // A real moving one-pixel line.
        }
        for(unsigned row=y;row<y+2;++row)for(unsigned x=64;x<66;++x)paint(raw,x,row,0);
        paint(raw,32,y,0);paint(raw,100,y,0);paint(raw,w-1,y,0);
        made[i]=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,raw.data());outputs[i]=made[i].Get();
        legacyMade[i]=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,raw.data());legacyOutputs[i]=legacyMade[i].Get();
    }
    framegen::EchoGuard guard,legacy;
    guard.Create(gpu.device.Get(),w,h,false,true);legacy.Create(gpu.device.Get(),w,h,false);
    guard.Dispatch(gpu.list.Get(),before.Get(),motion.Get(),outputs,0,true);
    legacy.Dispatch(gpu.list.Get(),before.Get(),motion.Get(),legacyOutputs,0,true);gpu.Finish();
    Require(gpu.Read(outputs[0])==originals[0],"DLSS guard bypass must leave all SDK pixels intact");
    guard.Dispatch(gpu.list.Get(),now.Get(),motion.Get(),outputs,3,false);
    legacy.Dispatch(gpu.list.Get(),now.Get(),motion.Get(),legacyOutputs,3,false);gpu.Finish();
    for(unsigned i=0;i<3;++i) {
        const auto pixels=gpu.Read(outputs[i]);const unsigned shift=2*(i+1);
        for(unsigned row=y;row<y+2;++row)for(unsigned x=64;x<66;++x) {
            Require(std::abs(static_cast<int>(pixels[(row*w+x)*4])-static_cast<int>(50+(x+shift)/2))<=1,
                    "Moving 2x2 interpolation impulses must be repaired at every MFG fraction");
            Require(pixels[(row*w+x)*4+3]==137,"Motion impulse repair must preserve SDK alpha");
        }
        Require(pixels[(y*w+80-shift)*4]==0,"A real moving thin line must survive the impulse guard");
        Require(pixels[(y*w+100)*4]==0,"Conflicting real endpoints must not be used to invent a repair");
        Require(pixels[(y*w+32)*4]==0,"Discontinuous motion must reject impulse repair");
        Require(pixels[(y*w+w-1)*4]==0,"Image boundaries must not be sampled outside the frame");
        Require(gpu.Read(legacyOutputs[i])==originals[i],"Non-DLSS guards must retain their existing output");
    }
    auto pack=[](double nits,unsigned alpha) {
        auto pq=[](double n) {
            const double v=std::pow(n/10000.0,0.1593017578125);
            return static_cast<unsigned>(std::lround(std::pow((0.8359375+18.8515625*v)/(1.0+18.6875*v),78.84375)*1023.0));
        };
        return pq(nits)|(pq(nits*.7)<<10)|(pq(nits*.4)<<20)|(alpha<<30);
    };
    framegen::EchoGuard hdrGuard;hdrGuard.Create(gpu.device.Get(),w,h,true,true);
    for(double nits:{20.0,203.0,1000.0,4000.0}) {
        const auto real=pack(nits,3),generated=pack(nits,1),highlight=pack(10000,1);
        std::vector<unsigned> hdrA(w*h,real),hdrB=hdrA;
        for(unsigned row=0;row<h;++row) {
            hdrA[row*w+80]=pack(10000,3);hdrB[row*w+72]=pack(10000,3);
            for(unsigned x=92;x<=112;++x)hdrB[row*w+x]=pack(nits*2,3);
        }
        auto first=gpu.Texture(w,h,DXGI_FORMAT_R10G10B10A2_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,hdrA.data());
        auto second=gpu.Texture(w,h,DXGI_FORMAT_R10G10B10A2_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,hdrB.data());
        std::array<ComPtr<ID3D12Resource>,3> hdrMade;
        std::array<ID3D12Resource*,3> hdrOutputs{};
        std::array<std::vector<unsigned>,3> raw;
        for(unsigned i=0;i<3;++i) {
            raw[i].assign(w*h,generated);
            for(unsigned row=0;row<h;++row)raw[i][row*w+80-2*(i+1)]=highlight;
            for(unsigned row=y;row<y+2;++row)for(unsigned x=64;x<66;++x)raw[i][row*w+x]=1u<<30;
            raw[i][y*w+100]=1u<<30;
            hdrMade[i]=gpu.Texture(w,h,DXGI_FORMAT_R10G10B10A2_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,raw[i].data());
            hdrOutputs[i]=hdrMade[i].Get();
        }
        hdrGuard.Dispatch(gpu.list.Get(),first.Get(),motion.Get(),hdrOutputs,0,true);gpu.Finish();
        hdrGuard.Dispatch(gpu.list.Get(),second.Get(),motion.Get(),hdrOutputs,3,false);gpu.Finish();
        for(unsigned i=0;i<3;++i) {
            const auto pixels=gpu.Read(hdrMade[i].Get());
            for(unsigned row=0;row<h;++row)for(unsigned x=0;x<w;++x) {
                unsigned actual=0;std::memcpy(&actual,pixels.data()+(row*w+x)*4,4);
                if(row>=y && row<y+2 && x>=64 && x<66) {
                    for(unsigned channel=0;channel<3;++channel)
                        Require(std::abs(static_cast<int>((actual>>(10*channel))&1023)-static_cast<int>((generated>>(10*channel))&1023))<=2,
                                "HDR impulse must reconstruct its native PQ/BT.2020 color without clipping highlights");
                    Require((actual>>30)==1,"HDR repair must preserve the original SDK alpha");
                } else Require(actual==raw[i][row*w+x],"HDR detail, endpoint disagreement and untouched pixels must remain bit-exact");
            }
        }
    }
    std::cout<<"DLSS SDR/HDR10 impulse repair, MFG fractions, 20-4000 nit colors, real highlights, rejection and alpha passed (WARP)\n";
}
int main() {
    try {
        Gpu12 gpu;
        TestSampleCapture(gpu);
        TestAmdNr(gpu);
        TestDlssMotionImpulses(gpu);
        constexpr unsigned w=128,h=64;
        std::vector<unsigned char> a(w*h*4),b(a.size());
        auto pattern=[](unsigned x,unsigned y) {return static_cast<unsigned char>(32+(((x/8)*73856093u)^((y/8)*19349663u))%192);};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const auto at=(y*w+x)*4;
            a[at]=a[at+1]=a[at+2]=pattern(x,y);a[at+3]=255;
            b[at]=b[at+1]=b[at+2]=pattern((std::min)(x+8,w-1),y);b[at+3]=255;
        }
        auto previous=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,a.data());
        auto current=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,b.data());
        std::array<ComPtr<ID3D12Resource>,3> generated;
        std::array<ID3D12Resource*,3> outputs{};
        for(unsigned i=0;i<3;++i) {generated[i]=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);outputs[i]=generated[i].Get();}
        framegen::EchoFrameGeneration echo;
        echo.Create(gpu.device.Get(),w,h,false);
        echo.Dispatch(gpu.list.Get(),previous.Get(),outputs,3,true);gpu.Finish();
        Require(echo.Collect()==0,"EchoFG first/reset frame must not publish uninitialized interpolation");
        echo.Dispatch(gpu.list.Get(),current.Get(),outputs,3,false);gpu.Finish();
        Require(echo.Collect()==3,"Native EchoFG must produce all three temporal fractions");
        framegen::EchoFrameGeneration guidance;
        guidance.Create(gpu.device.Get(),w,h,false,true);
        guidance.Dispatch(gpu.list.Get(),previous.Get(),{},0,true);gpu.Finish();
        guidance.Dispatch(gpu.list.Get(),current.Get(),{},0,false);gpu.Finish();
        Require(guidance.Collect()==0,"Motion-only guidance must not synthesize or publish frames");
        const auto vectors=gpu.Read(guidance.Motion());
        auto half=[](unsigned lo,unsigned hi) {
            const unsigned bits=lo|(hi<<8),exp=(bits>>10)&31u,mantissa=bits&1023u;
            const float value=exp ? std::ldexp(1.f+static_cast<float>(mantissa)/1024.f,static_cast<int>(exp)-15)
                : std::ldexp(static_cast<float>(mantissa),-24);
            return bits&32768u ? -value : value;
        };
        double motionError=0;unsigned motionSamples=0;
        for(unsigned y=16;y<48;++y)for(unsigned x=32;x<88;++x) {
            const auto at=(y*w+x)*4;
            motionError+=std::abs(half(vectors[at],vectors[at+1])-8.f)+std::abs(half(vectors[at+2],vectors[at+3]));
            ++motionSamples;
        }
        Require(motionError/motionSamples<1,"Vendor FG guidance must use final-frame backward motion in final-resolution pixels");
        auto exported=gpu.Texture(w,h,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto postMotion=gpu.Texture(w*2,h*2,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto confidence=gpu.Texture(w,h,DXGI_FORMAT_R8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        guidance.WriteGuidance(gpu.list.Get(),exported.Get(),confidence.Get(),2.f);
        guidance.WriteGuidance(gpu.list.Get(),postMotion.Get());gpu.Finish();
        for(auto* resource:{exported.Get(),postMotion.Get(),confidence.Get()})
            Transition(gpu.list.Get(),resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto exportedPixels=gpu.Read(exported.Get());
        const auto postPixels=gpu.Read(postMotion.Get());
        const auto fastMask=gpu.Read(confidence.Get(),1);
        const unsigned center=(h/2*w+w/2)*4,postCenter=(h*(w*2)+w)*4;
        Require(std::abs(half(exportedPixels[center],exportedPixels[center+1])-8.f)<1,"Shared native motion must retain pixel units");
        Require(std::abs(half(postPixels[postCenter],postPixels[postCenter+1])-16.f)<2,"After-upscale NR motion must scale to output pixels");
        Require(fastMask[h/2*w+w/2]==255,"Native SR mask must retain fast-motion rejection");
        for(auto* resource:{exported.Get(),postMotion.Get(),confidence.Get()})
            Transition(gpu.list.Get(),resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        guidance.WriteGuidance(gpu.list.Get(),exported.Get(),confidence.Get(),0.f);gpu.Finish();
        Transition(gpu.list.Get(),confidence.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto matchMask=gpu.Read(confidence.Get(),1);
        Require(matchMask[h/2*w+w/2]<64,"Matching confidence must remain independent of SR fast-motion rejection");
        double totalError=0;
        for(unsigned i=0;i<3;++i) {
            const auto pixels=gpu.Read(outputs[i]);
            double error=0;unsigned n=0;
            for(unsigned y=16;y<48;++y)for(unsigned x=32;x<88;++x) {
                error+=std::abs(static_cast<int>(pixels[(y*w+x)*4])-static_cast<int>(pattern(x+2*(i+1),y)));++n;
            }
            error/=n;totalError+=error;
            Require(error<24,"EchoFG must reconstruct the known camera translation at the requested temporal fraction");
        }
        framegen::EchoFrameGeneration bgraGuide;
        bgraGuide.Create(gpu.device.Get(),w,h,false,true,DXGI_FORMAT_B8G8R8A8_UNORM);
        auto bgraBefore=gpu.Texture(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,a.data());
        auto bgraNow=gpu.Texture(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,b.data());
        bgraGuide.Dispatch(gpu.list.Get(),bgraBefore.Get(),{},0,true);gpu.Finish();
        bgraGuide.Dispatch(gpu.list.Get(),bgraNow.Get(),{},0,false);gpu.Finish();
        const auto bgraMotion=gpu.Read(bgraGuide.Motion());
        Require(std::abs(half(bgraMotion[center],bgraMotion[center+1])-8.f)<1,"XeSS BGRA input must use native backward motion without format conversion errors");
        echo.Dispatch(gpu.list.Get(),current.Get(),outputs,3,true);gpu.Finish();Require(echo.Collect()==0,"EchoFG seek must reset both motion and synthesis history");
        std::vector<unsigned char> low(w*h*4,51),high(w*h*4,64),bad(w*h*4,255);
        for(unsigned i=3;i<low.size();i+=4) {low[i]=high[i]=bad[i]=255;}
        auto lo=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,low.data());
        auto hi=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,high.data());
        std::vector<float> zero(w*h*2,0);
        auto motion=gpu.Texture(w,h,DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,zero.data(),8);
        framegen::EchoGuard guard;guard.Create(gpu.device.Get(),w,h,false);
        guard.Dispatch(gpu.list.Get(),lo.Get(),motion.Get(),outputs,0,true);gpu.Finish();
        for(unsigned i=0;i<3;++i)generated[i]=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,bad.data());
        for(unsigned i=0;i<3;++i)outputs[i]=generated[i].Get();
        // Bypass must preserve the SDK pixels while advancing the guard history.
        guard.Dispatch(gpu.list.Get(),lo.Get(),motion.Get(),outputs,0,false);gpu.Finish();
        const auto bypass=gpu.Read(outputs[0]);
        Require(bypass[(32*w+64)*4]==255,"Disabling the output guard must preserve SDK pixels");
        guard.Dispatch(gpu.list.Get(),hi.Get(),motion.Get(),outputs,3,false);gpu.Finish();
        for(unsigned i=0;i<3;++i) {
            const auto pixels=gpu.Read(outputs[i]);const float expected=51.f+13.f*static_cast<float>(i+1)/4.f;
            Require(std::abs(pixels[(32*w+64)*4]-expected)<=1,"Echo post-FG guard must restore stationary foreground at each MFG temporal fraction");
        }
        std::cout<<"Native EchoFG, exact temporal fractions, reset and post-FG guard passed (WARP); mean error "<<totalError/3<<"/255\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
