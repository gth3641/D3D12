// cs_postprocess.hlsl

static const uint OUT_TANH = 0x0001; // [-1,1] -> [0,1]
static const uint OUT_255 = 0x0002; // [0,255] -> [0,1]

StructuredBuffer<float> gOut : register(t0); // CHW
//RWTexture2D<unorm float4> gDst : register(u0);
RWTexture2D<float4> gDst : register(u0); // ★ unorm 제거!

cbuffer CB : register(b0)
{
    uint SrcW, SrcH, SrcC, Flags;
    uint DstW, DstH, _r1, _r2;
    float Gain;
    float Bias;
    float _pad0;
    float _pad1;
}
float sampleCHW_bilinear(uint c, float2 uv)
{
    float2 p = uv * float2(SrcW, SrcH) - 0.5;
    int2 p0 = int2(floor(p));
    float2 f = frac(p);
    int x0 = clamp(p0.x, 0, (int) SrcW - 1);
    int y0 = clamp(p0.y, 0, (int) SrcH - 1);
    int x1 = min(x0 + 1, (int) SrcW - 1);
    int y1 = min(y0 + 1, (int) SrcH - 1);

    uint plane = SrcW * SrcH;
    uint i00 = y0 * SrcW + x0;
    uint i10 = y0 * SrcW + x1;
    uint i01 = y1 * SrcW + x0;
    uint i11 = y1 * SrcW + x1;

    float v00 = gOut[i00 + c * plane];
    float v10 = gOut[i10 + c * plane];
    float v01 = gOut[i01 + c * plane];
    float v11 = gOut[i11 + c * plane];

    return lerp(lerp(v00, v10, f.x), lerp(v01, v11, f.x), f.y);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= DstW || dtid.y >= DstH)
        return;
    float2 uv = (dtid.xy + 0.5) / float2(DstW, DstH);

    float r = (SrcC > 0) ? sampleCHW_bilinear(0, uv) : 0.0;
    float g = (SrcC > 1) ? sampleCHW_bilinear(1, uv) : r;
    float b = (SrcC > 2) ? sampleCHW_bilinear(2, uv) : r;

    float3 rgb = float3(r, g, b);
    if (Flags & 0x10)
    {
        rgb = float3(r, g, b) / 255.0;
    }
    
    gDst[dtid.xy] = float4(saturate(rgb), 1);
}




//// cs_postprocess.hlsl (예시)
//cbuffer CB2 : register(b0)
//{
//    uint SrcW, SrcH, SrcC, Flags;
//    uint DstW, DstH, _r1, _r2;
//    float Gain, Bias, _f0, _f1;
//}

//StructuredBuffer<float> In : register(t0); // 모델 출력 [C,H,W] 플레인
//RWTexture2D<float4> OutTex : register(u0); // 최종 텍스처

//float3 LinearToSRGB(float3 c)
//{
//    float3 lo = 12.92 * c;
//    float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
//    return lerp(lo, hi, step(0.0031308, c));
//}

//[numthreads(8, 8, 1)]
//void main(uint3 id : SV_DispatchThreadID)
//{
//    if (id.x >= DstW || id.y >= DstH)
//        return;

//    // 여기서는 SrcW==DstW, SrcH==DstH 가정 (다르면 스케일링 로직 추가)
//    uint x = id.x, y = id.y;
//    uint idx = y * SrcW + x;
//    uint plane = SrcW * SrcH;

//    float r = In[idx + 0 * plane];
//    float g = In[idx + 1 * plane];
//    float b = In[idx + 2 * plane];

//    float3 c = float3(r, g, b) * Gain + Bias;

//    // 필요 시 Linear→sRGB
//    // if (Flags & 0x2) c = LinearToSRGB(saturate(c));

//    OutTex[uint2(x, y)] = float4(c, 1.0);
//}



//StructuredBuffer<float> gIn : register(t0); // 모델 출력 [C,H,W] CHW
//RWTexture2D<float4> gOut : register(u0);

//cbuffer CB : register(b0)
//{
//    uint SrcW, SrcH, SrcC, PostFlags; // SrcC=3 기대
//    uint DstW, DstH, _r1, _r2;
//    float Gain, Bias, _f0, _f1; // 추가 스케일/바이어스(옵션)
//}

//uint Plane(uint c, uint w, uint h)
//{
//    return c * w * h;
//}
//uint Index(uint x, uint y, uint w)
//{
//    return y * w + x;
//}

//[numthreads(8, 8, 1)]
//void main(uint3 tid : SV_DispatchThreadID)
//{
//    if (tid.x >= DstW || tid.y >= DstH)
//        return;

//    // 최근접 스케일 (필요하면 bilinear 샘플링으로 개선 가능)
//    uint sx = min(SrcW - 1, (uint) round((float) tid.x * (float) SrcW / (float) DstW));
//    uint sy = min(SrcH - 1, (uint) round((float) tid.y * (float) SrcH / (float) DstH));

//    uint p = Index(sx, sy, SrcW);
//    float r = (SrcC > 0) ? gIn[Plane(0, SrcW, SrcH) + p] : 0.0;
//    float g = (SrcC > 1) ? gIn[Plane(1, SrcW, SrcH) + p] : r;
//    float b = (SrcC > 2) ? gIn[Plane(2, SrcW, SrcH) + p] : r;

//    // tanh 출력이면 [-1,1] -> [0,1]
//    if (PostFlags & 0x0002 /*POST_TANH_TO01*/)
//    {
//        r = r * 0.5 + 0.5;
//        g = g * 0.5 + 0.5;
//        b = b * 0.5 + 0.5;
//    }

//    // 0..255 범위면 /255
//    if (PostFlags & 0x0004 /*POST_DIV_255*/)
//    {
//        r *= (1.0 / 255.0);
//        g *= (1.0 / 255.0);
//        b *= (1.0 / 255.0);
//    }

//    // 추가 Gain/Bias
//    r = r * Gain + Bias;
//    g = g * Gain + Bias;
//    b = b * Gain + Bias;

//    // BGR 스왑이 필요하면(드물지만 모델이 BGR로 낸다면)
//    if (PostFlags & 0x0001 /*POST_BGR_SWAP*/)
//    {
//        float t = r;
//        r = b;
//        b = t;
//    }

//    gOut[tid.xy] = float4(saturate(r), saturate(g), saturate(b), 1.0);
//}
