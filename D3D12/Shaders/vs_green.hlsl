struct VSOut
{
    float4 pos : SV_Position;
};
VSOut main(uint vid : SV_VertexID)
{
    float2 pos[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    VSOut o;
    o.pos = float4(pos[vid], 0, 1);
    return o;
}