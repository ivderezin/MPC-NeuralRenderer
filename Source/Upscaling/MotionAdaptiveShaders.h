// SPDX-License-Identifier: MIT
// Adapted from Echo-Storm/ls-addon-manager, sr_engine.cpp (CatmullRom / lean pass).
// Revision 8d3eac351d42480326290b0f8965127a630b44f2.
// Copyright (c) 2026 Echo-Storm
// Copyright (c) 2026 andreiday
// See ../NeuralRendering/EchoStorm-LICENSE.txt for the complete MIT notice.
#pragma once
namespace upscaling {
inline constexpr char MotionAdaptiveShader[] = R"(
Texture2D<float4> tUp : register(t0);
Texture2D<float4> tIn : register(t1);
Texture2D<float> tDistrust : register(t2);
RWTexture2D<float4> uOut : register(u0);
SamplerState sLinear : register(s0);
static uint2 inSize;
float4 CatmullRom(float2 uv) {   // 16 loads, the input's size
    const float2 pos = uv * float2(inSize) - 0.5, base = floor(pos), f = pos - base;
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f), w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    const float wx[4] = { w0.x, w1.x, w2.x, w3.x }, wy[4] = { w0.y, w1.y, w2.y, w3.y };
    const int2 last = int2(inSize) - 1;
    float4 sum = 0, lo = 1e30, hi = -1e30;
    [unroll] for (int j = 0; j < 4; ++j) [unroll] for (int i = 0; i < 4; ++i) {
        const float4 t = tIn.Load(int3(clamp(int2(base) + int2(i - 1, j - 1), int2(0, 0), last), 0));
        sum += t * (wx[i] * wy[j]);
        if (i >= 1 && i <= 2 && j >= 1 && j <= 2) { lo = min(lo, t); hi = max(hi, t); }   // the four texels the sample lies among
    }
    // HDR (mode bit 1): the negative lobes ring around a bright glint, and in light that is a speck: the result stays within the four nearest texels' range (as EASU's does)
    return clamp(sum, lo, hi);
}

[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint w,h; uOut.GetDimensions(w,h);
    if (id.x>=w || id.y>=h) return;
    tIn.GetDimensions(inSize.x,inSize.y);
    const float2 uv=(float2(id.xy)+0.5)/float2(w,h);
    const float4 up=tUp.Load(int3(id.xy,0));
    const float d=saturate(tDistrust.SampleLevel(sLinear,uv,0));
    // Frames have no engine jitter. At unreliable/fast pixels trust this frame's
    // spatial reconstruction, including for DLSS models that ignore bias masks.
    uOut[id.xy]=d<=0.001 ? up : float4(lerp(up.rgb,max(CatmullRom(uv).rgb,0.0),d),up.a);
}
)";
}
