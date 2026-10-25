// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Source/Processing/ColourTransfer.h"
#include "../Source/Processing/ResourceBarrier.h"
#include "../Source/Processing/RuntimeLibrary.h"
#include <dxgi1_4.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
using Microsoft::WRL::ComPtr;
using processing::CheckHr;
using processing::Transition;
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    CheckHr(
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)),
        "Create staging buffer");
    return resource;
}
float Half(uint16_t bits) {
    const unsigned exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    return std::ldexp(exponent ? 1.0f + static_cast<float>(mantissa) / 1024.0f : static_cast<float>(mantissa) / 1024.0f,
                      exponent ? static_cast<int>(exponent) - 15 : -14);
}
int main() {
    try {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;
        ComPtr<ID3D12Device> device;
        CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create factory");
        CheckHr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Get WARP adapter");
        CheckHr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
                "Create WARP D3D12 device");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 7;
        desc.Height = 3;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> source, output;
        CheckHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&source)),
                "Create source");
        CheckHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)),
                "Create output");
        auto linear = processing::ColourTransfer::LinearTexture(device.Get(), 7, 3);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}, linearFootprint{};
        UINT64 bytes = 0, linearBytes = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        auto linearDesc = linear->GetDesc();
        device->GetCopyableFootprints(&linearDesc, 0, 1, 0, &linearFootprint, nullptr, nullptr, &linearBytes);
        auto upload = Buffer(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto readback = Buffer(device.Get(), bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto linearReadback =
            Buffer(device.Get(), linearBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        std::array<unsigned char, 84> pixels{};
        const unsigned char values[] = {0, 1, 10, 11, 64, 128, 255};
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 7; ++x) {
                const auto i = (y * 7 + x) * 4;
                pixels[i] = values[x];
                pixels[i + 1] = values[(x + y) % 7];
                pixels[i + 2] = values[6 - x];
                pixels[i + 3] = 255;
            }
        void* mapped = nullptr;
        D3D12_RANGE empty{0, 0};
        CheckHr(upload->Map(0, &empty, &mapped), "Map upload");
        for (unsigned y = 0; y < 3; ++y)
            std::memcpy(static_cast<unsigned char*>(mapped) + y * footprint.Footprint.RowPitch, pixels.data() + y * 28,
                        28);
        upload->Unmap(0, nullptr);
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        CheckHr(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create queue");
        CheckHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "Create allocator");
        CheckHr(
            device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
            "Create list");
        D3D12_TEXTURE_COPY_LOCATION sourceLocation{}, uploadLocation{};
        sourceLocation.pResource = source.Get();
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        uploadLocation.pResource = upload.Get();
        uploadLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        uploadLocation.PlacedFootprint = footprint;
        list->CopyTextureRegion(&sourceLocation, 0, 0, 0, &uploadLocation, nullptr);
        Transition(list.Get(), source.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        processing::ColourTransfer transfer;
        transfer.Create(device.Get());
        Transition(list.Get(), linear.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transfer.Dispatch(list.Get(), source.Get(), linear.Get(), false);
        Transition(list.Get(), linear.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION linearLocation{}, linearReadLocation{};
        linearLocation.pResource = linear.Get();
        linearLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        linearReadLocation.pResource = linearReadback.Get();
        linearReadLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        linearReadLocation.PlacedFootprint = linearFootprint;
        list->CopyTextureRegion(&linearReadLocation, 0, 0, 0, &linearLocation, nullptr);
        Transition(list.Get(), linear.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transfer.Dispatch(list.Get(), linear.Get(), output.Get(), true);
        Transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION outputLocation{}, readLocation{};
        outputLocation.pResource = output.Get();
        outputLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        readLocation.pResource = readback.Get();
        readLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        readLocation.PlacedFootprint = footprint;
        list->CopyTextureRegion(&readLocation, 0, 0, 0, &outputLocation, nullptr);
        CheckHr(list->Close(), "Close list");
        ID3D12CommandList* commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        ComPtr<ID3D12Fence> fence;
        CheckHr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
        CheckHr(queue->Signal(fence.Get(), 1), "Signal fence");
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            throw std::runtime_error("Create event failed");
        const auto wait = fence->SetEventOnCompletion(1, event);
        const auto completed = SUCCEEDED(wait) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
        CloseHandle(event);
        if (completed != WAIT_OBJECT_0)
            throw std::runtime_error("GPU test timeout");
        D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
        CheckHr(readback->Map(0, &range, &mapped), "Read encoded pixels");
        bool valid = true;
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 28; ++x)
                valid &= std::abs(static_cast<int>(
                                      static_cast<unsigned char*>(mapped)[y * footprint.Footprint.RowPitch + x]) -
                                  pixels[y * 28 + x]) <= 1;
        readback->Unmap(0, &empty);
        if (!valid)
            throw std::runtime_error("sRGB round trip or descriptor lifetime failed");
        range.End = static_cast<SIZE_T>(linearBytes);
        CheckHr(linearReadback->Map(0, &range, &mapped), "Read linear pixels");
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 7; ++x)
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const float value = pixels[(y * 7 + x) * 4 + channel] / 255.0f;
                    const float expected = value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
                    const auto* row = reinterpret_cast<const uint16_t*>(static_cast<unsigned char*>(mapped) +
                                                                        y * linearFootprint.Footprint.RowPitch);
                    valid &= std::abs(Half(row[x * 4 + channel]) - expected) < .001f;
                }
        linearReadback->Unmap(0, &empty);
        if (!valid)
            throw std::runtime_error("sRGB input was not decoded to linear light");
        std::cout << "Colour transfer WARP tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
