// SPDX-License-Identifier: MIT
// Adapted from Echo-Storm/ls-addon-manager, flow_estimator.cpp
// Revision 8d3eac351d42480326290b0f8965127a630b44f2.
// Copyright (c) 2026 Echo-Storm
// Copyright (c) 2026 andreiday
// See EchoStorm-LICENSE.txt for the complete MIT notice.
// D3D11 adaptation: input is already an SDR proxy; statistics/reuse/shape variants are not enabled.
#pragma once
namespace neural::echo {
inline constexpr char Common[] = R"(
SamplerState sLinear : register(s0);
cbuffer C : register(b0) { uint2 size; uint2 grid; uint2 coarse; uint radius; uint flags; float lambda; float bias; float stability; float strayCap; float fastMotion; float meanWeight; float gradWeight; uint pad2; };
)";
inline constexpr char Luma[] = R"(Texture2D<float4> tFrame : register(t0);
RWTexture2D<float> uLuma : register(u0);
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
    if (id.x < size.x && id.y < size.y)
        uLuma[id.xy] = dot(tFrame.Load(int3(id.xy,0)).rgb, float3(.299,.587,.114));
}
)";
inline constexpr char Down[] = R"(
Texture2D<float> tSrc : register(t0);
RWTexture2D<float> uDst : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    const int2 p = int2(id.xy) * 2, last = int2(grid) - 1;
    uDst[id.xy] = 0.25 * (tSrc.Load(int3(min(p, last), 0)) + tSrc.Load(int3(min(p + int2(1, 0), last), 0)) +
                          tSrc.Load(int3(min(p + int2(0, 1), last), 0)) + tSrc.Load(int3(min(p + int2(1, 1), last), 0)));
}
)";
inline constexpr char Global[] = R"(
Texture2D<float> tCur : register(t0);
Texture2D<float> tPrev : register(t1);
RWTexture2D<float> uCost : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int n = 2 * int(radius) + 1;
    if (int(id.x) >= n || int(id.y) >= n) return;
    const int2 v = int2(id.xy) - int(radius), lo = max(-v, int2(0, 0)), hi = min(int2(size), int2(size) - v);
    float s = 0; uint count = 0;
    [loop] for (int y = lo.y; y < hi.y; ++y) {
        [loop] for (int x = lo.x + ((lo.x + y) & 1); x < hi.x; x += 2) { s += abs(tCur.Load(int3(x, y, 0)) - tPrev.Load(int3(x + v.x, y + v.y, 0))); ++count; }
    }
    uCost[id.xy] = count * 8 >= size.x * size.y ? s / count : 1e3;   // at least a quarter of the picture shared
}
)";
inline constexpr char Search[] = R"(
Texture2D<float> tCur : register(t0);
Texture2D<float> tPrev : register(t1);
Texture2D<float2> tCoarse : register(t2);
Texture2D<float> tGlobal : register(t3);
RWTexture2D<float2> uGrid : register(u0);
static const float kExact = 0.0005, kShallow = 0.002;   // an exact match (brightness 0..1, per pixel); the least rise either side to refine on
static float cw[64];
static int2 origin;
// The cost of straying from the coarser size's guess, capped at strayCap pixels (of this size): it settles near-ties (flat areas follow their
// surroundings), but never holds a block to a guess that is plainly wrong for it. Uncapped, a character the camera follows in a fast turn (the
// guess: the background sweeping past, 170 px) paid more for "not moving" than its dark, faint texture could win back, and took random vectors.
float Stray(float2 d) { return lambda * min(length(d), strayCap); }
// meanWeight below 1: the block's shape is compared with its average brightness taken out (a light change, a flash, a fade or a shadow
// passing over no longer pulls the match toward whatever is equally bright), and the brightness difference counts only meanWeight as much.
// At 1, the plain difference, exactly as before.
// This costs seconds to compile (the unrolled arrays, several times over), so it is a variant of the shader (SHAPE_COST): the default is the plain
// difference, compiled in a few milliseconds, and the variant is made in the background only when the "Motion by shape" test is switched on.
#ifndef SHAPE_COST
float Sad(int2 v) {   // all 64 pixels of the 8x8 (the final answer and its fraction of a pixel)
    const int2 last = int2(size) - 1;
    float s = 0;
    [loop] for (int j = 0; j < 8; ++j) {
        [unroll] for (int i = 0; i < 8; ++i) s += abs(cw[j * 8 + i] - tPrev.Load(int3(clamp(origin + int2(i, j) + v, int2(0, 0), last), 0)));
    }
    return s * (1.0 / 64.0);
}
float SadHalf(int2 v) {   // half of them, as a checkerboard (the search: half the reads, with no stripe the pattern could hide in)
    const int2 last = int2(size) - 1;
    float s = 0;
    [loop] for (int j = 0; j < 8; ++j) {
        [unroll] for (int i = (j & 1); i < 8; i += 2) s += abs(cw[j * 8 + i] - tPrev.Load(int3(clamp(origin + int2(i, j) + v, int2(0, 0), last), 0)));
    }
    return s * (1.0 / 32.0);
}
#else
static float cMean, cMeanHalf;   // this block's average: all 64 pixels, and the checkerboard's 32
float Sad(int2 v) {   // all 64 pixels of the 8x8 (the final answer and its fraction of a pixel)
    const int2 last = int2(size) - 1;
    float p[64], mean = 0;
    [unroll] for (int k = 0; k < 64; ++k) { p[k] = tPrev.Load(int3(clamp(origin + int2(k & 7, k >> 3) + v, int2(0, 0), last), 0)); mean += p[k]; }
    mean *= 1.0 / 64.0;
    const float shift = meanWeight < 1.0 ? cMean - mean : 0.0;
    float s = 0;
    [unroll] for (int k = 0; k < 64; ++k) s += abs(cw[k] - p[k] - shift);
    s = s * (1.0 / 64.0) + (meanWeight < 1.0 ? meanWeight * abs(shift) : 0.0);
    if (gradWeight > 0.0) {   // the edges: each pixel's step to its right and lower neighbour, this frame's against the frame before's
        float g = 0;
        [unroll] for (int k = 0; k < 64; ++k) {
            if ((k & 7) < 7) g += abs((cw[k + 1] - cw[k]) - (p[k + 1] - p[k]));
            if (k < 56) g += abs((cw[k + 8] - cw[k]) - (p[k + 8] - p[k]));
        }
        s = lerp(s, g * (1.0 / 112.0), gradWeight);
    }
    return s;
}
float SadHalf(int2 v) {   // half of them, as a checkerboard (the search: half the reads, with no stripe the pattern could hide in)
    const int2 last = int2(size) - 1;
    float p[32], mean = 0;
    [unroll] for (int k = 0; k < 32; ++k) { const int j = k >> 2, i = (k & 3) * 2 + (j & 1); p[k] = tPrev.Load(int3(clamp(origin + int2(i, j) + v, int2(0, 0), last), 0)); mean += p[k]; }
    mean *= 1.0 / 32.0;
    const float shift = meanWeight < 1.0 ? cMeanHalf - mean : 0.0;
    float s = 0;
    [unroll] for (int k = 0; k < 32; ++k) { const int j = k >> 2, i = (k & 3) * 2 + (j & 1); s += abs(cw[j * 8 + i] - p[k] - shift); }
    s = s * (1.0 / 32.0) + (meanWeight < 1.0 ? meanWeight * abs(shift) : 0.0);
    if (gradWeight > 0.0) {   // the edges, from the checkerboard's own pixels: the step two to the right and two down
        float g = 0;
        [unroll] for (int k = 0; k < 32; ++k) {
            const int j = k >> 2, i = (k & 3) * 2 + (j & 1), c = j * 8 + i;
            if ((k & 3) < 3) g += abs((cw[c + 2] - cw[c]) - (p[k + 1] - p[k]));
            if (j < 6) g += abs((cw[c + 16] - cw[c]) - (p[k + 8] - p[k]));
        }
        s = lerp(s, g * (1.0 / 48.0), gradWeight);
    }
    return s;
}
#endif
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= grid.x || id.y >= grid.y) return;
    const int2 last = int2(size) - 1;
    origin = int2(id.xy) * 4 - 2;
    [unroll] for (int j = 0; j < 8; ++j) [unroll] for (int i = 0; i < 8; ++i) cw[j * 8 + i] = tCur.Load(int3(clamp(origin + int2(i, j), int2(0, 0), last), 0));
#ifdef SHAPE_COST
    cMean = 0; cMeanHalf = 0;
    [unroll] for (int k = 0; k < 64; ++k) { cMean += cw[k]; if ((((k >> 3) + k) & 1) == 0) cMeanHalf += cw[k]; }
    cMean *= 1.0 / 64.0; cMeanHalf *= 1.0 / 32.0;
#endif

    float2 predicted = float2(0, 0);
    int2 best = int2(0, 0);
    float bestCost = SadHalf(best);
    if (flags & 1) {   // the coarser size's answers for this block and its neighbours, doubled
        const int2 c = min(int2((id.xy * 2 + 1) >> 2), int2(coarse) - 1);
        predicted = tCoarse.Load(int3(c, 0)) * 2.0;
        bestCost += Stray(predicted);   // "not moving" strays from the guess too
        const int2 seeds[5] = { int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
        [unroll] for (int s = 0; s < 5; ++s) {
            const int2 v = int2(round(tCoarse.Load(int3(clamp(c + seeds[s], int2(0, 0), int2(coarse) - 1), 0)) * 2.0));
            const float cost = SadHalf(v) + Stray(float2(v) - predicted);
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
    } else if (flags & 4) {   // the whole picture's shift, the guess (a block moving with the camera needs no search of its own)
        const int gr = int(coarse.x), n = 2 * gr + 1;
        // "not moving" unless a shift is clearly better (a still picture of a repeating pattern matches as well at many shifts)
        const float still = tGlobal.Load(int3(gr, gr, 0));
        float g = still * 0.9 - 0.0005; int2 shift = int2(0, 0);
        [loop] for (int j = 0; j < n; ++j) [loop] for (int i = 0; i < n; ++i) { const float c = tGlobal.Load(int3(i, j, 0)); if (c < g) { g = c; shift = int2(i, j) - gr; } }
        if (any(shift != int2(0, 0))) {
            predicted = float2(shift);
            bestCost += Stray(predicted);
            const float cost = SadHalf(shift);
            if (cost < bestCost) { bestCost = cost; best = shift; }
        }
    }
    const int2 center = best;
    const int r = int(radius);
    const float seedCost = bestCost - Stray(float2(best) - predicted);
    if (seedCost > kExact) {   // a guess that already matches exactly (a still area, mostly) needs no search around it
        [loop] for (int dy = -r; dy <= r; ++dy) {
            [loop] for (int dx = -r; dx <= r; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int2 v = center + int2(dx, dy);
                const float cost = SadHalf(v) + Stray(float2(v) - predicted);
                if (cost < bestCost) { bestCost = cost; best = v; }
            }
        }
    }
    float2 result = float2(best);
    if (flags & 2) {
        // A fraction of a pixel: the costs of summed differences rise in a V around the true position, so two lines of equal slope through
        // the costs either side (not a parabola, which pulls toward whole pixels). Not where the match is already exact (a still picture
        // must give exactly zero: a small wrong offset would slide DLSS's history every frame and blur text), nor where the V is too shallow.
        const float c0 = Sad(best), xm = Sad(best - int2(1, 0)), xp = Sad(best + int2(1, 0)), ym = Sad(best - int2(0, 1)), yp = Sad(best + int2(0, 1));
        const float sx = max(xm, xp) - c0, sy = max(ym, yp) - c0;
        if (c0 > kExact && sx > kShallow) result.x += clamp(0.5 * (xm - xp) / sx, -0.5, 0.5);
        if (c0 > kExact && sy > kShallow) result.y += clamp(0.5 * (ym - yp) / sy, -0.5, 0.5);
    }
    uGrid[id.xy] = result;
}
)";
inline constexpr char Median[] = R"(
Texture2D<float2> tGrid : register(t0);
RWTexture2D<float2> uOut : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    float2 v[9];
    [unroll] for (int k = 0; k < 9; ++k) v[k] = tGrid.Load(int3(clamp(int2(id.xy) + int2(k % 3 - 1, k / 3 - 1), int2(0, 0), int2(size) - 1), 0));
    float bestSum = 1e30; float2 best = v[4];
    [unroll] for (int a = 0; a < 9; ++a) {
        float s = 0;
        [unroll] for (int b = 0; b < 9; ++b) s += abs(v[a].x - v[b].x) + abs(v[a].y - v[b].y);
        if (s < bestSum) { bestSum = s; best = v[a]; }
    }
    uOut[id.xy] = best;
}
)";
inline constexpr char Pixel[] = R"(
Texture2D<float> tCur : register(t0);
Texture2D<float> tPrev : register(t1);
Texture2D<float2> tGrid : register(t2);
RWTexture2D<float2> uMotion : register(u0);
RWTexture2D<float> uDistrust : register(u1);
static const float kTrusted = 0.03, kUntrusted = 0.10;
float Pixel(uint2 id) {
    if (!(flags & 1)) { uMotion[id.xy] = float2(0, 0); uDistrust[id.xy] = 0; return 0; }
    const int2 p = int2(id.xy), last = int2(size) - 1, gl = int2(grid) - 1;
    const int2 cell = min(p >> 3, gl);
    const int2 q = int2((p.x & 7) < 4 ? -1 : 1, (p.y & 7) < 4 ? -1 : 1);
    float2 cand[5];
    cand[0] = float2(0, 0);
    cand[1] = tGrid.Load(int3(cell, 0)) * 2.0;
    cand[2] = tGrid.Load(int3(clamp(cell + int2(q.x, 0), int2(0, 0), gl), 0)) * 2.0;
    cand[3] = tGrid.Load(int3(clamp(cell + int2(0, q.y), int2(0, 0), gl), 0)) * 2.0;
    cand[4] = tGrid.Load(int3(clamp(cell + q, int2(0, 0), gl), 0)) * 2.0;
    float c[9];
    [unroll] for (int k = 0; k < 9; ++k) c[k] = tCur.Load(int3(clamp(p + int2(k % 3 - 1, k / 3 - 1), int2(0, 0), last), 0));
    const float2 inv = 1.0 / float2(size);
    float bestCost = 1e30; float2 best = cand[0]; float bestPrev = 0;
    [unroll] for (int n = 0; n < 5; ++n) {
        // a vector already tried (neighbours moving alike, which is most of a moving picture; or the own block not moving) is not tried again
        if (n == 1 && all(abs(cand[1]) < 0.05)) continue;
        if (n >= 2 && (all(abs(cand[n] - cand[1]) < 0.05) || all(abs(cand[n] - cand[max(n - 1, 1)]) < 0.05) || all(abs(cand[n]) < 0.05))) continue;
        float s = n <= 1 ? 0.0 : bias, sp = 0;
        [unroll] for (int k = 0; k < 9; ++k) {
            const float prev = tPrev.SampleLevel(sLinear, (float2(p + int2(k % 3 - 1, k / 3 - 1)) + 0.5 + cand[n]) * inv, 0);
            s += abs(c[k] - prev) * (1.0 / 9.0); sp += prev;
        }
        if (s < bestCost) { bestCost = s; best = cand[n]; bestPrev = sp; }
    }
    uMotion[id.xy] = best;
    float cost = bestCost;
    if (stability > 0.001) {   // the 5x5 around: the 3x3 plus four bilinear taps, each the mean of a 2x2 in a corner
        float sc = 0;
        [unroll] for (int k = 0; k < 9; ++k) sc += c[k];
        float tc = 0, tp = 0;
        [unroll] for (int k = 0; k < 4; ++k) {
            const float2 at = float2(p) + 0.5 + float2(k & 1 ? 1.5 : -1.5, k & 2 ? 1.5 : -1.5);
            tc += tCur.SampleLevel(sLinear, at * inv, 0); tp += tPrev.SampleLevel(sLinear, (at + best) * inv, 0);
        }
        const float around = abs((sc + 4.0 * tc) - (bestPrev + 4.0 * tp)) * (1.0 / 25.0);
        cost = lerp(bestCost, around, stability) / (1.0 + stability);
    }
    float distrust = saturate((cost - kTrusted) / (kUntrusted - kTrusted));
    // A thin line that moves (a wire swaying, a branch): a ridge one pixel wide, brighter or darker than both its neighbours across it. Its
    // history is the line at other sub-pixel places, which only blurs it, so the upscaler leans on this frame there, whatever stability
    // says (test host: a swaying line 8.8 levels off the truth without stability, 10.2 with it, 2026-09-25)
    if (stability > 0.001 && dot(best, best) > 0.0625) {
        const float ridgeX = min(c[4] - max(c[3], c[5]), 1.0) , valleyX = min(min(c[3], c[5]) - c[4], 1.0);
        const float ridgeY = min(c[4] - max(c[1], c[7]), 1.0), valleyY = min(min(c[1], c[7]) - c[4], 1.0);
        const float thin = max(max(ridgeX, valleyX), max(ridgeY, valleyY));
        distrust = max(distrust, saturate((thin - 0.04) / 0.08));
    }
    // Fast motion (fastMotion > 0: from that many pixels, fully at twice it): the upscaler leans on this frame. The frames come without the
    // sub-pixel jitter a game gives its temporal upscaler, so history adds little detail there, and in a fast turn it mostly trails.
    if (fastMotion > 0.0) distrust = max(distrust, saturate((length(best) - fastMotion) / fastMotion));
    uDistrust[id.xy] = distrust;
    return distrust;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x < size.x && id.y < size.y) Pixel(id.xy);
}
)";
}
