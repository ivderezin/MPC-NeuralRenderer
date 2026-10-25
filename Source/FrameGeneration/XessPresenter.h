// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <memory>
#include <string>
namespace framegen {
class XessPresenter {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::wstring status_;

  public:
    XessPresenter();
    ~XessPresenter();
    void Reset();
    bool Active() const { return impl_ != nullptr; }
    const std::wstring& Status() const { return status_; }
    HRESULT Create(ID3D11Device* device, ID3D11DeviceContext* context, IDXGIFactory2* factory, HWND window,
                   DXGI_SWAP_CHAIN_DESC1 description, unsigned multiplier, double refreshHz, float frameTimeMs,
                   const std::filesystem::path& directory, IDXGISwapChain1** swapchain);
    HRESULT BackBuffer(ID3D11Texture2D** texture) const;
    HRESULT Resize(unsigned width, unsigned height);
    HRESULT Present(unsigned syncInterval, bool running, unsigned long long token, float frameTimeMs);
    void ClearHistory();
};
}
