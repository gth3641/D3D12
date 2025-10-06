Texture2D<float4> Src : register(t0);
SamplerState gSamp : register(s0);
RWStructuredBuffer<float> Out : register(u0);
cbuffer CB
{
    uint W;
    uint H;
    uint C;
    uint Flags;
}; // bit0: BGR 스왑

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;
    float2 uv = (id.xy + 0.5) / float2(W, H);
    float3 rgb = Src.SampleLevel(gSamp, uv, 0).rgb; // [0..1] sRGB 값 그대로
    if (Flags & 1)
        rgb = rgb.bgr; // BGR→RGB
    uint idx = id.y * W + id.x;
    Out[idx + 0 * W * H] = rgb.r;
    Out[idx + 1 * W * H] = rgb.g;
    Out[idx + 2 * W * H] = rgb.b;
}
