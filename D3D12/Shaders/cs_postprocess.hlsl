// 루트파라미터: b0(상수), t0(ONNX 출력 SRV  structured buffer float), u0(RGBA8 UAV)
cbuffer CB : register(b0)
{
    uint W; // 출력 텐서 폭
    uint H; // 출력 텐서 높이
    uint C; // 채널 수(1/3/4 등)
    uint _pad;
}

StructuredBuffer<float> gOut : register(t0); // DX12: Buffer SRV with StructureByteStride=4
RWTexture2D<float4> gDst : register(u0); // UAV: R8G8B8A8_UNORM (float4 0..1로 기록)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    uint idx = id.y * W + id.x;
    uint plane = W * H;

    float r = (C > 0) ? gOut[idx + 0 * plane] : 0.0;
    float g = (C > 1) ? gOut[idx + 1 * plane] : r;
    float b = (C > 2) ? gOut[idx + 2 * plane] : r;

    // 필요시 범위 보정(예: -1..1 → 0..1, 0..255 → 0..1)을 여기서 수행
    gDst[uint2(id.xy)] = saturate(float4(r, g, b, 1.0));
}
