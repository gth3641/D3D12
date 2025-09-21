
// cs_postprocess.hlsl : NCHW(float) ¡æ RGBA8
StructuredBuffer<float> src : register(t0);
RWTexture2D<uint> outTex : register(u0); // R8G8B8A8_UNORM
cbuffer CB2 : register(b0)
{
    uint W;
    uint H;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;
    uint idx = id.y * W + id.x;
    float3 rgb = float3(
    src[idx + 0 * H * W],
    src[idx + 1 * H * W],
    src[idx + 2 * H * W]);
    rgb = saturate(rgb); // ÇÊ¿ä½Ã scale
    uint4 u = uint4(rgb * 255.0, 255);
    outTex[id.xy] = (u.r) | (u.g << 8) | (u.b << 16) | (u.a << 24);
}