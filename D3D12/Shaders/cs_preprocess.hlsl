// cs_preprocess.hlsl
cbuffer CB : register(b0)
{
    uint W, H, C, Flags;
};
Texture2D<float4> Src : register(t0);
SamplerState Smp : register(s0);
RWStructuredBuffer<float> Out : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    float2 uv = (float2(id.x + 0.5, id.y + 0.5) / float2(W, H));
    float3 rgb = Src.SampleLevel(Smp, uv, 0).rgb;
    if (Flags & 0x10)
        rgb = rgb.bgr;

    uint idx = id.y * W + id.x;
    uint plane = W * H;

    // CHW layout
    Out[idx + 0 * plane] = rgb.r;
    Out[idx + 1 * plane] = rgb.g;
    Out[idx + 2 * plane] = rgb.b;
}

