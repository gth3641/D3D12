// cs_postprocess.hlsl
StructuredBuffer<float> gOut : register(t0); // CHW
RWTexture2D<unorm float4> gDst : register(u0);

cbuffer CB : register(b0)
{
    uint SrcW, SrcH, SrcC, _r0;
    uint DstW, DstH, _r1, _r2;
    float Gain;
    float Bias;
    float _pad0;
    float _pad1; // ★ 추가
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

    float3 rgb = float3(r, g, b) * Gain + Bias;
    gDst[dtid.xy] = float4(saturate(rgb), 1);
}

//// cs_postprocess.hlsl
//StructuredBuffer<float> gOut : register(t0); // CHW, len = SrcW*SrcH*SrcC
//RWTexture2D<unorm float4> gDst : register(u0); // 화면크기 텍스처

//cbuffer CB : register(b0)
//{
//    uint SrcW;
//    uint SrcH;
//    uint SrcC;
//    uint _r0;
//    uint DstW;
//    uint DstH;
//    uint _r1;
//    uint _r2;
//}

//float sampleCHW_bilinear(uint c, float2 uv)
//{
//    float2 p = uv * float2(SrcW, SrcH) - 0.5;
//    int2 p0 = int2(floor(p));
//    float2 f = frac(p);

//    int x0 = clamp(p0.x, 0, (int) SrcW - 1);
//    int y0 = clamp(p0.y, 0, (int) SrcH - 1);
//    int x1 = min(x0 + 1, (int) SrcW - 1);
//    int y1 = min(y0 + 1, (int) SrcH - 1);

//    uint plane = SrcW * SrcH;
//    uint i00 = y0 * SrcW + x0;
//    uint i10 = y0 * SrcW + x1;
//    uint i01 = y1 * SrcW + x0;
//    uint i11 = y1 * SrcW + x1;

//    float v00 = gOut[i00 + c * plane];
//    float v10 = gOut[i10 + c * plane];
//    float v01 = gOut[i01 + c * plane];
//    float v11 = gOut[i11 + c * plane];

//    float vx0 = lerp(v00, v10, f.x);
//    float vx1 = lerp(v01, v11, f.x);
//    return lerp(vx0, vx1, f.y);
//}

//[numthreads(8, 8, 1)]
//void main(uint3 dtid : SV_DispatchThreadID)
//{
//    if (dtid.x >= DstW || dtid.y >= DstH)
//        return;

//    float2 uv = (dtid.xy + 0.5) / float2(DstW, DstH);
//    // 필요하면 뒤집기: uv.y = 1.0 - uv.y;

//    float r = (SrcC > 0) ? sampleCHW_bilinear(0, uv) : 0.0;
//    float g = (SrcC > 1) ? sampleCHW_bilinear(1, uv) : r;
//    float b = (SrcC > 2) ? sampleCHW_bilinear(2, uv) : r;

//    // 간단 범위 보정
//    float mx = max(r, max(g, b)), mn = min(r, min(g, b));
//    if (mx > 2.0)
//    {
//        r *= 1.0 / 255.0;
//        g *= 1.0 / 255.0;
//        b *= 1.0 / 255.0;
//    }
//    else if (mn < -0.1)
//    {
//        r = r * 0.5 + 0.5;
//        g = g * 0.5 + 0.5;
//        b = b * 0.5 + 0.5;
//    }

//    gDst[dtid.xy] = float4(saturate(r), saturate(g), saturate(b), 1);
//}
