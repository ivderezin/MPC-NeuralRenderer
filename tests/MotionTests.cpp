// SPDX-License-Identifier: GPL-3.0-or-later
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../Source/NeuralRendering/MotionShaders.h"
#include "../Source/NeuralRendering/MotionEstimator.h"
#include "../Source/NeuralRendering/MotionRepair.h"
#include "../Source/Upscaling/MotionAdaptiveShaders.h"
#include "../Source/FrameGeneration/FrameContentProbe.h"
#include "../Source/FrameGeneration/ExactInputRepeat.h"
#include "../Source/EchoStorm/addon/compose11.h"
#include <limits>
using Microsoft::WRL::ComPtr;
void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("D3D11 test failed: " + std::to_string(static_cast<unsigned long>(result))); }
void Require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
struct Image {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};
struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Buffer> constants;
    Gpu() {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context));
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = 32; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Check(device->CreateBuffer(&desc, nullptr, &constants));
    }
    Image Texture(UINT width, UINT height, DXGI_FORMAT format, UINT bind, const void* data = nullptr, UINT pitch = 0) {
        Image image;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=format; desc.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=data; initial.SysMemPitch=pitch;
        Check(device->CreateTexture2D(&desc, data ? &initial : nullptr, &image.texture));
        if (bind & D3D11_BIND_SHADER_RESOURCE) Check(device->CreateShaderResourceView(image.texture.Get(), nullptr, &image.srv));
        if (bind & D3D11_BIND_UNORDERED_ACCESS) Check(device->CreateUnorderedAccessView(image.texture.Get(), nullptr, &image.uav));
        return image;
    }
    ComPtr<ID3D11ComputeShader> Compile(const char* source) {
        ComPtr<ID3DBlob> code, errors;
        auto result = D3DCompile(source, strlen(source), "MotionTests", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (FAILED(result) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        Check(result);
        ComPtr<ID3D11ComputeShader> shader;
        Check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        return shader;
    }
    void Dispatch(ID3D11ComputeShader* shader, Image& source, Image* previous, Image& target, UINT width, UINT height, UINT reset, UINT x=0, UINT y=0, bool hdr=false, Image* secondOutput=nullptr, Image* thirdInput=nullptr) {
        UINT data[] = {width,height,x,y,reset,hdr ? 1u : 0u,0,0};
        context->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);
        ID3D11Buffer* buffers[] = {constants.Get()};
        ID3D11ShaderResourceView* views[] = {source.srv.Get(), previous ? previous->srv.Get() : nullptr, thirdInput ? thirdInput->srv.Get() : nullptr};
        ID3D11UnorderedAccessView* uavs[] = {target.uav.Get(), secondOutput ? secondOutput->uav.Get() : nullptr};
        context->CSSetConstantBuffers(0,1,buffers);
        context->CSSetShaderResources(0,3,views);
        context->CSSetUnorderedAccessViews(0,2,uavs,nullptr);
        context->CSSetShader(shader,nullptr,0);
        context->Dispatch((width+7)/8,(height+7)/8,1);
        views[0]=views[1]=views[2]=nullptr; uavs[0]=uavs[1]=nullptr;
        context->CSSetShaderResources(0,3,views);
        context->CSSetUnorderedAccessViews(0,2,uavs,nullptr);
    }
    std::vector<float> Read(Image& image, UINT components) {
        D3D11_TEXTURE2D_DESC desc{}; image.texture->GetDesc(&desc);
        desc.BindFlags=desc.MiscFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Check(device->CreateTexture2D(&desc,nullptr,&staging));
        context->CopyResource(staging.Get(),image.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        std::vector<float> pixels(static_cast<size_t>(desc.Width)*desc.Height*components);
        for (UINT y=0; y<desc.Height; ++y)
            memcpy(pixels.data()+static_cast<size_t>(y)*desc.Width*components,static_cast<const char*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,static_cast<size_t>(desc.Width)*components*sizeof(float));
        context->Unmap(staging.Get(),0);
        return pixels;
    }
};
void TestEchoPresentation(Gpu& gpu) {
    const UINT w=17,h=9;
    std::vector<float> pixels(w*h*4,.2f);
    auto input=gpu.Texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,pixels.data(),w*16);
    framegen::ExactInputRepeat repeat;
    RECT roi{0,0,w,h};
    auto compare=[&](bool reset=false) {
        repeat.Record(gpu.context.Get(),input.texture.Get(),input.srv.Get(),roi,reset);
        // A staging map here stands in for the renderer's existing completion fence.
        gpu.Read(input,4);
        return repeat.Same(gpu.context.Get());
    };
    Require(!compare(),"First input must be distinct");
    Require(compare(),"Identical full-resolution input must repeat");
    pixels[(w*h-1)*4+1]=.20001f;
    gpu.context->UpdateSubresource(input.texture.Get(),0,nullptr,pixels.data(),w*16,0);
    Require(!compare(),"One changed channel at odd image boundary must be distinct");
    Require(compare(),"The updated input must become the repeat baseline");
    roi={1,1,16,8};pixels[0]=.8f;
    gpu.context->UpdateSubresource(input.texture.Get(),0,nullptr,pixels.data(),w*16,0);
    Require(compare(),"Changes outside the video ROI must not trigger new content");
    pixels[(w+1)*4]=std::numeric_limits<float>::quiet_NaN();
    gpu.context->UpdateSubresource(input.texture.Get(),0,nullptr,pixels.data(),w*16,0);
    Require(!compare(),"Non-finite samples must fail open");
    repeat.Invalidate();Require(!compare(),"Seek must invalidate repeat history");

    Compose11 compose;
    Require(compose.Init(gpu.device.Get(),[](const char* message){std::cerr<<message<<'\n';}),"Native NR compose initialization");
    std::vector<float> deltaPixels(w*h*4,.1f);
    auto delta=gpu.Texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,deltaPixels.data(),w*16);
    for (auto fmt:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}) {
        std::vector<UINT> base(w*h,0xff333333);
        auto target=gpu.Texture(w,h,fmt,D3D11_BIND_RENDER_TARGET,base.data(),w*4);
        Compose11::Args a;a.target=target.texture.Get();a.delta=delta.srv.Get();a.hiProtect=1;
        Require(compose.Run(gpu.context.Get(),a),"Compose must support RGBA and BGRA swapchain buffers");
        D3D11_TEXTURE2D_DESC d{};target.texture->GetDesc(&d);
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Check(gpu.device->CreateTexture2D(&d,nullptr,&staging));
        gpu.context->CopyResource(staging.Get(),target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE read{};Check(gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&read));
        for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) {
            const auto* px=static_cast<const unsigned char*>(read.pData)+y*read.RowPitch+x*4;
            Require(px[0]>=75 && px[0]<=78 && px[1]==px[0] && px[2]==px[0],"Delta must reach every presented pixel");
        }
        gpu.context->Unmap(staging.Get(),0);
    }
    compose.Shutdown();
}
int main() {
    try {
        Gpu gpu;
        TestEchoPresentation(gpu);
        const UINT size=64;
        std::vector<float> oldPixels(size*size*4), newPixels(oldPixels.size());
        unsigned state=456789;
        for (UINT y=0; y<size; ++y) for (UINT x=0; x<size; ++x) {
            state ^= state<<13; state ^= state>>17; state ^= state<<5;
            const float value=static_cast<float>(state & 65535)/65535.0f;
            const auto index=(static_cast<size_t>(y)*size+x)*4;
            oldPixels[index]=oldPixels[index+1]=oldPixels[index+2]=value; oldPixels[index+3]=1;
        }
        for (UINT y=0; y<size; ++y) for (UINT x=0; x<size; ++x) {
            const UINT px=(std::min)(x+4,size-1);
            memcpy(&newPixels[(static_cast<size_t>(y)*size+x)*4],&oldPixels[(static_cast<size_t>(y)*size+px)*4],4*sizeof(float));
        }
        auto previous=gpu.Texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,oldPixels.data(),size*16);
        auto current=gpu.Texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,newPixels.data(),size*16);
        auto output=gpu.Texture(size,size,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        auto shader=gpu.Compile(neural::MotionShader);
        gpu.Dispatch(shader.Get(),current,&previous,output,size,size,0);
        auto pixels=gpu.Read(output,2);
        for (UINT y=16;y<48;y+=8) for (UINT x=16;x<48;x+=8) {
            const auto index=(static_cast<size_t>(y)*size+x)*2;
            Require(pixels[index]==4 && pixels[index+1]==0,"Motion must point from current pixel to previous (+4,0)");
        }
        for (const auto delta : {std::array<int,2>{-16,-16}, std::array<int,2>{16,16}, std::array<int,2>{0,16}, std::array<int,2>{16,0}}) {
            for (UINT y=0; y<size; ++y) for (UINT x=0; x<size; ++x) {
                const auto px = static_cast<UINT>(std::clamp(static_cast<int>(x)+delta[0],0,static_cast<int>(size)-1));
                const auto py = static_cast<UINT>(std::clamp(static_cast<int>(y)+delta[1],0,static_cast<int>(size)-1));
                memcpy(&newPixels[(static_cast<size_t>(y)*size+x)*4],&oldPixels[(static_cast<size_t>(py)*size+px)*4],4*sizeof(float));
            }
            auto translated=gpu.Texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,newPixels.data(),size*16);
            gpu.Dispatch(shader.Get(),translated,&previous,output,size,size,0);
            pixels=gpu.Read(output,2);
            for (UINT y=16;y<48;y+=8) for (UINT x=16;x<48;x+=8) {
                const auto index=(static_cast<size_t>(y)*size+x)*2;
                Require(pixels[index]==static_cast<float>(delta[0]) && pixels[index+1]==static_cast<float>(delta[1]),"Parallel search must cover every motion candidate");
            }
        }
        gpu.Dispatch(shader.Get(),current,&previous,output,size,size,1);
        pixels=gpu.Read(output,2);
        for (float value:pixels) Require(value==0,"Reset must produce zero motion");
        gpu.Dispatch(shader.Get(),previous,&previous,output,size,size,0);
        pixels=gpu.Read(output,2);
        for (float value:pixels) Require(value==0,"Static frame must produce zero motion");
        std::vector<float> flatPixels(13*11*4, 0.5f);
        auto flat=gpu.Texture(13,11,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,flatPixels.data(),13*16);
        auto oddMotion=gpu.Texture(13,11,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        gpu.Dispatch(shader.Get(),flat,&flat,oddMotion,13,11,0);
        pixels=gpu.Read(oddMotion,2);
        for (float value:pixels) Require(value==0,"Flat regions and partial workgroups must produce zero motion");
        // Exercise the production EchoStorm path, including its history and pyramid orchestration.
        neural::MotionEstimator echo;
        constexpr UINT flowWidth=256, flowHeight=128;
        std::vector<float> referencePixels(flowWidth*flowHeight*4), movedPixels(referencePixels.size());
        auto referenceValue=[](UINT x,UINT y) {
            unsigned v=(x/4+1)*73856093u ^ (y/4+1)*19349663u;
            v^=v>>13; v*=1274126177u;
            return .1f+.8f*static_cast<float>(v&65535)/65535.0f;
        };
        for (UINT y=0;y<flowHeight;++y) for (UINT x=0;x<flowWidth;++x) {
            const size_t at=(static_cast<size_t>(y)*flowWidth+x)*4;
            referencePixels[at]=referencePixels[at+1]=referencePixels[at+2]=referenceValue(x,y);
            referencePixels[at+3]=1;
        }
        auto reference=gpu.Texture(flowWidth,flowHeight,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,referencePixels.data(),flowWidth*16);
        auto dense=gpu.Texture(flowWidth,flowHeight,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        auto distrust=gpu.Texture(flowWidth,flowHeight,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        for (const auto shift : {std::array<int,2>{48,0},{-24,8},{5,-3}}) {
            echo.Initialize(gpu.device.Get(),flowWidth,flowHeight);
            Require(echo.Levels()>=3,"EchoStorm must search a multilevel pyramid");
            echo.Dispatch(gpu.context.Get(),reference.srv.Get(),dense.uav.Get(),distrust.uav.Get(),false);
            for (float value:gpu.Read(dense,2)) Require(value==0,"First EchoStorm frame must reset motion");
            for (UINT y=0;y<flowHeight;++y) for (UINT x=0;x<flowWidth;++x) {
                const auto px=static_cast<UINT>(std::clamp(static_cast<int>(x)+shift[0],0,static_cast<int>(flowWidth)-1));
                const auto py=static_cast<UINT>(std::clamp(static_cast<int>(y)+shift[1],0,static_cast<int>(flowHeight)-1));
                const size_t at=(static_cast<size_t>(y)*flowWidth+x)*4;
                const size_t from=(static_cast<size_t>(py)*flowWidth+px)*4;
                std::copy_n(referencePixels.data()+from,4,movedPixels.data()+at);
            }
            auto moved=gpu.Texture(flowWidth,flowHeight,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,movedPixels.data(),flowWidth*16);
            echo.Dispatch(gpu.context.Get(),moved.srv.Get(),dense.uav.Get(),distrust.uav.Get(),false);
            const auto field=gpu.Read(dense,2);
            float error=0; unsigned count=0;
            for (UINT y=32;y<96;y+=4) for (UINT x=64;x<160;x+=4) {
                const size_t at=(static_cast<size_t>(y)*flowWidth+x)*2;
                error+=std::abs(field[at]-static_cast<float>(shift[0]))+std::abs(field[at+1]-static_cast<float>(shift[1])); ++count;
            }
            Require(error/static_cast<float>(count)<1.0f,"EchoStorm must recover large, negative and non-four-pixel motion in source-pixel units");
            for (float value:gpu.Read(distrust,1)) Require(std::isfinite(value)&&value>=0&&value<=1,"Motion distrust must remain finite and normalized");
            echo.Dispatch(gpu.context.Get(),moved.srv.Get(),dense.uav.Get(),distrust.uav.Get(),true);
            for (float value:gpu.Read(dense,2)) Require(value==0,"Seek/reset must discard EchoStorm history");
            echo.Dispatch(gpu.context.Get(),moved.srv.Get(),dense.uav.Get(),distrust.uav.Get(),false);
            for (float value:gpu.Read(dense,2)) Require(value==0,"Static EchoStorm frames must have exactly zero motion");
        }
        // Fixed HUD over a moving background: its exact stationary match must win.
        for (UINT y=40;y<72;++y) for (UINT x=88;x<120;++x) {
            const size_t at=(static_cast<size_t>(y)*flowWidth+x)*4;
            std::copy_n(referencePixels.data()+at,4,movedPixels.data()+at);
        }
        auto hud=gpu.Texture(flowWidth,flowHeight,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,movedPixels.data(),flowWidth*16);
        echo.Dispatch(gpu.context.Get(),reference.srv.Get(),dense.uav.Get(),distrust.uav.Get(),true);
        echo.Dispatch(gpu.context.Get(),hud.srv.Get(),dense.uav.Get(),distrust.uav.Get(),false);
        const auto hudFlow=gpu.Read(dense,2);
        for (UINT y=44;y<68;++y) for (UINT x=92;x<116;++x) {
            const size_t at=(static_cast<size_t>(y)*flowWidth+x)*2;
            Require(hudFlow[at]==0&&hudFlow[at+1]==0,"Stationary HUD must not inherit background motion");
        }
        echo.Initialize(gpu.device.Get(),13,11);
        auto oddDistrust=gpu.Texture(13,11,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        echo.Dispatch(gpu.context.Get(),flat.srv.Get(),oddMotion.uav.Get(),oddDistrust.uav.Get(),true);
        echo.Dispatch(gpu.context.Get(),flat.srv.Get(),oddMotion.uav.Get(),oddDistrust.uav.Get(),false);
        for (float value:gpu.Read(oddMotion,2)) Require(value==0,"Odd/flat frames must not generate noisy EchoStorm motion");
        // FG-only consensus: a flat panel and repeating shelf labels are ambiguous
        // to a local matcher. A textured stationary HUD must keep its own zero vector.
        neural::MotionRepair repair;
        constexpr UINT repairWidth=256, repairHeight=128;
        std::vector<float> repairBefore(repairWidth*repairHeight*4), repairNow(repairBefore.size());
        std::vector<float> rawField(repairWidth*repairHeight*2);
        auto paint=[&](UINT x,UINT y) {
            if (x>=64 && x<160 && y>=32 && y<96) return .4f; // flat moving panel
            if (x>=32 && x<224 && y>=96) return (x%8<4) ? .2f : .8f; // repeating labels
            return referenceValue(x,y);
        };
        for (UINT y=0;y<repairHeight;++y) for (UINT x=0;x<repairWidth;++x) {
            const size_t at=(static_cast<size_t>(y)*repairWidth+x)*4;
            repairBefore[at]=repairBefore[at+1]=repairBefore[at+2]=paint(x,y); repairBefore[at+3]=1;
        }
        for (int direction : {1,-1}) {
            const float dx=static_cast<float>(direction*16);
            for (UINT y=0;y<repairHeight;++y) for (UINT x=0;x<repairWidth;++x) {
                const auto px=static_cast<UINT>(std::clamp(static_cast<int>(x)+direction*16,0,static_cast<int>(repairWidth)-1));
                const size_t at=(static_cast<size_t>(y)*repairWidth+x)*4;
                std::copy_n(repairBefore.data()+(static_cast<size_t>(y)*repairWidth+px)*4,4,repairNow.data()+at);
                const bool ambiguous=(x>=88 && x<136 && y>=48 && y<80) || (x>=88 && x<136 && y>=100 && y<124);
                rawField[at/2]=ambiguous ? 0.f : dx; rawField[at/2+1]=0;
                // A fixed textured overlay, where camera motion is a worse match.
                if (x>=176 && x<208 && y>=40 && y<72) {
                    std::copy_n(repairBefore.data()+at,4,repairNow.data()+at);
                    rawField[at/2]=0;
                }
            }
            auto oldFrame=gpu.Texture(repairWidth,repairHeight,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,repairBefore.data(),repairWidth*16);
            auto newFrame=gpu.Texture(repairWidth,repairHeight,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,repairNow.data(),repairWidth*16);
            auto rawMotion=gpu.Texture(repairWidth,repairHeight,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_SHADER_RESOURCE,rawField.data(),repairWidth*8);
            auto repaired=gpu.Texture(repairWidth,repairHeight,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
            auto confidence=gpu.Texture(repairWidth,repairHeight,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
            repair.Initialize(gpu.device.Get(),repairWidth,repairHeight);
            repair.Dispatch(gpu.context.Get(),oldFrame.srv.Get(),newFrame.srv.Get(),rawMotion.srv.Get(),nullptr,
                            repaired.uav.Get(),confidence.uav.Get(),1.f);
            auto field=gpu.Read(repaired,2);
            for (UINT y : {60u,110u}) for (UINT x=96;x<128;x+=4) {
                const size_t at=(static_cast<size_t>(y)*repairWidth+x)*2;
                Require(std::abs(field[at]-dx)<.05f && std::abs(field[at+1])<.05f,
                        "FG repair must recover camera motion on flat and repeating patches in both directions");
            }
            for (UINT y=48;y<64;++y) for (UINT x=184;x<200;++x) {
                const size_t at=(static_cast<size_t>(y)*repairWidth+x)*2;
                Require(field[at]==0 && field[at+1]==0,"FG repair must not drag a textured stationary HUD");
            }
            for (float value:field) Require(std::isfinite(value),"FG repair must produce finite motion");
            Require(gpu.Read(rawMotion,2)==rawField,"FG repair must preserve NR/SR's original motion field");
            const auto confidencePixels=gpu.Read(confidence,1);
            Require(confidencePixels[60*repairWidth+100]==1,"Fast camera motion must reject SR history after repair");
            Require(confidencePixels[56*repairWidth+192]==0,"A fixed HUD must retain reliable SR history");
            std::fill(rawField.begin(),rawField.end(),0.f);
            auto stillMotion=gpu.Texture(repairWidth,repairHeight,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_SHADER_RESOURCE,rawField.data(),repairWidth*8);
            repair.Dispatch(gpu.context.Get(),oldFrame.srv.Get(),oldFrame.srv.Get(),stillMotion.srv.Get(),nullptr,repaired.uav.Get());
            for (float value:gpu.Read(repaired,2)) Require(value==0,"Static/reset FG vectors must stay exactly zero");
        }
        repair.Initialize(gpu.device.Get(),13,11);
        auto tinyVectors=gpu.Texture(13,11,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_SHADER_RESOURCE,std::vector<float>(13*11*2,0.f).data(),13*8);
        repair.Dispatch(gpu.context.Get(),flat.srv.Get(),flat.srv.Get(),tinyVectors.srv.Get(),nullptr,oddMotion.uav.Get());
        for (float value:gpu.Read(oddMotion,2)) Require(value==0,"FG repair must handle odd and partial tiles");
        // Full-resolution 4K vectors exceed the upstream half-size voting range.
        // Keep the test short vertically, with partial edge cells/tiles.
        constexpr UINT wideWidth=3841, wideHeight=17;
        std::vector<float> widePixels(wideWidth*wideHeight*4,.4f), wideMotion(wideWidth*wideHeight*2,0.f);
        auto wideFrame=gpu.Texture(wideWidth,wideHeight,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,widePixels.data(),wideWidth*16);
        auto wideOutput=gpu.Texture(wideWidth,wideHeight,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        repair.Initialize(gpu.device.Get(),wideWidth,wideHeight);
        for (float cameraShift : {96.f,-80.f}) {
            for (UINT y=0;y<wideHeight;++y) for (UINT x=0;x<wideWidth;++x)
                wideMotion[(static_cast<size_t>(y)*wideWidth+x)*2]=(x>=1920 && x<1952) ? 0.f : cameraShift;
            auto wideInput=gpu.Texture(wideWidth,wideHeight,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_SHADER_RESOURCE,wideMotion.data(),wideWidth*8);
            repair.Dispatch(gpu.context.Get(),wideFrame.srv.Get(),wideFrame.srv.Get(),wideInput.srv.Get(),nullptr,wideOutput.uav.Get());
            const auto field=gpu.Read(wideOutput,2);
            Require(std::abs(field[(8*wideWidth+1936)*2]-cameraShift)<.05f,
                    "4K consensus must retain camera vectors beyond 64 source pixels");
        }
        // Provider-independent protection also covers DLSS models ignoring bias masks.
        auto adaptiveShader=gpu.Compile(upscaling::MotionAdaptiveShader);
        D3D11_SAMPLER_DESC sampleDesc{};
        sampleDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampleDesc.AddressU=sampleDesc.AddressV=sampleDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampleDesc.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> linearSampler;
        Check(gpu.device->CreateSamplerState(&sampleDesc,&linearSampler));
        ID3D11SamplerState* sampleState=linearSampler.Get();
        gpu.context->CSSetSamplers(0,1,&sampleState);
        std::vector<float> invented(13*11*4,.9f), plain(7*5*4,.25f);
        auto up=gpu.Texture(13,11,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,invented.data(),13*16);
        auto spatialInput=gpu.Texture(7,5,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,plain.data(),7*16);
        auto adaptiveOutput=gpu.Texture(13,11,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        for (float d : {0.f,1.f}) {
            std::vector<float> maskValues(7*5,d);
            auto mask=gpu.Texture(7,5,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE,maskValues.data(),7*4);
            gpu.Dispatch(adaptiveShader.Get(),up,&spatialInput,adaptiveOutput,13,11,0,0,0,false,nullptr,&mask);
            const auto result=gpu.Read(adaptiveOutput,4);
            for (size_t k=0;k<result.size();k+=4)
                Require(std::abs(result[k]-(d==0 ? .9f : .25f))<.0001f,
                        "SR must retain trusted detail and replace untrusted history with this frame for every provider");
        }
        // Probe the actual production diagnostic without affecting frame history.
        framegen::FrameContentProbe probe;
        auto probeImage=[&](UINT w,UINT h,float v) {
            std::vector<float> rgba(static_cast<size_t>(w)*h*4,v);
            return gpu.Texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,rgba.data(),w*16);
        };
        auto rawA=probeImage(13,11,.2f), rawB=probeImage(13,11,.4f);
        auto realA=probeImage(7,5,.2f), realB=probeImage(7,5,.3f), realC=probeImage(7,5,.4f);
        auto middle=probeImage(7,5,.25f);
        auto finishProbe=[&]() {
            gpu.context->Flush(); // tests may wait; the production Collect never flushes or waits
            for (unsigned i=0;i<5000 && probe.Pending();++i) {
                probe.Collect(gpu.context.Get());
                if (probe.Pending()) Sleep(1);
            }
            Require(!probe.Pending(),"Content probe must complete on WARP");
        };
        probe.Submit(gpu.context.Get(),rawA.texture.Get(),realA.texture.Get(),{},0);
        Require(!probe.Pending() && probe.metrics.samples==0,"First diagnostic frame must only establish endpoints");
        probe.Submit(gpu.context.Get(),rawA.texture.Get(),realB.texture.Get(),{middle.texture.Get(),nullptr,nullptr},1);
        finishProbe();
        Require(probe.metrics.samples==1 && probe.metrics.unchangedInputs==1 && probe.metrics.delta[0]==0,
                "Repeated raw input must be visible even if NR changes its output");
        Require(std::abs(probe.metrics.delta[1]-25.5f)<.001f &&
                std::abs(probe.metrics.delta[2]-12.75f)<.001f && std::abs(probe.metrics.delta[3]-12.75f)<.001f,
                "Content diagnostics must compare generated frames against both correct real endpoints");
        probe.Submit(gpu.context.Get(),rawB.texture.Get(),realC.texture.Get(),{realC.texture.Get(),nullptr,nullptr},1);
        finishProbe();
        Require(probe.metrics.samples==2 && probe.metrics.unchangedInputs==1 && probe.metrics.delta[2]==0 &&
                std::abs(probe.metrics.delta[0]-51.f)<.001f,
                "A generated copy of the current endpoint must be distinguishable from changing raw input");
        probe.Submit(gpu.context.Get(),rawA.texture.Get(),realA.texture.Get(),{},0);
        probe.Invalidate();
        finishProbe();
        Require(probe.metrics.samples==0,"Invalidated pre-seek probe results must not reappear");
        auto crop=gpu.Texture(13,11,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        auto prepare=gpu.Compile(neural::PrepareShader.c_str());
        gpu.Dispatch(prepare.Get(),previous,nullptr,crop,13,11,0,5,7);
        pixels=gpu.Read(crop,4);
        for (UINT y=0;y<11;++y) for (UINT x=0;x<13;++x)
            Require(pixels[(y*13+x)*4]==oldPixels[((y+7)*size+x+5)*4],"Crop origin / odd extent must preserve video pixels");
        // High-bit-depth SDR must reach the same normalized 8-bit model input, without channel swaps.
        Require(neural::IsSdrInputFormat(DXGI_FORMAT_R10G10B10A2_UNORM) &&
                neural::IsSdrInputFormat(DXGI_FORMAT_R16G16B16A16_FLOAT), "High-bit-depth SDR formats must be accepted");
        Require(!neural::IsSdrInputFormat(DXGI_FORMAT_P010), "YUV must be converted to RGB before neural processing");
        const uint32_t packed10 = 512u | (256u << 10) | (1023u << 20) | (3u << 30);
        const std::array<uint16_t,4> halfPixels = {0x3800, 0x3400, 0x3c00, 0x3c00}; // .5, .25, 1, 1
        auto input10 = gpu.Texture(1,1,DXGI_FORMAT_R10G10B10A2_UNORM,D3D11_BIND_SHADER_RESOURCE,&packed10,4);
        auto input16 = gpu.Texture(1,1,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,halfPixels.data(),8);
        auto normalized = gpu.Texture(1,1,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        auto readable = gpu.Texture(1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        for (auto* input : {&input10,&input16}) {
            gpu.Dispatch(prepare.Get(),*input,nullptr,normalized,1,1,0);
            gpu.Dispatch(prepare.Get(),normalized,nullptr,readable,1,1,0);
            const auto rgb = gpu.Read(readable,4);
            Require(std::abs(rgb[0]-.5f)<=1.0f/255 && std::abs(rgb[1]-.25f)<=1.0f/255 && rgb[2]==1 && rgb[3]==1,
                    "10-bit and float16 SDR must normalize to the model input without changing RGB order");
        }
        // The HDR proxy/residual round trip must preserve absolute highlights and wide-gamut colours.
        auto composite = gpu.Compile(processing::HdrCompositeShader.c_str());
        auto pq = [](float nits) {
            const double value=std::pow(static_cast<double>(nits)/10000.0,.1593017578125);
            return static_cast<float>(std::pow((.8359375+18.8515625*value)/(1+18.6875*value),78.84375));
        };
        for (const auto light : {std::array<float,3>{0,0,0}, {203,203,203}, {1000,1000,1000},
                                {10000,10000,10000}, {1000,0,0}, {0,1000,0}, {0,0,1000}}) {
            const std::array<float,4> encoded={pq(light[0]),pq(light[1]),pq(light[2]),1};
            auto hdrInput=gpu.Texture(1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,encoded.data(),16);
            auto original=gpu.Texture(1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
            gpu.Dispatch(prepare.Get(),hdrInput,nullptr,normalized,1,1,0,0,0,true,&original);
            gpu.Dispatch(composite.Get(),normalized,&original,readable,1,1,0,0,0,false,nullptr,&normalized);
            const auto restored=gpu.Read(readable,4);
            for (unsigned channel=0;channel<3;++channel)
                Require(std::isfinite(restored[channel]) && std::abs(restored[channel]-encoded[channel])<.0001f,
                        "An unchanged SDR proxy must preserve HDR black, white, highlights and BT.2020 primaries");
        }
        // R10G10B10A2 is the HDR composite/MFG output, not the 8-bit model proxy.
        auto hdr10=gpu.Texture(1,1,DXGI_FORMAT_R10G10B10A2_UNORM,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        const std::array<float,4> hdrGrey={pq(1000),pq(1000),pq(1000),1};
        auto hdrSource=gpu.Texture(1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,hdrGrey.data(),16);
        auto raw=gpu.Texture(1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        gpu.Dispatch(prepare.Get(),hdrSource,nullptr,normalized,1,1,0,0,0,true,&raw);
        gpu.Dispatch(composite.Get(),normalized,&raw,hdr10,1,1,0,0,0,false,nullptr,&normalized);
        gpu.Dispatch(prepare.Get(),hdr10,nullptr,readable,1,1,0);
        const auto actualHdr=gpu.Read(readable,4);
        Require(std::abs(actualHdr[0]-pq(1000))<=1.0f/1023,"HDR MFG input must retain 1000-nit PQ at 10-bit precision");
        gpu.Dispatch(prepare.Get(),normalized,nullptr,readable,1,1,0);
        const auto proxyPixel=gpu.Read(readable,4);
        std::array<float,16> enlargedProxy{};
        for (unsigned pixel=0;pixel<4;++pixel)
            std::copy(proxyPixel.begin(),proxyPixel.end(),enlargedProxy.begin()+pixel*4);
        auto enlarged=gpu.Texture(2,2,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,enlargedProxy.data(),32);
        auto hdrScaled=gpu.Texture(2,2,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        gpu.Dispatch(composite.Get(),enlarged,&raw,hdrScaled,2,2,0,0,0,false,nullptr,&normalized);
        const auto scaledHdr=gpu.Read(hdrScaled,4);
        for (unsigned pixel=0;pixel<4;++pixel)
            Require(std::abs(scaledHdr[pixel*4]-pq(1000))<.0001f,"HDR reconstruction must preserve highlights after upscaling");
        for (unsigned pixel=0;pixel<4;++pixel)
            for (unsigned channel=0;channel<3;++channel) enlargedProxy[pixel*4+channel]+=.01f;
        auto edited=gpu.Texture(2,2,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_SHADER_RESOURCE,enlargedProxy.data(),32);
        gpu.Dispatch(composite.Get(),edited,&raw,hdrScaled,2,2,0,0,0,false,nullptr,&normalized);
        const auto editedHdr=gpu.Read(hdrScaled,4);
        Require(editedHdr[0]>pq(1000) && editedHdr[0]<1 && std::isfinite(editedHdr[0]),
                "Model edits must be transferred to HDR as a finite linear-light residual");
        // After-upscaling NR: 8x8 source motion resampled to 16x16, scaled by the 2x ratio.
        std::vector<float> lowMotion(8*8*2);
        for (UINT i=0;i<8*8;++i) { lowMotion[i*2]=static_cast<float>(i%8); lowMotion[i*2+1]=-static_cast<float>(i/8); }
        auto lowRes=gpu.Texture(8,8,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_SHADER_RESOURCE,lowMotion.data(),8*8);
        auto highRes=gpu.Texture(16,16,DXGI_FORMAT_R32G32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        auto scale=gpu.Compile(neural::ScaleMotionShader);
        gpu.Dispatch(scale.Get(),lowRes,nullptr,highRes,8,8,0,16,16); // constants: source 8x8, target 16x16
        pixels=gpu.Read(highRes,2);
        for (UINT y=0;y<8;++y) for (UINT x=0;x<8;++x) {
            const auto index=(static_cast<size_t>(y)*16+x)*2;
            Require(pixels[index]==2.0f*static_cast<float>(x/2) && pixels[index+1]==-2.0f*static_cast<float>(y/2),"Upscaled motion must be resampled and scaled to output pixels");
        }
        std::cout << "GPU motion and crop tests passed (D3D11 WARP)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
