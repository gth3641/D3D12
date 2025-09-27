// t0: OutputCHW (StructuredBuffer<float>)
// u0: OnnxTex (RWTexture2D<uint>)  // R8G8B8A8_UNORM as uint-packed
// b0: { uint SrcW, SrcH, Channels, _pad }  // 모델 출력 크기 기준

StructuredBuffer<float> gOutCHW : register(t0);
RWTexture2D<uint> gDst : register(u0);
cbuffer CB : register(b0)
{
    uint SrcW;
    uint SrcH;
    uint Channels;
    uint _pad;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint dstW, dstH;
    gDst.GetDimensions(dstW, dstH);
    if (dtid.x >= dstW || dtid.y >= dstH)
        return;

    // dst 해상도 → src 해상도 비례 좌표
    float2 uv = (float2(dtid.xy) + 0.5) / float2(dstW, dstH);
    float2 src = uv * float2(SrcW, SrcH);

    uint x = (uint) src.x, y = (uint) src.y;
    uint srcIdx = y * SrcW + x;
    uint plane = SrcW * SrcH;

    float r = 0, g = 0, b = 0;
    if (Channels >= 3)
    {
        r = gOutCHW[srcIdx + 0 * plane];
        g = gOutCHW[srcIdx + 1 * plane];
        b = gOutCHW[srcIdx + 2 * plane];
    }
    else if (Channels == 1)
    {
        r = g = b = gOutCHW[srcIdx];
    }

    // 0..1 가정(필요시 범위 정규화 추가)
    r = saturate(r);
    g = saturate(g);
    b = saturate(b);
    uint ur = (uint) round(r * 255.0);
    uint ug = (uint) round(g * 255.0);
    uint ub = (uint) round(b * 255.0);
    uint ua = 255u;

    gDst[dtid.xy] = (ua << 24) | (ub << 16) | (ug << 8) | ur;
}
