// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d11.h>
#include <memory>
#include <string>
#include "NeuralSettings.h"
namespace neural {
class EchoNr {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    EchoNr();
    ~EchoNr();
    void Reset();
    void Invalidate();
    void Submit(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* frame,
                const RECT& rect,long long timestamp,bool hdr,const Settings& settings);
    void Compose(ID3D11DeviceContext* context,ID3D11Texture2D* target,const RECT& viewport,
                 long long timestamp,bool generated,int rotation=0,bool flip=false);
    std::wstring Status() const;
};
}
