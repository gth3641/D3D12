// cs_copy_tex_to_tex2d.hlsl
Texture2D<float4> Src : register(t0);
SamplerState gSamp : register(s0); 
RWTexture2D<unorm float4> Dst : register(u0);

cbuffer CB : register(b0)
{
    uint W, H, _r0, _r1; // Dst Å©±â
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    float2 uv = (id.xy + 0.5) / float2(W, H);
    float4 c = Src.SampleLevel(gSamp, uv, 0);
    Dst[id.xy] = c;
}
