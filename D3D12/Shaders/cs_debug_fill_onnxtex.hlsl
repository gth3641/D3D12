// cs_debug_fill_onnxtex.hlsl
RWTexture2D<float4> gDst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint W, H;
    gDst.GetDimensions(W, H);
    if (id.x >= W || id.y >= H)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(W, H);
    gDst[id.xy] = float4(uv.x, uv.y, 0.0, 1.0); // X=빨강 그라디언트, Y=초록
}
