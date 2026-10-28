// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace processing {
// ST 2084, absolute light in nits. Matrix transforms use linear BT.2020 / BT.709 RGB.
inline constexpr char HdrFunctions[] = R"(
float3 pqDecode(float3 v) {
    float3 p=pow(saturate(v),1.0/78.84375);
    return 10000.0*pow(max(p-.8359375,0)/max(18.8515625-18.6875*p,1e-6),1.0/.1593017578125);
}
float3 pqEncode(float3 nits) {
    float3 p=pow(saturate(nits/10000.0),.1593017578125);
    return pow((.8359375+18.8515625*p)/(1+18.6875*p),78.84375);
}
float3 to709(float3 c) {
    return mul(float3x3(1.660491,-.587641,-.072850,-.124550,1.132900,-.008349,-.018151,-.100579,1.118730),c);
}
float3 to2020(float3 c) {
    return mul(float3x3(.627404,.329283,.043313,.069097,.919540,.011362,.016391,.088013,.895595),c);
}
float3 srgbDecode(float3 c) {
    return float3(c.r<=.04045 ? c.r/12.92 : pow((c.r+.055)/1.055,2.4),
                  c.g<=.04045 ? c.g/12.92 : pow((c.g+.055)/1.055,2.4),
                  c.b<=.04045 ? c.b/12.92 : pow((c.b+.055)/1.055,2.4));
}
float3 srgbEncode(float3 c) {
    c=max(c,0);
    return float3(c.r<=.0031308 ? c.r*12.92 : 1.055*pow(c.r,1/2.4)-.055,
                  c.g<=.0031308 ? c.g*12.92 : 1.055*pow(c.g,1/2.4)-.055,
                  c.b<=.0031308 ? c.b*12.92 : 1.055*pow(c.b,1/2.4)-.055);
}
float proxyGain(float3 light) { return 1+max(0,max(light.r,max(light.g,light.b))); }
)";
// Use the original quantized proxy as the residual baseline: an unchanged model result must
// preserve the original HDR pixel exactly, including highlights and out-of-sRGB colours.
inline const std::string HdrCompositeShader = std::string(HdrFunctions) + R"(
Texture2D<float4> enhanced : register(t0);
Texture2D<float4> hdrOriginal : register(t1);
Texture2D<float4> proxyOriginal : register(t2);
RWTexture2D<float4> result : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    uint w,h,sw,sh; result.GetDimensions(w,h); hdrOriginal.GetDimensions(sw,sh);
    if (id.x>=w || id.y>=h) return;
    float2 p=(float2(id.xy)+.5)*float2(sw,sh)/float2(w,h)-.5;
    int2 base=int2(floor(p)); float2 f=frac(p);
    float3 light=0,baseline=0;
    [unroll] for (int y=0;y<2;++y) [unroll] for (int x=0;x<2;++x) {
        int2 at=clamp(base+int2(x,y),int2(0,0),int2(sw,sh)-1);
        float weight=(x ? f.x : 1-f.x)*(y ? f.y : 1-f.y);
        light+=weight*to709(pqDecode(hdrOriginal.Load(int3(at,0)).rgb))/203.0;
        baseline+=weight*srgbDecode(proxyOriginal.Load(int3(at,0)).rgb);
    }
    float3 delta=clamp(srgbDecode(enhanced.Load(int3(id.xy,0)).rgb)-baseline,-.25,.25);
    float3 restored=light+proxyGain(light)*delta;
    result[id.xy]=float4(pqEncode(max(to2020(restored)*203.0,0)),1);
}
)";
}
