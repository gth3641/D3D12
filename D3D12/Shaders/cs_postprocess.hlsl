// cs_postprocess.hlsl
StructuredBuffer<float> gOut : register(t0); // CHW
RWTexture2D<unorm float4> gDst : register(u0);

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
    
    rgb = rgb * Gain + Bias;
    
    gDst[dtid.xy] = float4(saturate(rgb), 1);
}
