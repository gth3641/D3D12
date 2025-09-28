Texture2D<float4> gScene : register(t0);
SamplerState gSamp : register(s0);
RWStructuredBuffer<float> gInput : register(u0);

cbuffer CB : register(b0)
{
    uint W; // 네트워크 입력 너비(224)
    uint H; // 네트워크 입력 높이(224)
    uint C; // 채널(보통 3)
    uint _pad;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint x = dtid.x, y = dtid.y;
    if (x >= W || y >= H)
        return;

    // 장면은 SceneColor 해상도, 모델 입력은 W,H → uv로 리샘플
    float2 uv = (float2(x + 0.5, y + 0.5) / float2(W, H));
    float3 rgb = gScene.SampleLevel(gSamp, uv, 0).rgb;

    uint idx = y * W + x;
    uint plane = W * H;

    // NCHW로 써넣기
    gInput[idx + 0 * plane] = rgb.r;
    gInput[idx + 1 * plane] = rgb.g;
    gInput[idx + 2 * plane] = rgb.b;
}