// SRV: RGBA16F/RGBA8 SceneColor ¡æ UAV: NCHW float buffer
Texture2D<float4> srcTex : register(t0);
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
    x = (x - mean) * stdinv;

    uint idx = id.y * W + id.x;
    uint planeSize = W * H;
    dst[idx + 0 * planeSize] = x.r;
    dst[idx + 1 * planeSize] = x.g;
    dst[idx + 2 * planeSize] = x.b;
}
