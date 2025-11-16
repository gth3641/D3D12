#include "RootSignature.hlsl"
#include "Pipeline.hlsli"

ScreenCB screenCB : register(b1);

[RootSignature(ROOTSIG)]
void main(
    in float2 pos : Position,
    in float2 uv : Texcoord,
    out float2 o_uv : Texcoord,
    out float4 o_pos : SV_Position
)
{
    float2 px = pos;
    
    px.x = ((px.x + 0.5f) / screenCB.ViewSize.x) * 2.0f - 1.0f;
    px.y = 1.0f - ((px.y + 0.5f) / screenCB.ViewSize.y) * 2.0f; // y-down ¡æ y-up º¯È¯
    
    o_pos = float4(px, 0.f, 1.f);
    o_uv = uv;
}

