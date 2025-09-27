Texture2D<float4> gScene : register(t0);
RWStructuredBuffer<float> gNCHW : register(u0);

cbuffer CB : register(b0)
{
    uint W;
    uint H;
    uint C;
    uint _pad;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    float3 rgb = gScene.Load(int3(id.xy, 0)).rgb; // R16G16B16A16_FLOAT ¡æ float·Î ±×´ë·Î ¿È
    uint base = id.y * W + id.x;
    uint plane = W * H;

    gNCHW[base + 0 * plane] = rgb.r;
    gNCHW[base + 1 * plane] = rgb.g;
    gNCHW[base + 2 * plane] = rgb.b;
}
