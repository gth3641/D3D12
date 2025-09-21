
// cs_preprocess.hlsl : RGBA8 → NCHW(float) [0..1]
Texture2D srcTex : register(t0);
SamplerState samp : register(s0);
RWStructuredBuffer<float> dst : register(u0);
cbuffer CB : register(b0)
{
    uint W;
    uint H;
    float3 mean;
    float3 stdinv;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;
    float3 x = srcTex.Load(int3(id.xy, 0)).rgb;
    x = (x - mean) * stdinv; // 필요 없으면 mean=0, stdinv=1
    uint idx = id.y * W + id.x;
    dst[idx + 0 * H * W] = x.r;
    dst[idx + 1 * H * W] = x.g;
    dst[idx + 2 * H * W] = x.b;
}
