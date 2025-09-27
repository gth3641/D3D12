Texture2D gSrcTex : register(t0);
SamplerState gSamp : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TexCoord;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return gSrcTex.Sample(gSamp, i.uv);
}