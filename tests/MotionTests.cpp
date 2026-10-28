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
int main() {
    try {
        Gpu gpu;
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
