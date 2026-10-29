// SPDX-License-Identifier: MIT
// Adapted from Echo-Storm/ls-addon-manager fg_engine.cpp at 8d3eac351d42480326290b0f8965127a630b44f2.
// Copyright (c) 2026 Echo-Storm and andreiday. See ../EchoStorm/EchoStorm-LICENSE.txt.
// Host adaptation: GPU mean, low-resolution vector scaling, arbitrary MFG temporal fractions.
#pragma once
#include "../EchoStorm/engine/hdr_hlsl.h"
namespace framegen {
inline constexpr char EchoGuardShaders[]=NR_HDR_HLSL R"(
Texture2D<float4> tMade : register(t0);
Texture2D<float4> tBefore : register(t1);
Texture2D<float4> tNow : register(t2);
Texture2D<float2> tMotion : register(t3);
RWTexture2D<float4> uOut : register(u0);
StructuredBuffer<float> tMean : register(t4);
RWStructuredBuffer<float> uMean : register(u1);
cbuffer C : register(b0) { uint2 size; int radius; float agree; float apart; float reserved; uint encoding; float white; float fraction; };
float3 View(float3 c) { return ToSdr(c, encoding, white) * 255.0; }
float Dist(float3 a, float3 b) { const float3 d = abs(a - b); return max(d.x, max(d.y, d.z)); }
float2 Motion(int2 p) {
    uint w,h; tMotion.GetDimensions(w,h);
    const int2 q=clamp(int2((float2(p)+0.5)*float2(w,h)/float2(size)),int2(0,0),int2(w,h)-1);
    return tMotion[q]*float2(size)/float2(w,h);
}
// Bilinear endpoint sampling in pixel-centre coordinates, without border wrapping.
float3 SampleColor(Texture2D<float4> image, float2 p) {
    const int2 a=int2(floor(p));const float2 f=frac(p);
    return lerp(lerp(image[a].rgb,image[a+int2(1,0)].rgb,f.x),
                lerp(image[a+int2(0,1)].rgb,image[a+int2(1,1)].rgb,f.x),f.y);
}
bool Inside(float2 p,float margin) {
    return all(p>=margin) && all(p<=float2(size)-1.0-margin);
}
bool RepairMotionImpulse(int2 p,float3 made,out float3 corrected) {
    corrected=made;
    // This pass is calibrated on SDR replay data. Leave the existing HDR guard intact.
    if(reserved<0.5 || encoding!=0 || !Inside(float2(p),2.0))return false;
    const int2 ring[8]={int2(-2,-2),int2(0,-2),int2(2,-2),int2(2,0),
                       int2(2,2),int2(0,2),int2(-2,2),int2(-2,0)};
    float3 neighbors[8];float3 sum=0,lo=1,hi=0;
    [unroll]for(int i=0;i<8;++i) {
        float3 n=tMade[p+ring[i]].rgb;neighbors[i]=n;sum+=n;lo=min(lo,n);hi=max(hi,n);
    }
    const float3 reference=(sum-lo-hi)/6.0;
    uint support=0;sum=0;
    [unroll]for(int j=0;j<8;++j)if(Dist(neighbors[j],reference)<=12.0/255.0) {
        ++support;sum+=neighbors[j];
    }
    if(support<6)return false;
    const float3 spatial=sum/float(support);
    if(Dist(made,spatial)<=24.0/255.0)return false;
    // Motion is current -> previous. Solve the current coordinate of this
    // intermediate pixel; a half-vector lookup at p alone fails near boundaries.
    const float back=1.0-fraction;
    float2 v=Motion(p);
    if(!all(isfinite(v)) || length(v)<=3.0)return false;
    float2 q=float2(p)-back*v;
    [unroll]for(int k=0;k<2;++k) {
        if(!Inside(q,2.0))return false;
        v=Motion(int2(floor(q+0.5)));
        if(!all(isfinite(v)))return false;
        q=float2(p)-back*v;
    }
    if(!Inside(q,2.0))return false;
    const int2 at=int2(floor(q+0.5));v=Motion(at);
    if(!all(isfinite(v)) || length(q+back*v-float2(p))>=0.75)return false;
    const float2 previous=q+v;
    if(!Inside(previous,1.0))return false;
    const int2 cross[4]={int2(2,0),int2(-2,0),int2(0,2),int2(0,-2)};
    [unroll]for(int c=0;c<4;++c) {
        const float2 neighbor=Motion(at+cross[c]);
        if(!all(isfinite(neighbor)) || length(neighbor-v)>1.5)return false;
    }
    const float3 before=SampleColor(tBefore,previous),now=SampleColor(tNow,q);
    if(Dist(before,now)>8.0/255.0 || Dist(lerp(before,now,fraction),spatial)>12.0/255.0)return false;
    corrected=spatial;return true;
}
groupshared float lengths[256];
[numthreads(256,1,1)] void Mean(uint tid:SV_GroupIndex) {
    float s=0;
    for (uint i=tid;i<2304;i+=256) {
        const int2 p=int2((float2(i%64,i/64)+0.5)*float2(size)/float2(64,36));
        s+=length(Motion(p));
    }
    lengths[tid]=s; GroupMemoryBarrierWithGroupSync();
    for(uint n=128;n>0;n>>=1) { if(tid<n)lengths[tid]+=lengths[tid+n];GroupMemoryBarrierWithGroupSync(); }
    if(tid==0)uMean[0]=lengths[0]/2304.0;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= size.x || id.y >= size.y) return;
    const int2 p = int2(id.xy), last = int2(size) - 1;
    const float4 m = tMade[p];
    uOut[p] = m;
    float3 repaired;
    if(RepairMotionImpulse(p,m.rgb,repaired)) {uOut[p]=float4(repaired,m.a);return;}
    const float slow=max(3.0,0.2*tMean[0]);
    if (length(Motion(p)) > slow) return;
    // and slow around it too (5 of 8 samples on a ring 3 reaches out): a character is a slow region, a gap of sky in a canopy sweeping
    // past (flat, so its own motion reads as none) is not, and the leaf FSR moved through it belongs there
    uint around = 0;
    [unroll] for (int k = 0; k < 8; ++k) {
        const float a = k * 0.785398;
        around += length(Motion(clamp(p + int2(round(float2(cos(a), sin(a)) * (3.0 * radius))), int2(0, 0), last))) <= slow ? 1u : 0u;
    }
    if (around < 5) return;
    const float3 vb = View(tBefore[p].rgb), vn = View(tNow[p].rgb), vm = View(m.rgb);
    float dBN = Dist(vb, vn);
    [loop] for (int j = -radius; j <= radius && dBN > agree; ++j)
        [loop] for (int i = -radius; i <= radius; ++i) dBN = min(dBN, Dist(vb, View(tNow[clamp(p + int2(i, j), int2(0, 0), last)].rgb)));
    if (dBN > agree) return;
    float dMB = Dist(vm, vb), dMN = Dist(vm, vn);
    if (min(dMB, dMN) <= dBN + apart) return;
    [loop] for (int y = -radius; y <= radius; ++y)
        [loop] for (int x = -radius; x <= radius; ++x) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), last);
            dMB = min(dMB, Dist(vm, View(tBefore[q].rgb))); dMN = min(dMN, Dist(vm, View(tNow[q].rgb)));
        }
    const float t = saturate((min(dMB, dMN) - dBN - apart) / apart);
    uOut[p] = float4(lerp(m.rgb, lerp(tBefore[p].rgb,tNow[p].rgb,fraction), t), m.a);
}
)";
}
