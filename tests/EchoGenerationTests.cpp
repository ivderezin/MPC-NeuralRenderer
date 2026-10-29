// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/FrameGeneration/EchoFrameGeneration.h"
#include "../Source/FrameGeneration/EchoGuard.h"
#include "../Source/Processing/ResourceBarrier.h"
#include "../Source/Processing/RuntimeLibrary.h"
#include <dxgi1_4.h>
#include <vector>
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>
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
    void Finish() {
        CheckHr(list->Close(),"close");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
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
int main() {
    try {
        Gpu12 gpu;
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
        guard.Dispatch(gpu.list.Get(),hi.Get(),motion.Get(),outputs,3,false);gpu.Finish();
        for(unsigned i=0;i<3;++i) {
            const auto pixels=gpu.Read(outputs[i]);const float expected=51.f+13.f*static_cast<float>(i+1)/4.f;
            Require(std::abs(pixels[(32*w+64)*4]-expected)<=1,"Echo post-FG guard must restore stationary foreground at each MFG temporal fraction");
        }
        std::cout<<"Native EchoFG, exact temporal fractions, reset and post-FG guard passed (WARP); mean error "<<totalError/3<<"/255\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
