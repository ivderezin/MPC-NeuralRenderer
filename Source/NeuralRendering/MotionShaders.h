// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace neural {
inline constexpr char PrepareShader[] = R"(
Texture2D<float4> source : register(t0);
RWTexture2D<float4> color : register(u0);
cbuffer Params : register(b0) { uint2 size; uint2 origin; uint reset; uint3 padding; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= size)) return;
    color[id.xy] = float4(source.Load(int3(id.xy + origin, 0)).rgb, 1);
}
)";
inline constexpr char MotionShader[] = R"(
Texture2D<float4> current : register(t0);
Texture2D<float4> previous : register(t1);
RWTexture2D<float2> motion : register(u0);
cbuffer Params : register(b0) { uint2 size; uint2 origin; uint reset; uint3 padding; };
groupshared float2 displacement;
float luminance(float3 c) { return dot(c, float3(.2126,.7152,.0722)); }
float sampleLuma(Texture2D<float4> image, int2 p) {
    return luminance(image.Load(int3(clamp(p, int2(0,0), int2(size)-1),0)).rgb);
}
[numthreads(8,8,1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint3 id : SV_DispatchThreadID) {
    if (all(thread.xy == 0)) {
        displacement = 0;
        if (!reset) {
            int2 center = int2(group.xy * 8 + 4);
            float best = 1e10;
            for (int y=-4; y<=4; ++y) for (int x=-4; x<=4; ++x) {
                int2 delta = int2(x,y)*4;
                float error = 0;
                for (int py=-1; py<=1; ++py) for (int px=-1; px<=1; ++px) {
                    int2 p = center + int2(px,py)*3;
                    error += abs(sampleLuma(current,p) - sampleLuma(previous,p+delta));
                }
                // Prefer stationary matches in flat regions.
                float score = error + .0001 * dot(float2(delta),float2(delta));
                if (score < best) { best = score; displacement = delta; }
            }
            if (best > 1.1) displacement = 0;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(id.xy < size)) motion[id.xy] = displacement;
}
)";
}
