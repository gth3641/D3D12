Texture2D<float4> gScene : register(t0);
SamplerState gSamp : register(s0);
RWStructuredBuffer<float> gInput : register(u0);

cbuffer CB : register(b0)
{
    uint W; // ��Ʈ��ũ �Է� �ʺ�(224)
    uint H; // ��Ʈ��ũ �Է� ����(224)
    uint C; // ä��(���� 3)
    uint _pad;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint x = dtid.x, y = dtid.y;
    if (x >= W || y >= H)
        return;

    // ����� SceneColor �ػ�, �� �Է��� W,H �� uv�� ������
    float2 uv = (float2(x + 0.5, y + 0.5) / float2(W, H));
    float3 rgb = gScene.SampleLevel(gSamp, uv, 0).rgb;

    uint idx = y * W + x;
    uint plane = W * H;

    // NCHW�� ��ֱ�
    gInput[idx + 0 * plane] = rgb.r;
    gInput[idx + 1 * plane] = rgb.g;
    gInput[idx + 2 * plane] = rgb.b;
}