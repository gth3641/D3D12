StructuredBuffer<float> gCHW : register(t0); // 지금은 입력 버퍼 그대로 보냄(모델 바인딩 전까지)
RWTexture2D<float4> gOut : register(u0); // UAV = R8G8B8A8_UNORM

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

    uint base = id.y * W + id.x;
    uint plane = W * H;

    float3 rgb = float3(
        gCHW[base + 0 * plane],
        gCHW[base + 1 * plane],
        gCHW[base + 2 * plane]
    );

    gOut[id.xy] = float4(saturate(rgb), 1.0);
}
