// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <dxgiformat.h>
#include "../Processing/HdrShaders.h"
namespace neural {
// RGB textures sampled by PrepareShader; HDR transfer functions are checked separately by the caller.
inline constexpr bool IsSdrInputFormat(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}
inline const std::string PrepareShader = std::string(processing::HdrFunctions) + R"(
Texture2D<float4> source : register(t0);
RWTexture2D<float4> color : register(u0);
RWTexture2D<float4> hdrOriginal : register(u1);
cbuffer Params : register(b0) { uint2 size; uint2 origin; uint reset; uint hdr; uint2 padding; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= size)) return;
    float3 rgb=source.Load(int3(id.xy+origin,0)).rgb;
    if (hdr) {
        hdrOriginal[id.xy]=float4(rgb,1);
        float3 light=to709(pqDecode(rgb))/203.0;
        rgb=srgbEncode(saturate(light/proxyGain(light)));
    }
    color[id.xy]=float4(rgb,1);
}
)";
inline constexpr char MotionShader[] = R"(
Texture2D<float4> current : register(t0);
Texture2D<float4> previous : register(t1);
RWTexture2D<float2> motion : register(u0);
cbuffer Params : register(b0) { uint2 size; uint2 origin; uint reset; uint3 padding; };
groupshared float currentPatch[9];
groupshared float candidateScores[64];
groupshared uint candidateIndices[64];
groupshared float2 displacement;
float luminance(float3 c) { return dot(c, float3(.2126,.7152,.0722)); }
float sampleLuma(Texture2D<float4> image, int2 p) {
    return luminance(image.Load(int3(clamp(p, int2(0,0), int2(size)-1),0)).rgb);
}
[numthreads(8,8,1)]
void main(uint3 group : SV_GroupID, uint index : SV_GroupIndex, uint3 id : SV_DispatchThreadID) {
    int2 center = int2(group.xy * 8 + 4);
    if (index < 9) {
        int2 offset = (int2(index % 3, index / 3) - 1) * 3;
        currentPatch[index] = sampleLuma(current, center + offset);
    }
    GroupMemoryBarrierWithGroupSync();

    float best = 1e10;
    uint bestIndex = 81;
    if (!reset) {
        for (uint candidate = index; candidate < 81; candidate += 64) {
            int2 delta = (int2(candidate % 9, candidate / 9) - 4) * 4;
            float error = 0;
            [unroll] for (uint sampleIndex = 0; sampleIndex < 9; ++sampleIndex) {
                int2 offset = (int2(sampleIndex % 3, sampleIndex / 3) - 1) * 3;
                error += abs(currentPatch[sampleIndex] - sampleLuma(previous, center + offset + delta));
            }
            float score = error + .0001 * dot(float2(delta), float2(delta));
            if (score < best) { best = score; bestIndex = candidate; }
        }
    }
    candidateScores[index] = best;
    candidateIndices[index] = bestIndex;
    GroupMemoryBarrierWithGroupSync();

    if (index == 0) {
        best = 1e10;
        bestIndex = 81;
        [unroll] for (uint candidate = 0; candidate < 64; ++candidate) {
            float score = candidateScores[candidate];
            uint order = candidateIndices[candidate];
            if (score < best || (score == best && order < bestIndex)) {
                best = score;
                bestIndex = order;
            }
        }
        displacement = (reset || best > 1.1) ? float2(0,0)
            : float2((int2(bestIndex % 9, bestIndex / 9) - 4) * 4);
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(id.xy < size)) motion[id.xy] = displacement;
}
)";
// Resamples source-resolution motion to the upscaled resolution for the after-upscaling NR pass.
inline constexpr char ScaleMotionShader[] = R"(
Texture2D<float2> source : register(t0);
RWTexture2D<float2> target : register(u0);
cbuffer Params : register(b0) { uint2 sourceSize; uint2 targetSize; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= targetSize)) return;
    uint2 p = min(id.xy * sourceSize / targetSize, sourceSize - 1);
    target[id.xy] = source.Load(int3(p, 0)) * float2(targetSize) / float2(sourceSize);
}
)";
}
