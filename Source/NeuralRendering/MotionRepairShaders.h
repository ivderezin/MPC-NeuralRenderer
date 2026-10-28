// SPDX-License-Identifier: MIT
// Adapted from Echo-Storm/ls-addon-manager, flow_fix.cpp
// Revision 8d3eac351d42480326290b0f8965127a630b44f2.
// Copyright (c) 2026 Echo-Storm
// Copyright (c) 2026 andreiday
// See EchoStorm-LICENSE.txt for the complete MIT notice.
// D3D11 adaptation: SDR proxy inputs, full-resolution output, fixed repair rules.
#pragma once
namespace neural {
inline constexpr char MotionRepairShader[] = R"(
Texture2D<float4> tA : register(t0);
Texture2D<float4> tC : register(t1);
Texture2D<float2> tMotion : register(t2);
Texture2D<float> tDistrust : register(t3);
Texture2D<float> tLumaA : register(t4);
Texture2D<float> tLumaC : register(t5);
Texture2D<float4> tModes : register(t6);
Texture2D<float4> tCells : register(t7);
RWTexture2D<float> uLumaA : register(u0);
RWTexture2D<float> uLumaC : register(u1);
RWTexture2D<float4> uModes : register(u2);
RWTexture2D<float4> uCells : register(u3);
RWTexture2D<float2> uOut : register(u4);
SamplerState sLin : register(s0);
RWTexture2D<float> uConfidence : register(u5);
cbuffer C : register(b0) { uint2 fsize; uint2 csize; uint2 tiles; uint tile; uint reach; uint encoding; float fastMotion; uint cs; uint S; uint2 fullsize; uint fixOn; uint rules; };

float3 View(float3 c) { return c; }
float Luma255(float3 c) { return dot(View(c), float3(0.299, 0.587, 0.114)) * 255.0; }

// 1. brightness at half size (the frames' own values in their SDR view, 0..255)
[numthreads(8, 8, 1)] void Luma(uint3 id : SV_DispatchThreadID) {
    if (id.x >= csize.x || id.y >= csize.y) return;
    const float2 uv = (float2(id.xy) + 0.5) / float2(csize);
    if (cs <= 2u) {
        uLumaA[id.xy] = Luma255(tA.SampleLevel(sLin, uv, 0).rgb);
        uLumaC[id.xy] = Luma255(tC.SampleLevel(sLin, uv, 0).rgb);
    } else {   // four taps, each the mean of 2 x 2 pixels: the cell's 4 x 4
        const float2 q = 0.5 / float2(fsize) * float(cs) * 0.5;
        float a = 0, c = 0;
        [unroll] for (int k = 0; k < 4; ++k) {
            const float2 o = float2((k & 1) ? q.x : -q.x, (k & 2) ? q.y : -q.y);
            a += Luma255(tA.SampleLevel(sLin, uv + o, 0).rgb); c += Luma255(tC.SampleLevel(sLin, uv + o, 0).rgb);
        }
        uLumaA[id.xy] = a * 0.25; uLumaC[id.xy] = c * 0.25;
    }
}

// 2. per tile: the dominant movement of the trusted, moving pixels in the window of (2 * reach + 1)^2 tiles around it (every second pixel): the 2 px bin with the most votes
// in its 3 x 3 neighbourhood of bins, then the mean of the vectors within 3 px of that bin's middle; with the share of the window's votes that agree
groupshared uint gHist[4096];
groupshared uint gBest, gTotal, gAll;   // gAll: every trusted pixel that votes, moving or not
groupshared int gSumX, gSumY, gCount;
// The upstream host estimates 4K flow at half size. Here vectors stay in source
// pixels, so widen the voting bins instead of discarding fast 4K camera motion.
float VoteScale() { return max(1.0, float(fsize.x) / 1920.0); }
bool Vote(int2 p, out float2 v) {
    v = tMotion.Load(int3(p, 0));
    return tDistrust.Load(int3(p, 0)) < 0.3 && dot(v, v) > 1.0 && all(abs(v) < 63.9 * VoteScale());
}
[numthreads(256, 1, 1)] void TileMode(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
    const int2 t = int2(gid.xy);
    const int x0 = max(0, (t.x - int(reach)) * int(tile)), x1 = min(int(fsize.x), (t.x + int(reach) + 1) * int(tile));
    const int y0 = max(0, (t.y - int(reach)) * int(tile)), y1 = min(int(fsize.y), (t.y + int(reach) + 1) * int(tile));
    const int st = int(cs);   // every cs-th pixel votes (cs: 2 or 4)
    const int nx = max(1, (x1 - x0 + st - 1) / st), ny = max(1, (y1 - y0 + st - 1) / st);
    for (uint b = gi; b < 4096u; b += 256u) gHist[b] = 0u;
    if (gi == 0) { gBest = 0u; gTotal = 0u; gAll = 0u; gSumX = 0; gSumY = 0; gCount = 0; }
    GroupMemoryBarrierWithGroupSync();
    for (int idx = int(gi); idx < nx * ny; idx += 256) {
        const int2 p = int2(x0 + st * (idx % nx), y0 + st * (idx / nx));
        float2 v;
        if (tDistrust.Load(int3(p, 0)) < 0.3) { uint o; InterlockedAdd(gAll, 1u, o); }
        if (Vote(p, v)) {
            const int2 bin = clamp(int2(floor(v * (0.5 / VoteScale()))) + 32, int2(0, 0), int2(63, 63));
            uint old; InterlockedAdd(gHist[bin.y * 64 + bin.x], 1u, old);
            InterlockedAdd(gTotal, 1u, old);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint b = gi; b < 4096u; b += 256u) {
        const int2 c = int2(b & 63u, b >> 6);
        uint score = 0;
        [unroll] for (int j = -1; j <= 1; ++j) [unroll] for (int i = -1; i <= 1; ++i) {
            const int2 n = clamp(c + int2(i, j), int2(0, 0), int2(63, 63));
            score += gHist[n.y * 64 + n.x];
        }
        uint old; InterlockedMax(gBest, (score << 12) | b, old);
    }
    GroupMemoryBarrierWithGroupSync();
    const uint peak = gBest & 4095u;
    const float2 centre = ((float2(int2(peak & 63u, peak >> 6)) - 32.0) * 2.0 + 1.0) * VoteScale();
    for (int idx2 = int(gi); idx2 < nx * ny; idx2 += 256) {
        const int2 p = int2(x0 + st * (idx2 % nx), y0 + st * (idx2 / nx));
        float2 v;
        if (Vote(p, v) && all(abs(v - centre) <= 3.0 * VoteScale())) {
            int old;
            InterlockedAdd(gSumX, int(round(v.x * 16.0)), old); InterlockedAdd(gSumY, int(round(v.y * 16.0)), old); InterlockedAdd(gCount, 1, old);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) {
        float4 r = float4(0, 0, 0, 0);
        if (gTotal >= 60u && gCount > 0) r = float4(float(gSumX) / (16.0 * gCount), float(gSumY) / (16.0 * gCount), float(gCount) / float(gTotal), float(gCount) / float(max(gAll, 1u)));
        uModes[t] = r;
    }
}

// 3. per half-size cell: take the tile's dominant vector, or keep the cell's own
float PatchCost(int2 c, float2 v) {   // the 3 x 3 cells around c: how unlike their place in the earlier frame (moved by v, in the frames' pixels) they are
    float s = 0;
    [unroll] for (int j = -1; j <= 1; ++j) [unroll] for (int i = -1; i <= 1; ++i) {
        const int2 n = clamp(c + int2(i, j), int2(0, 0), int2(csize) - 1);
        s += abs(tLumaC.Load(int3(n, 0)) - tLumaA.SampleLevel(sLin, (float2(n) + 0.5 + v / float(cs)) / float2(csize), 0));
    }
    return s * (1.0 / 9.0);
}
float Contrast(int2 c) {
    float s = 0, a[25];
    [unroll] for (int j = -2; j <= 2; ++j) [unroll] for (int i = -2; i <= 2; ++i) { const float l = tLumaC.Load(int3(clamp(c + int2(i, j), int2(0, 0), int2(csize) - 1), 0)); a[(j + 2) * 5 + (i + 2)] = l; s += l; }
    const float mean = s * (1.0 / 25.0);
    float d = 0;
    [unroll] for (int k = 0; k < 25; ++k) d += abs(a[k] - mean);
    return d * (1.0 / 25.0);
}
[numthreads(8, 8, 1)] void Decide(uint3 id : SV_DispatchThreadID) {
    if (id.x >= csize.x || id.y >= csize.y) return;
    const int2 c = int2(id.xy);
    const int2 px = min(c * int(cs), int2(fsize) - 1);
    const float2 own = tMotion.Load(int3(px, 0));
    const float4 mode = tModes.Load(int3(min(px / int(tile), int2(tiles) - 1), 0));
    float2 outv = own; float take = 0.0;
    if (mode.w > 0.25 && mode.z > 0.3 && length(mode.xy - own) > 1.0) {
        const float2 L = mode.xy;
        const float ownc = PatchCost(c, own), lc = PatchCost(c, L);
        bool amb = false;
        if (ownc < 8.0 && lc <= ownc * 1.1 + 1.5 && length(own) <= length(L) + 2.0) {
            const float contrast = Contrast(c);
            amb = contrast < 2.5 && (rules & 1u) != 0u && length(own) < 1.5;   // a plain block reads as still (it matches anywhere); one the estimate already moved is left
            if (!amb && (rules & 2u) != 0u && contrast >= 2.5) {
                // repeating lines: the patch matches about as well one or two periods on, along or across (d), yet NOT a little way on (within a period the cost rises: s). Grain or a long edge
                // (which matches at every shift along it) fails the second test, so it is left alone.
                const float thr = ownc + 0.15 * contrast + 0.5, rise = ownc + 0.4 * contrast + 2.0;
                [unroll] for (int ax = 0; ax < 2; ++ax) {
                    const float2 dir = ax == 0 ? float2(1, 0) : float2(0, 1);
                    float far = 1e9, near = 0.0;
                    [unroll] for (int k = 0; k < 2; ++k) {
                        const float d = 8.0 + 6.0 * k;
                        far = min(far, min(PatchCost(c, own + dir * d), PatchCost(c, own - dir * d)));
                    }
                    [unroll] for (int k = 0; k < 3; ++k) {
                        const float sh = 1.5 + 1.5 * k;
                        near = max(near, max(PatchCost(c, own + dir * sh), PatchCost(c, own - dir * sh)));
                    }
                    amb = amb || (far <= thr && near > rise);
                }
            }
        }
        if (amb || (lc + 8.0 < ownc && (rules & 4u) != 0u)) { outv = L; take = 1.0; }
    }
    uCells[c] = float4(outv, take, 0.0);
}

// 4. the result at the frames' size
[numthreads(8, 8, 1)] void Apply(uint3 id : SV_DispatchThreadID) {
    if (id.x >= fullsize.x || id.y >= fullsize.y) return;
    const int2 a = min(int2(id.xy) / int(S), int2(fsize) - 1);   // the estimate's pixel (S: how much larger the frames are)
    float2 v = tMotion.Load(int3(a, 0));
    if (fixOn != 0u) { const float4 cell = tCells.Load(int3(min(a / int(cs), int2(csize) - 1), 0)); if (cell.z > 0.5) v = cell.xy; }
    uOut[id.xy] = v * float(S);
    // Keep the matching confidence used for voting separate from the SR mask:
    // fast camera motion should reject temporal SR history, not camera votes.
    float distrust = tDistrust.Load(int3(a, 0));
    if (fastMotion > 0.0) distrust = max(distrust, saturate((length(v) - fastMotion) / fastMotion));
    uConfidence[id.xy] = distrust;
}
)";
}
