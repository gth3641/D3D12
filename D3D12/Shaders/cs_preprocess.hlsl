// 루트파라미터: b0(상수), t0(SceneColor SRV), u0(ONNX 입력 UAV structured buffer float)
cbuffer CB : register(b0)
{
    uint W; // 네트워크 입력 폭
    uint H; // 네트워크 입력 높이
    uint C; // 채널 수(보통 3)
    uint _pad;
}

Texture2D<float4> gSrc : register(t0); // R16G16B16A16_FLOAT SRV
SamplerState gSamp : register(s0); // 루트시그에 넣어둔 static sampler(Clamp/Linear)
RWStructuredBuffer<float> gDst : register(u0); // DX12 UAV Buffer(Stride=4)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    // 소스와 입력 크기가 같다고 가정. 다르면 uv 스케일 추가 필요!
    float2 uv = (float2(id.xy) + 0.5) / float2(W, H);
    float4 c = gSrc.SampleLevel(gSamp, uv, 0);

    uint idx = id.y * W + id.x;
    uint plane = W * H;

    if (C >= 3)
    {
        gDst[idx + 0 * plane] = c.r;
        gDst[idx + 1 * plane] = c.g;
        gDst[idx + 2 * plane] = c.b;
    }
    else if (C == 1)
    {
        float y = dot(c.rgb, float3(0.299, 0.587, 0.114));
        gDst[idx] = y;
    }
    else
    {
        if (C > 0)
            gDst[idx + 0 * plane] = c.r;
        if (C > 1)
            gDst[idx + 1 * plane] = c.g;
    }
}
