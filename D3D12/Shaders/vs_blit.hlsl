// vs_blit.hlsl
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    // Ǯ��ũ�� �ﰢ�� (����, �»�+2h, ����+2w)
    float2 pos;
    if (vid == 0)
        pos = float2(-1.0, -1.0);
    else if (vid == 1)
        pos = float2(-1.0, 3.0);
    else
        pos = float2(3.0, -1.0);

    VSOut o;
    o.pos = float4(pos, 0.0, 1.0);

    // �� �ٽ�: y ������ (NDC y��, tex v��)
    o.uv = float2(pos.x * 0.5 + 0.5,
                  -pos.y * 0.5 + 0.5);

    return o;
}
