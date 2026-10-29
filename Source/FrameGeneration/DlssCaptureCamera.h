// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
namespace framegen {
// Capture has no engine depth or camera pose. Keep the 2D orthographic fallback
// internally consistent; it is not reconstructed scene geometry.
struct DlssCaptureCamera {
    static constexpr float Near=.1f, Far=1000.f, Depth=.5f;
    using Matrix=std::array<std::array<float,4>,4>;
    Matrix viewToClip{},clipToView{};
    explicit DlssCaptureCamera(float aspect) {
        if (!std::isfinite(aspect) || aspect<=0) throw std::invalid_argument("Invalid DLSS FG aspect ratio");
        viewToClip[0][0]=1.f/aspect;viewToClip[1][1]=1;
        viewToClip[2][2]=1.f/(Near-Far);viewToClip[3][2]=Far/(Far-Near);viewToClip[3][3]=1;
        clipToView[0][0]=aspect;clipToView[1][1]=1;
        clipToView[2][2]=Near-Far;clipToView[3][2]=Far;clipToView[3][3]=1;
    }
};
}
