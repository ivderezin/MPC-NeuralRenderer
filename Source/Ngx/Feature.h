// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ngx {
inline void Check(NVSDK_NGX_Result result, const char* name) {
    if (result != NVSDK_NGX_Result_Success) {
        std::ostringstream message;
        message << name << ": NGX 0x" << std::hex << static_cast<unsigned>(result);
        throw std::runtime_error(message.str());
    }
}
inline void CheckHr(HRESULT result, const char* name) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << name << ": HRESULT 0x" << std::hex << static_cast<unsigned long>(result);
        throw std::runtime_error(message.str());
    }
}
inline std::wstring Message(const std::exception& error) {
    const std::string message = error.what();
    return {message.begin(), message.end()};
}
class Feature {
protected:
    NVSDK_NGX_Handle* handle_ = nullptr;
    NVSDK_NGX_Parameter* params_ = nullptr;
public:
    Feature() = default;
    Feature(const Feature&) = delete;
    Feature& operator=(const Feature&) = delete;
    ~Feature() { Reset(); }
    bool Active() const { return handle_ != nullptr; }
    void Allocate() { Check(NVSDK_NGX_D3D12_AllocateParameters(&params_), "Allocate feature parameters"); }
    void Reset() {
        if (handle_) NVSDK_NGX_D3D12_ReleaseFeature(handle_);
        if (params_) NVSDK_NGX_D3D12_DestroyParameters(params_);
        handle_ = nullptr; params_ = nullptr;
    }
};
inline void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from; barrier.Transition.StateAfter = to;
    list->ResourceBarrier(1, &barrier);
}
}
