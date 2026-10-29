// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <memory>
#include <string>
#include "NeuralSettings.h"

namespace neural {
class NeuralPass {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Settings settings_;
    std::wstring status_ = L"Off";
    bool failed_ = false;
public:
    NeuralPass();
    ~NeuralPass();
    void Configure(Settings settings);
    void ResetDevice();
    void ClearHistory();
    void Bypass(const wchar_t* reason) { ClearHistory(); status_ = reason; }
    bool Enabled() const { return (settings_.enabled || (settings_.upscaler != processing::Upscaler::Off) || settings_.ManualGeneration()) && !failed_; }
    unsigned GeneratedCount() const;
    unsigned long long OutputSerial() const;
    long long OutputDuration() const;
    // Frame of a completed batch: generated frames first, then the real frame; null once the batch is overwritten.
    ID3D11Texture2D* PresentationFrame(unsigned long long batch, unsigned index) const;
    const std::wstring& Status() const { return status_; }
    // Returned texture is owned by this pass and remains valid until reset or resize.
    ID3D11Texture2D* Process(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11Texture2D* frame, const RECT& videoRect, long long timestamp, bool hdr, SIZE targetSize, SIZE monitorSize,
        double refreshHz, float frameTimeMs, bool diagnostics = false);
};
}
