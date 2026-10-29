// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../Source/FrameGeneration/DlssMultiFrameGeneration.h"
#include "../../Source/Processing/RuntimeLibrary.h"
#include "../../Source/Processing/ResourceBarrier.h"
#include <dxgi1_6.h>
#include <wincodec.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string_view>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"uuid.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"nvsdk_ngx_s.lib")
using Microsoft::WRL::ComPtr;
using processing::CheckHr;
using processing::Transition;
struct Image {unsigned w=0,h=0;std::vector<unsigned char> rgba;};
void Require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
Image LoadPng(IWICImagingFactory* factory,const std::filesystem::path& path) {
    ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    CheckHr(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Open input PNG");
    CheckHr(decoder->GetFrame(0,&frame),"Get input PNG frame");Image image;
    CheckHr(frame->GetSize(&image.w,&image.h),"Get input image size");
    Require(image.w>=2 && image.h>=2 && image.w<=8192 && image.h<=8192,"Unsupported replay image size");
    CheckHr(factory->CreateFormatConverter(&converter),"Create image converter");
    CheckHr(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert input PNG");
    image.rgba.resize(static_cast<size_t>(image.w)*image.h*4);
    CheckHr(converter->CopyPixels(nullptr,image.w*4,static_cast<UINT>(image.rgba.size()),image.rgba.data()),"Read input PNG");return image;
}
void SavePng(IWICImagingFactory* factory,const std::filesystem::path& path,const Image& image) {
    ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    CheckHr(factory->CreateStream(&stream),"Create PNG stream");CheckHr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open result PNG");
    CheckHr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create PNG encoder");
    CheckHr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize PNG encoder");
    CheckHr(encoder->CreateNewFrame(&frame,nullptr),"Create PNG frame");CheckHr(frame->Initialize(nullptr),"Initialize PNG frame");
    CheckHr(frame->SetSize(image.w,image.h),"Size output PNG");auto format=GUID_WICPixelFormat24bppBGR;
    CheckHr(frame->SetPixelFormat(&format),"Set output PNG format");Require(format==GUID_WICPixelFormat24bppBGR,"Unsupported output PNG format");
    std::vector<unsigned char> bgr(static_cast<size_t>(image.w)*image.h*3);
    for(size_t i=0;i<static_cast<size_t>(image.w)*image.h;++i) {bgr[i*3]=image.rgba[i*4+2];bgr[i*3+1]=image.rgba[i*4+1];bgr[i*3+2]=image.rgba[i*4];}
    CheckHr(frame->WritePixels(image.h,image.w*3,static_cast<UINT>(bgr.size()),bgr.data()),"Write PNG pixels");
    CheckHr(frame->Commit(),"Commit PNG frame");CheckHr(encoder->Commit(),"Commit PNG file");
}
std::vector<unsigned char> Bytes(const std::filesystem::path& path,size_t expected) {
    std::ifstream file(path,std::ios::binary);Require(bool(file),"Input raw plane is missing");
    std::vector<unsigned char> data(expected);file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
    Require(file.gcount()==static_cast<std::streamsize>(expected) && file.peek()==EOF,"Raw plane size mismatch");return data;
}
float Half(unsigned bits) {
    const unsigned exponent=(bits>>10)&31u,mantissa=bits&1023u;
    if(exponent==31)return NAN;
    const float v=exponent ? std::ldexp(1.f+static_cast<float>(mantissa)/1024.f,static_cast<int>(exponent)-15) : std::ldexp(static_cast<float>(mantissa),-24);
    return bits&32768u ? -v : v;
}
float Sample(const Image& im,float x,float y,unsigned channel) {
    x=std::clamp(x,0.f,static_cast<float>(im.w)-1.001f);y=std::clamp(y,0.f,static_cast<float>(im.h)-1.001f);
    const auto ix=static_cast<unsigned>(x),iy=static_cast<unsigned>(y);const float fx=x-static_cast<float>(ix),fy=y-static_cast<float>(iy);
    auto p=[&](unsigned a,unsigned b){return static_cast<float>(im.rgba[(static_cast<size_t>(b)*im.w+a)*4+channel]);};
    return p(ix,iy)*(1-fx)*(1-fy)+p(ix+1,iy)*fx*(1-fy)+p(ix,iy+1)*(1-fx)*fy+p(ix+1,iy+1)*fx*fy;
}
// Photometric diagnostic, not a ground-truth interpolation quality score.
double MidpointError(const Image& previous,const Image& current,const Image& generated,const std::vector<unsigned char>& motion) {
    double sum=0;unsigned long long n=0;
    for(unsigned y=16;y+16<current.h;y+=4)for(unsigned x=16;x+16<current.w;x+=4) {
        const auto i=(static_cast<size_t>(y)*current.w+x)*4;unsigned short bits[2];std::memcpy(bits,motion.data()+i,4);
        const float vx=Half(bits[0]),vy=Half(bits[1]);if(!std::isfinite(vx) || !std::isfinite(vy))continue;
        const float px=static_cast<float>(x),py=static_cast<float>(y);
        const float maxX=static_cast<float>(current.w-1),maxY=static_cast<float>(current.h-1);
        if(px+vx*.5f<1 || py+vy*.5f<1 || px+vx*.5f>=maxX || py+vy*.5f>=maxY ||
           px-vx*.5f<1 || py-vy*.5f<1 || px-vx*.5f>=maxX || py-vy*.5f>=maxY)continue;
        for(unsigned c=0;c<3;++c) {const float estimate=(Sample(previous,static_cast<float>(x)+vx*.5f,static_cast<float>(y)+vy*.5f,c)+Sample(current,static_cast<float>(x)-vx*.5f,static_cast<float>(y)-vy*.5f,c))*.5f;
            sum+=std::abs(estimate-generated.rgba[i+c]);++n;}
    }
    return n ? sum/static_cast<double>(n) : 0;
}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;UINT64 value=0;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    Gpu() {
        ComPtr<IDXGIFactory1> factory;CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create DXGI factory");
        ComPtr<IDXGIAdapter1> chosen;
        for(UINT i=0;;++i) {ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(desc.VendorId==0x10de && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){chosen=adapter;break;}}
        Require(chosen.Get()!=nullptr,"An NVIDIA GPU is required for DLSS replay");
        CheckHr(D3D12CreateDevice(chosen.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Create replay D3D12 device");
        D3D12_COMMAND_QUEUE_DESC q{};CheckHr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Create replay queue");
        CheckHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create replay allocator");
        CheckHr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Create replay command list");
        CheckHr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create replay fence");Require(event!=nullptr,"Replay event unavailable");
    }
    ~Gpu(){if(event)CloseHandle(event);}
    void Finish() {
        CheckHr(list->Close(),"Close replay commands");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        CheckHr(queue->Signal(fence.Get(),++value),"Signal replay fence");CheckHr(fence->SetEventOnCompletion(value,event),"Wait replay fence");
        Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"Replay GPU timeout");uploads.clear();
        CheckHr(allocator->Reset(),"Reset replay allocator");CheckHr(list->Reset(allocator.Get(),nullptr),"Reset replay list");
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;
        desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> resource;CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"Create replay buffer");return resource;
    }
    ComPtr<ID3D12Resource> Texture(unsigned w,unsigned h,DXGI_FORMAT format,const void* data,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.Format=format;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> resource;
        CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,data ? D3D12_RESOURCE_STATE_COPY_DEST : state,nullptr,IID_PPV_ARGS(&resource)),"Create replay texture");
        if(data) {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
            auto upload=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped=nullptr;const D3D12_RANGE empty{0,0};
            CheckHr(upload->Map(0,&empty,&mapped),"Map replay upload");
            for(unsigned y=0;y<h;++y)std::memcpy(static_cast<unsigned char*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,static_cast<const unsigned char*>(data)+static_cast<size_t>(y)*w*4,static_cast<size_t>(w)*4);
            upload->Unmap(0,nullptr);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=resource.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            Transition(list.Get(),resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);uploads.push_back(upload);
        }
        return resource;
    }
    void Copy(ID3D12Resource* dst,ID3D12Resource* src) {
        Transition(list.Get(),dst,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(list.Get(),src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(dst,src);
        Transition(list.Get(),dst,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list.Get(),src,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    Image Read(ID3D12Resource* resource) {
        const auto desc=resource->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);auto read=Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(list.Get(),resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        src.pResource=resource;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Transition(list.Get(),resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Finish();
        Image image;image.w=static_cast<unsigned>(desc.Width);image.h=desc.Height;image.rgba.resize(static_cast<size_t>(image.w)*image.h*4);
        void* mapped=nullptr;const D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};CheckHr(read->Map(0,&range,&mapped),"Read replay output");
        for(unsigned y=0;y<image.h;++y)std::memcpy(image.rgba.data()+static_cast<size_t>(y)*image.w*4,static_cast<unsigned char*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,static_cast<size_t>(image.w)*4);
        const D3D12_RANGE empty{0,0};read->Unmap(0,&empty);return image;
    }
};
struct NgxSession {
    ID3D12Device* device;bool initialized=false;
    NgxSession(ID3D12Device* dev):device(dev) {
        const auto directory=processing::ModuleDirectory();const wchar_t* paths[]={directory.c_str()};
        wchar_t temp[32768]{};Require(GetTempPathW(32768,temp)>0,"Temporary directory unavailable");
        const auto cache=std::filesystem::path(temp)/L"MPC-NeuralRenderer"/L"Replay-cache";std::filesystem::create_directories(cache);
        NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo.Path=paths;info.PathListInfo.Length=1;
        ngx::Check(NVSDK_NGX_D3D12_Init_with_ProjectID("80b3e1a9-7dab-4bb3-b64c-5d923d8cbf40",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"MPC-NeuralRenderer-replay",cache.c_str(),device,&info),"Initialize replay NGX");initialized=true;
    }
    ~NgxSession(){if(initialized)NVSDK_NGX_D3D12_Shutdown1(device);}
};
std::filesystem::path PickFolder() {
    ComPtr<IFileOpenDialog> dialog;CheckHr(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Open sample folder picker");
    DWORD options=0;CheckHr(dialog->GetOptions(&options),"Get picker options");CheckHr(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST),"Set picker options");
    dialog->SetTitle(L"Choose the extracted DLSS FG sample folder");const auto shown=dialog->Show(nullptr);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};CheckHr(shown,"Select sample folder");
    ComPtr<IShellItem> item;CheckHr(dialog->GetResult(&item),"Get sample folder");PWSTR path=nullptr;CheckHr(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"Get sample path");
    const std::filesystem::path result(path);CoTaskMemFree(path);return result;
}
void SelfTest(IWICImagingFactory* factory) {
    Require(Half(0x3c00)==1 && Half(0xbc00)==-1,"Half-float conversion failed");
    Image image;image.w=image.h=8;image.rgba.resize(8*8*4);
    for(size_t i=0;i<64;++i){image.rgba[i*4]=static_cast<unsigned char>(i);image.rgba[i*4+1]=80;image.rgba[i*4+2]=160;image.rgba[i*4+3]=255;}
    wchar_t temp[32768]{};Require(GetTempPathW(32768,temp)>0,"Test temp unavailable");
    const auto path=std::filesystem::path(temp)/(L"mpc-replay-selftest-"+std::to_wstring(GetCurrentProcessId())+L".png");
    SavePng(factory,path,image);Require(LoadPng(factory,path).rgba==image.rgba,"Replay PNG round-trip failed");std::filesystem::remove(path);
    Require(std::abs(Sample(image,1.5f,2.5f,0)-21.5f)<.001f,"Bilinear image sampling failed");
    Image a,b,m;a.w=b.w=m.w=64;a.h=b.h=m.h=64;
    a.rgba.resize(64*64*4,255);b.rgba=a.rgba;m.rgba=a.rgba;
    std::vector<unsigned char> vectors(a.rgba.size(),0);
    for(size_t i=0;i<64*64;++i) {
        for(unsigned c=0;c<3;++c) {
            a.rgba[i*4+c]=static_cast<unsigned char>(i%64);
            b.rgba[i*4+c]=static_cast<unsigned char>(i%64+8);
            m.rgba[i*4+c]=static_cast<unsigned char>(i%64+4);
        }
        const unsigned short vx=0x4800;std::memcpy(vectors.data()+i*4,&vx,2);
    }
    Require(MidpointError(a,b,m,vectors)<.001,"Midpoint diagnostic failed for translated ramp");
    std::fill(m.rgba.begin(),m.rgba.end(),0);
    Require(MidpointError(a,b,m,vectors)>20,"Midpoint diagnostic missed corrupt output");
    std::cout<<"Replay image I/O and sampler self-test passed (NVIDIA not required)\n";
}
int wmain(int argc,wchar_t** argv) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 1;struct ComScope{~ComScope(){CoUninitialize();}} scope;
    try {
        ComPtr<IWICImagingFactory> factory;CheckHr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create replay image factory");
        if(argc>1 && std::wstring_view(argv[1])==L"--self-test"){SelfTest(factory.Get());return 0;}
        const auto input=argc>1 ? std::filesystem::path(argv[1]) : PickFolder();if(input.empty())return 0;
        std::ifstream file(input/L"manifest.txt");std::string manifest((std::istreambuf_iterator<char>(file)),{});
        Require(manifest.find("hdr=0")!=std::string::npos && manifest.find("previous_opaque_sdr=1")!=std::string::npos && manifest.find("current_opaque_sdr=1")!=std::string::npos,"Replay currently requires an opaque 8-bit SDR sample");
        const auto previous=LoadPng(factory.Get(),input/L"previous.png"),current=LoadPng(factory.Get(),input/L"current.png");
        Require(previous.w==current.w && previous.h==current.h,"Sample dimensions differ");
        const auto bytes=static_cast<size_t>(current.w)*current.h*4;
        const auto motion=Bytes(input/L"motion.raw",bytes),depth=Bytes(input/L"depth.raw",bytes);std::vector<unsigned char> zero(bytes,0);
        const auto output=input/(L"Replay-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(output);
        std::ofstream report(output/L"report.txt");report.exceptions(std::ios::badbit|std::ios::failbit);
        report<<"DLSS FG captured-pair replay v1\nThree repeated previous frames warm up each fresh feature, then the captured current frame.\nInput lifetime and motionVectorsDilated are varied independently. Prepared is a metadata-only probe: no dilation is applied.\nPhotometric midpoint error is a diagnostic, not ground truth. The replay executable/profile and pre-capture history differ from the live player.\n\n";
        Gpu gpu;NgxSession ngx(gpu.device.Get());
        auto a=gpu.Texture(current.w,current.h,DXGI_FORMAT_R8G8B8A8_UNORM,previous.rgba.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto b=gpu.Texture(current.w,current.h,DXGI_FORMAT_R8G8B8A8_UNORM,current.rgba.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto mv0=gpu.Texture(current.w,current.h,DXGI_FORMAT_R16G16_FLOAT,zero.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto mv=gpu.Texture(current.w,current.h,DXGI_FORMAT_R16G16_FLOAT,motion.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto z=gpu.Texture(current.w,current.h,DXGI_FORMAT_R32_FLOAT,depth.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto colorScratch=gpu.Texture(current.w,current.h,DXGI_FORMAT_R8G8B8A8_UNORM,previous.rgba.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto motionScratch=gpu.Texture(current.w,current.h,DXGI_FORMAT_R16G16_FLOAT,zero.data(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto made=gpu.Texture(current.w,current.h,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);gpu.Finish();
        struct Variant {const char* name;bool color,motion,prepared;};
        const Variant variants[]={{"reuse_inputs",false,false,false},{"retain_color",true,false,false},{"retain_motion",false,true,false},{"retain_inputs",true,true,false},{"reuse_inputs_prepared",false,false,true},{"retain_inputs_prepared",true,true,true}};
        for(const auto& variant:variants) {
            std::cout<<variant.name<<" ..."<<std::endl;
            framegen::DlssMultiFrameGeneration generator;generator.Create(gpu.device.Get(),gpu.list.Get(),current.w,current.h,current.w,current.h,false);
            generator.SetMotionVectorsDilated(variant.prepared);generator.SetGuidanceInfo("captured replay field");gpu.Finish();
            std::array<ID3D12Resource*,3> outputs{made.Get(),nullptr,nullptr};unsigned published=0;
            for(unsigned step=0;step<4;++step) {
                auto* source=step==3 ? b.Get() : a.Get();auto* vectors=step==3 ? mv.Get() : mv0.Get();
                if(!variant.color)gpu.Copy(colorScratch.Get(),source);
                if(!variant.motion)gpu.Copy(motionScratch.Get(),vectors);
                const auto result=generator.Evaluate(gpu.list.Get(),variant.color ? source : colorScratch.Get(),z.Get(),variant.motion ? vectors : motionScratch.Get(),outputs,1,step==0);
                gpu.Finish();ngx::Check(result,"Replay DLSS evaluation");published=generator.Collect();
            }
            report<<variant.name<<": published="<<published<<"; ";
            if(published) {
                const auto result=gpu.Read(made.Get());SavePng(factory.Get(),output/(std::string(variant.name)+".png"),result);
                report<<"midpoint_error="<<MidpointError(previous,current,result,motion)<<'\n';
            }else report<<"SDK disabled interpolation\n";
            const auto runtime=generator.RuntimeStatus();
            const int length=WideCharToMultiByte(CP_UTF8,0,runtime.data(),static_cast<int>(runtime.size()),nullptr,0,nullptr,nullptr);
            std::string utf8(static_cast<size_t>(length),0);WideCharToMultiByte(CP_UTF8,0,runtime.data(),static_cast<int>(runtime.size()),utf8.data(),length,nullptr,nullptr);
            report<<utf8<<"\n\n";report.flush();
        }
        report<<"Original manifest:\n"<<manifest;report.close();
        ShellExecuteW(nullptr,L"open",output.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;
    }catch(const std::exception& e) {
        std::cerr<<e.what()<<std::endl;if(argc<2){const std::string text=e.what();MessageBoxA(nullptr,text.c_str(),"DLSS FG replay failed",MB_OK|MB_ICONERROR);}return 1;
    }
}
