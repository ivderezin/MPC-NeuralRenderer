// SPDX-License-Identifier: GPL-3.0-or-later
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../Source/NeuralRendering/MotionShaders.h"
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
    void Dispatch(ID3D11ComputeShader* shader, Image& source, Image* previous, Image& target, UINT width, UINT height, UINT reset, UINT x=0, UINT y=0) {
        UINT data[] = {width,height,x,y,reset,0,0,0};
        context->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);
        ID3D11Buffer* buffers[] = {constants.Get()};
        ID3D11ShaderResourceView* views[] = {source.srv.Get(), previous ? previous->srv.Get() : nullptr};
        ID3D11UnorderedAccessView* uavs[] = {target.uav.Get()};
        context->CSSetConstantBuffers(0,1,buffers);
        context->CSSetShaderResources(0,2,views);
        context->CSSetUnorderedAccessViews(0,1,uavs,nullptr);
        context->CSSetShader(shader,nullptr,0);
        context->Dispatch((width+7)/8,(height+7)/8,1);
        views[0]=views[1]=nullptr; uavs[0]=nullptr;
        context->CSSetShaderResources(0,2,views);
        context->CSSetUnorderedAccessViews(0,1,uavs,nullptr);
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
        gpu.Dispatch(shader.Get(),current,&previous,output,size,size,1);
        pixels=gpu.Read(output,2);
        for (float value:pixels) Require(value==0,"Reset must produce zero motion");
        gpu.Dispatch(shader.Get(),previous,&previous,output,size,size,0);
        pixels=gpu.Read(output,2);
        for (float value:pixels) Require(value==0,"Static frame must produce zero motion");
        auto crop=gpu.Texture(13,11,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
        auto prepare=gpu.Compile(neural::PrepareShader);
        gpu.Dispatch(prepare.Get(),previous,nullptr,crop,13,11,0,5,7);
        pixels=gpu.Read(crop,4);
        for (UINT y=0;y<11;++y) for (UINT x=0;x<13;++x)
            Require(pixels[(y*13+x)*4]==oldPixels[((y+7)*size+x+5)*4],"Crop origin / odd extent must preserve video pixels");
        std::cout << "GPU motion and crop tests passed (D3D11 WARP)\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
