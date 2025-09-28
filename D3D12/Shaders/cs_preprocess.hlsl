// ��Ʈ�Ķ����: b0(���), t0(SceneColor SRV), u0(ONNX �Է� UAV structured buffer float)
cbuffer CB : register(b0)
{
    uint W; // ��Ʈ��ũ �Է� ��
    uint H; // ��Ʈ��ũ �Է� ����
    uint C; // ä�� ��(���� 3)
    uint _pad;
}

Texture2D<float4> gSrc : register(t0); // R16G16B16A16_FLOAT SRV
SamplerState gSamp : register(s0); // ��Ʈ�ñ׿� �־�� static sampler(Clamp/Linear)
RWStructuredBuffer<float> gDst : register(u0); // DX12 UAV Buffer(Stride=4)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    // �ҽ��� �Է� ũ�Ⱑ ���ٰ� ����. �ٸ��� uv ������ �߰� �ʿ�!
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
