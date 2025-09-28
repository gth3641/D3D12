// ��Ʈ�Ķ����: b0(���), t0(ONNX ��� SRV  structured buffer float), u0(RGBA8 UAV)
cbuffer CB : register(b0)
{
    uint W; // ��� �ټ� ��
    uint H; // ��� �ټ� ����
    uint C; // ä�� ��(1/3/4 ��)
    uint _pad;
}

StructuredBuffer<float> gOut : register(t0); // DX12: Buffer SRV with StructureByteStride=4
RWTexture2D<float4> gDst : register(u0); // UAV: R8G8B8A8_UNORM (float4 0..1�� ���)

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

    // �ʿ�� ���� ����(��: -1..1 �� 0..1, 0..255 �� 0..1)�� ���⼭ ����
    gDst[uint2(id.xy)] = saturate(float4(r, g, b, 1.0));
}
