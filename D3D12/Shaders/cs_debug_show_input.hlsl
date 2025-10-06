// cs_debug_show_input.hlsl
StructuredBuffer<float> InCHW : register(t0);
RWTexture2D<unorm float4> Dst : register(u0);
cbuffer CB : register(b0)
{
    uint W, H, C, _;
}

static const float3 MEAN = float3(0.485, 0.456, 0.406);
static const float3 STD = float3(0.229, 0.224, 0.225);

float3 sampleCHW(uint c, float2 uv)
{
    float2 p = uv * float2(W, H) - 0.5;
    int2 i = int2(clamp(round(p), 0.0, float2(W - 1, H - 1)));
    uint idx = i.y * W + i.x;
    return float3(
        InCHW[idx + 0 * W * H],
        InCHW[idx + 1 * W * H],
        InCHW[idx + 2 * W * H]);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;
    float2 uv = (id.xy + 0.5) / float2(W, H);
    float3 x = sampleCHW(0, uv); // 정규화된 값
    float3 rgb = x * STD + MEAN; // 역정규화
    Dst[id.xy] = float4(saturate(rgb), 1);
}
